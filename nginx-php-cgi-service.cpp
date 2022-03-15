#include <windows.h>
#include <tchar.h>
#include <strsafe.h>
#include <atlconv.h>
#include "php-cgi-spawner.h"
#include "nginx-php-cgi-service.h"
#include "s-pipe.h"
#include "smap.h"
#pragma comment(lib, "advapi32.lib")

#define SVCNAME TEXT("nginx-php")

void SvcInstall(void);
void SvcUninstall(void);
BOOL CtrlHandler(DWORD fdwctrltype);
void WINAPI SvcCtrlHandler(DWORD);
void WINAPI SvcMain(DWORD, LPTSTR*);

void ReportSvcStatus(DWORD, DWORD, DWORD);
void SvcInit(DWORD, LPTSTR*);
void SvcReportEvent(LPTSTR);
BOOL initLog(LPCTSTR path);
int ReadAllProfile(LPCTSTR lpFileName);
BOOL StartSpawner(LPCGI_PROFILE);
typedef struct _EXE_PROFILE {
	size_t iConfigCount = 0;
	LPTSTR pStartCmd;
	LPTSTR pStopCmd;
	LPTSTR pReloadCmd;
	LPTSTR pWorkPath;
	PROCESS_INFORMATION PI;//nginxProcessInfomation
	STARTUPINFO SI;
	LPSMAP lpSmap;
} EXE_PROFILE, * LPEXE_PROFILE;

LPSMAP lpProfileMap;

LPEXE_PROFILE ReadExeProfile(LPCTSTR name,LPCTSTR lpFileName,BOOL fReload);
LPCGI_PROFILE ReadCGIProfile();


SERVICE_STATUS          gSvcStatus;
SERVICE_STATUS_HANDLE   gSvcStatusHandle;
HANDLE                  ghSvcStopEvent = NULL;
LPEXE_PROFILE nginxProfile = NULL;
LPCGI_PROFILE cgiProfile = NULL;
FILE* fFile = NULL;

TCHAR szPath[MAX_PATH];
char szPathA[MAX_PATH*sizeof(TCHAR)];
void DestoryALL() {
	/*ResetEvent(lpPipeInst->oOverlap.hEvent);*/
	if (NULL != lpProfileMap)
	{
		destorySmap(&lpProfileMap);
		lpProfileMap = NULL;
	}
	if (NULL != cgiProfile) {
		free(cgiProfile->cgiStartCmd);
		free(cgiProfile);
	}
	//DisconnectAndClose(NULL);
	WriteLog(TEXT("Destoried ALL\n"));
	fclose(fFile);
	fFile = NULL;
}
BOOL initLog(LPCTSTR lpFileName) {
	if (NULL == fFile) {
		if (NULL == lpFileName)return FALSE;
		fFile = _tfsopen(lpFileName, TEXT("a+"), _SH_DENYWR);
		if (NULL == fFile) {
			printf("CreateFile Error %d\n", GetLastError());
			return FALSE;
		}
	}
	return TRUE;
}
size_t WriteLog(LPCTSTR fmt) {
	if (NULL == fmt) return 0;
	int iWrite;
	if (NULL != fFile) {
		iWrite = _fputts(fmt, fFile);
	}
	else {
		iWrite = _putts(fmt);
	}
	return iWrite;
}
size_t WriteLogF(LPCTSTR fmt, ...) {
	if (NULL == fmt) return 0;
	int iWrited = 0;
	va_list list = NULL;
	va_start(list, fmt);
	if (NULL != fFile) {
		iWrited = _vftprintf(fFile, fmt, list);
		_vtprintf(fmt, list);
		//if (fmt[lstrlen(fmt)] != '\n') {
		//	_ftprintf(fFile, TEXT(""));
		//	puts("\n");
		//}
	}
	else {
		iWrited = _vtprintf(fmt, list);
		//if (fmt[lstrlen(fmt)] != '\n') {
		//	puts("\n");
		//}
	}
	va_end(list);
	return iWrited;
}
void GetLastFormatError(LPTSTR buff, size_t buf_size) {
	DWORD systemLocale = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), err_code = GetLastError();
	HLOCAL hLocal = NULL;
	if (!FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_ALLOCATE_BUFFER,
		NULL, err_code, systemLocale, (LPTSTR)&hLocal, 0, NULL))
	{
		StringCbPrintf(buff, buf_size, TEXT("格式化错误消息失败，错误代码 %d\n"), GetLastError());
		return;
	}
	StringCbPrintf(buff, buf_size, TEXT("错误代码: %u %s\n"), err_code, (LPCTSTR)hLocal);
	LocalFree(hLocal);
}
void OutputLastError()
{
	LPTSTR buff = (LPTSTR)malloc(STR_BUFF_SIZE);
	if (NULL != buff)
	{
		GetLastFormatError(buff, STR_BUFF_SIZE);
		WriteLog(buff);
		free(buff);
	}
}
//
// Purpose: 
//   Entry point for the process
//
// Parameters:
//   None
// 
// Return value:
//   None
//
void __cdecl _tmain(int argc, TCHAR* argv[])
{
	// If command-line parameter is "install", install the service. 
	// Otherwise, the service is probably being started by the SCM.
	if (argc > 1 && !GetModuleFileName(NULL, szPath, MAX_PATH))
	{
		OutputLastError();
		return;
	}
	if (lstrcmpi(argv[1], TEXT("install")) == 0)
	{
		SvcInstall();
		return;
	}
	else if (lstrcmpi(argv[1], TEXT("delete")) == 0) {
		SvcUninstall();
		return;
	}
	else if (lstrcmpi(argv[1], TEXT("reinstall")) == 0) {
		SvcUninstall();
		SvcInstall();
		return;
	}
	else if (lstrcmpi(argv[1], TEXT("debug")) == 0) {
		if (SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlHandler, TRUE))
		{
			WriteLog(TEXT("the control handler is installed."));
		}
		SvcInit(argc,argv);
		return;
	}
	// TO_DO: Add any additional services for the process to this table.
	SERVICE_TABLE_ENTRY DispatchTable[] =
	{
		{ SVCNAME, (LPSERVICE_MAIN_FUNCTION)SvcMain },
		{ NULL, NULL }
	};

	// This call returns when the service has stopped. 
	// The process should simply terminate when the call returns.

	if (!StartServiceCtrlDispatcher(DispatchTable))
	{
		SvcReportEvent(TEXT("StartServiceCtrlDispatcher"));
	}

}
//
// Purpose: 
//   Installs a service in the SCM database
//
// Parameters:
//   None
// 
// Return value:
//   None
//
void SvcInstall()
{
	SC_HANDLE schSCManager;
	SC_HANDLE schService;
	// Get a handle to the SCM database. 

	schSCManager = OpenSCManager(
		NULL,                    // local computer
		NULL,                    // ServicesActive database 
		SC_MANAGER_ALL_ACCESS);  // full access rights 

	if (NULL == schSCManager)
	{
		printf("OpenSCManager failed\n");
		return;
	}

	// Create the service

	schService = CreateService(
		schSCManager,              // SCM database 
		SVCNAME,                   // name of service 
		SVCNAME,                   // service name to display 
		SERVICE_ALL_ACCESS,        // desired access 
		SERVICE_WIN32_OWN_PROCESS, // service type 
		SERVICE_DEMAND_START,      // start type 
		SERVICE_ERROR_NORMAL,      // error control type 
		szPath,                    // path to service's binary 
		NULL,                      // no load ordering group 
		NULL,                      // no tag identifier 
		NULL,                      // no dependencies 
		NULL,                      // LocalSystem account 
		NULL);                     // no password 

	if (schService == NULL)
	{
		printf("CreateService failed\n");
		CloseServiceHandle(schSCManager);
		return;
	}
	else printf("Service installed successfully\n");

	CloseServiceHandle(schService);
	CloseServiceHandle(schSCManager);
}

void SvcUninstall() {
	SC_HANDLE schSCManager;
	SC_HANDLE schService;
 

	schSCManager = OpenSCManagerA(
		NULL,                    // local computer
		NULL,                    // ServicesActive database 
		SC_MANAGER_ALL_ACCESS);  // full access rights 

	if (NULL == schSCManager)
	{
		printf("OpenSCManager failed\n");
		return;
	}

	schService = OpenService(
		schSCManager,       // SCM database 
		SVCNAME,          // name of service 
		DELETE);
	if (!schService) {
		printf("OpenService failed\n");
		CloseServiceHandle(schSCManager);
		return;
	}
	if (DeleteService(schService)) {
		printf("Service Uninstall successfully\n");
		CloseServiceHandle(schService);
		CloseServiceHandle(schSCManager);
	}
	else {
		printf("UninstallService failed\n");
		CloseServiceHandle(schSCManager);
	}
	
}
//
// Purpose: 
//   Entry point for the service
//
// Parameters:
//   dwArgc - Number of arguments in the lpszArgv array
//   lpszArgv - Array of strings. The first string is the name of
//     the service and subsequent strings are passed by the process
//     that called the StartService function to start the service.
// 
// Return value:
//   None.
//
void WINAPI SvcMain(DWORD dwArgc, LPTSTR* lpszArgv)
{
	// Register the handler function for the service

	gSvcStatusHandle = RegisterServiceCtrlHandler(SVCNAME, SvcCtrlHandler);

	if (!gSvcStatusHandle)
	{
		SvcReportEvent(TEXT("RegisterServiceCtrlHandler"));
		return;
	}

	// These SERVICE_STATUS members remain as set here

	gSvcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	gSvcStatus.dwServiceSpecificExitCode = 0;

	// Report initial status to the SCM

	ReportSvcStatus(SERVICE_START_PENDING, NO_ERROR, 3000);

	// Perform service-specific initialization and work.

	SvcInit(dwArgc, lpszArgv);
}

//
// Purpose: 
//   The service code
//
// Parameters:
//   dwArgc - Number of arguments in the lpszArgv array
//   lpszArgv - Array of strings. The first string is the name of
//     the service and subsequent strings are passed by the process
//     that called the StartService function to start the service.
// 
// Return value:
//   None
//
void SvcInit(DWORD dwArgc, LPTSTR* lpszArgv)
{
	// TO_DO: Declare and set any required variables.
	//   Be sure to periodically call ReportSvcStatus() with 
	//   SERVICE_START_PENDING. If initialization fails, call
	//   ReportSvcStatus with SERVICE_STOPPED.

	// Create an event. The control handler function, SvcCtrlHandler,
	// signals this event when it receives the stop control code.
	DWORD dwPathLen;
	ghSvcStopEvent = CreateEventA(
		NULL,    // default security attributes
		TRUE,    // manual reset event
		FALSE,   // not signaled
		NULL);   // no name
	ReportSvcStatus(SERVICE_START_PENDING, NO_ERROR, 0);
	if (ghSvcStopEvent == NULL)
	{
		ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
		return;
	}

	// Report running status when initialization is complete.

	dwPathLen = GetModuleFileName(NULL, szPath, MAX_PATH);
	if (dwPathLen<=0|| dwPathLen>MAX_PATH)
	{
		ReportSvcStatus(SERVICE_STOPPED, ERROR_INVALID_VARIANT, 0);
		return;
	}

	TCHAR szLogFilePath[MAX_PATH] = { 0 };
	lstrcpy(szLogFilePath, szPath);
	//memcpy_s(szLogFilePath, MAX_PATH, szPath, dwPathLen);
	lstrcpy(szPath + dwPathLen - 3, TEXT("ini"));
	lstrcpy(szLogFilePath + dwPathLen - 3, TEXT("log"));

	WideCharToMultiByte(65001, 0, szPath, -1, szPathA, MAX_PATH * sizeof(TCHAR), NULL, NULL);

	if (!initLog(szLogFilePath)) {
		ReportSvcStatus(SERVICE_STOPPED, ERROR_INVALID_VARIANT, 0);
		return;
	}
	WriteLog(TEXT("Service Begin Init\n"));
	
	if (!RunPipe(TEXT("\\\\.\\pipe\\nginx_php_cgi_service"))) {
		ReportSvcStatus(SERVICE_STOPPED, ERROR_INVALID_VARIANT, 0);
		//TODO
		return;
	}
	
	ReadAllProfile(szPath);
	nginxProfile = ReadExeProfile(TEXT("Nginx"), szPath,FALSE);
	cgiProfile    = ReadCGIProfile();

	if (nginxProfile == NULL) {
		WriteLog(TEXT("Failed ReadExeProfile\n"));
		ReportSvcStatus(SERVICE_STOPPED, ERROR_INVALID_VARIANT, 0);
		return;
	}
	if (cgiProfile == NULL) {
		WriteLog(TEXT("Failed ReadCGIProfile\n"));
		ReportSvcStatus(SERVICE_STOPPED, ERROR_INVALID_VARIANT, 0);
		return;
	}
	else if (!StartSpawner(cgiProfile)) {
		WriteLog(TEXT("Failed Start PHP-CGI\n"));
		ReportSvcStatus(SERVICE_STOPPED, ERROR_INVALID_VARIANT, 0);
		return;
	}
	if (RunExec(TEXT("Nginx"), CMD_START, NULL)) {
		WriteLog(TEXT("Nginx Process Start up Success\n"));
	}
	else {
		OutputLastError();
		ReportSvcStatus(SERVICE_STOPPED, ERROR_INVALID_VARIANT, 0);
	}
	
	//RunSpawner(cgiStartCmd, cgiPort, cgiStartServers, cgiMaxChildren, cgiRestartDelay);

	WriteLog(TEXT("Run php-cgi Done\n"));
	ReportSvcStatus(SERVICE_RUNNING, NO_ERROR, 0);
	while (1)
	{
		WaitForSingleObject(ghSvcStopEvent, INFINITE);
		ReportSvcStatus(SERVICE_STOP_PENDING, NO_ERROR, 0);
		if (NULL != nginxProfile)
		{
			RunExec(TEXT("Nginx"), CMD_STOP, NULL);
			WriteLog(TEXT("Stop Nginx Done\n"));
		}

		StopSpawner();
		WriteLog(TEXT("StopSpawner Done\n"));
		DestoryALL();
		ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
		break;
	}
}

//
// Purpose: 
//   Sets the current service status and reports it to the SCM.
//
// Parameters:
//   dwCurrentState - The current state (see SERVICE_STATUS)
//   dwWin32ExitCode - The system error code
//   dwWaitHint - Estimated time for pending operation, 
//     in milliseconds
// 
// Return value:
//   None
//
void ReportSvcStatus(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwWaitHint)
{
	static DWORD dwCheckPoint = 1;

	// Fill in the SERVICE_STATUS structure.

	gSvcStatus.dwCurrentState = dwCurrentState;
	gSvcStatus.dwWin32ExitCode = dwWin32ExitCode;
	gSvcStatus.dwWaitHint = dwWaitHint;

	if (dwCurrentState == SERVICE_START_PENDING)
		gSvcStatus.dwControlsAccepted = 0;
	else gSvcStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;

	if ((dwCurrentState == SERVICE_RUNNING) ||
		(dwCurrentState == SERVICE_STOPPED))
		gSvcStatus.dwCheckPoint = 0;
	else gSvcStatus.dwCheckPoint = dwCheckPoint++;

	// Report the status of the service to the SCM.
	SetServiceStatus(gSvcStatusHandle, &gSvcStatus);
}

//
// Purpose: 
//   Called by SCM whenever a control code is sent to the service
//   using the ControlService function.
//
// Parameters:
//   dwCtrl - control code
// 
// Return value:
//   None
//
BOOL CtrlHandler(DWORD fdwctrltype)
{
	switch (fdwctrltype)
	{
		// handle the ctrl-c signal.
	case CTRL_C_EVENT:
		printf("ctrl-c event\n\n");
	case CTRL_CLOSE_EVENT:
		printf("ctrl-close event\n\n");
		SvcCtrlHandler(SERVICE_CONTROL_STOP);
		return FALSE;
		// pass other signals to the next handler.
	case CTRL_BREAK_EVENT:
		printf("ctrl-break event\n\n");
		return FALSE;
	case CTRL_LOGOFF_EVENT:
		printf("ctrl-logoff event\n\n");
		return FALSE;
	case CTRL_SHUTDOWN_EVENT:
		printf("ctrl-shutdown event\n\n");
		return FALSE;
	default:
		return FALSE;
	}
}

void WINAPI SvcCtrlHandler(DWORD dwCtrl)
{
	// Handle the requested control code. 

	switch (dwCtrl)
	{
	case SERVICE_CONTROL_SHUTDOWN:
		WriteLog(TEXT("Recv SERVICE_CONTROL_SHUTDOWN\n"));
	case SERVICE_CONTROL_STOP:
		WriteLog(TEXT("Recv SERVICE_CONTROL_STOP\n"));
		SetEvent(ghSvcStopEvent);
		//ReportSvcStatus(gSvcStatus.dwCurrentState, NO_ERROR, 0);
		break;
	case SERVICE_CONTROL_INTERROGATE:
		break;

	default:
		break;
	}
}

//
// Purpose: 
//   Logs messages to the event log
//
// Parameters:
//   szFunction - name of function that failed
// 
// Return value:
//   None
//
// Remarks:
//   The service must have an entry in the Application event log.
//
void SvcReportEvent(LPTSTR szFunction)
{
	HANDLE hEventSource;
	LPCTSTR lpszStrings[2];
	TCHAR Buffer[80];
	hEventSource = RegisterEventSource(NULL, SVCNAME);

	if (NULL != hEventSource)
	{
		StringCchPrintf(Buffer, 80, TEXT("%s failed with %d"), szFunction, GetLastError());

		lpszStrings[0] = SVCNAME;
		lpszStrings[1] = Buffer;

		ReportEvent(hEventSource,        // event log handle
			EVENTLOG_ERROR_TYPE, // event type
			0,                   // event category
			SVC_ERROR,           // event identifier
			NULL,                // no security identifier
			2,                   // size of lpszStrings array
			0,                   // no binary data
			lpszStrings,         // array of strings
			NULL);               // no binary data

		DeregisterEventSource(hEventSource);
	}
}
BOOL RunCmd(LPTSTR cmd, LPCTSTR args, LPCTSTR path, LPSTARTUPINFO lpsi, LPPROCESS_INFORMATION lppi) {
	if (cmd == NULL)return FALSE;
	LPTSTR _cmd = (LPTSTR)LocalAlloc(LMEM_ZEROINIT, STR_BUFF_SIZE);
	if (NULL == _cmd)
		return FALSE;
	if (NULL == args)
		//memcpy_s(_cmd, STR_BUFF_SIZE, cmd, lstrlen(cmd) * sizeof(TCHAR));
		//StringCbCopy(_cmd, STR_BUFF_SIZE_A, cmd);
		lstrcpy(_cmd, cmd);
	else
		StringCbPrintf(_cmd, STR_BUFF_SIZE, TEXT("%s %s"), cmd, args);
	BOOL success = CreateProcess(NULL, _cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, path, lpsi, lppi);
	if (!success) {
		LPTSTR errmsg = (LPTSTR)malloc(STR_BUFF_SIZE);
		if (NULL != errmsg)
		{
			GetLastFormatError(errmsg, STR_BUFF_SIZE);
			WriteLog(errmsg);
			free(errmsg);
		}
		WriteLog(_cmd);
	}
	LocalFree(_cmd);
	return success;
}
BOOL RunExec(LPCTSTR name, int option, LPCTSTR args) {
	LPSMAP node = findSmap(lpProfileMap, name);
	if (NULL == node||NULL == node->value)return FALSE;

	LPEXE_PROFILE profile = (LPEXE_PROFILE)node->value;
	BOOL bSuccess = TRUE;
	if (option == CMD_RELOAD) {
		bSuccess = RunCmd(profile->pReloadCmd, args, profile->pWorkPath, &profile->SI, &profile->PI);
	}
	else {
		if (option & CMD_STOP)
		{
			bSuccess = RunCmd(profile->pStopCmd, args, profile->pWorkPath, &profile->SI, &profile->PI);
		}
		if ((option & CMD_START) && bSuccess) {
			if (option & CMD_STOP) {
				WaitForSingleObject(profile->PI.hProcess, 1000);
			}
			bSuccess = RunCmd(profile->pStartCmd, args, profile->pWorkPath, &profile->SI, &profile->PI);
		}
	}
	return bSuccess;
}
void DestoryProfile(LPEXE_PROFILE lpExeProfile) {
	if (NULL == lpExeProfile) return;
	LocalFree((HLOCAL)lpExeProfile->pStartCmd);
	LocalFree((HLOCAL)lpExeProfile->pStopCmd);
	LocalFree((HLOCAL)lpExeProfile->pReloadCmd);
	LocalFree((HLOCAL)lpExeProfile->pWorkPath);
	LocalFree((HLOCAL)lpExeProfile);
}

int ReadAllProfile(LPCTSTR lpFileName) {
	int c = 0;
	HLOCAL hNames = LocalAlloc(LMEM_ZEROINIT, STR_BUFF_SIZE);
	if (NULL == hNames)return c;
	LPTSTR lpNames = (LPTSTR)hNames;
	DWORD dwNameLength;

	dwNameLength = GetPrivateProfileSectionNames(lpNames, STR_BUFF_SIZE_A, lpFileName);
	for (DWORD i = 0; i < dwNameLength; i++, lpNames += i)
	{
		WriteLogF(L"Read Process Section %s\n", lpNames);
		ReadExeProfile(lpNames, lpFileName, TRUE);
		c++;
		i += lstrlen(lpNames);
	}

	LocalFree((HLOCAL)hNames);
	return c;
}
LPEXE_PROFILE ReadExeProfile(LPCTSTR lpNames, LPCTSTR lpFileName, BOOL fReload) {
	LPSMAP smap = findSmap(lpProfileMap, lpNames);
	LPEXE_PROFILE profile = NULL == smap ? NULL : (LPEXE_PROFILE)smap->value;
	if (!fReload) {
		return profile;
	} else {
		DestoryProfile(profile);
		deleteSmap(lpProfileMap, smap);
	}
	LPTSTR hContent = (LPTSTR)LocalAlloc(LMEM_ZEROINIT, STR_BUFF_SIZE), lpContent = hContent;
	if (NULL == hContent) {
		return NULL;
	}
	profile = (LPEXE_PROFILE)LocalAlloc(LMEM_ZEROINIT, sizeof(EXE_PROFILE));
	if (NULL == profile) {
		LocalFree((HLOCAL)hContent);
		return NULL;
	}
	DWORD dwContentLength = GetPrivateProfileSection(lpNames, lpContent, STR_BUFF_SIZE_A, lpFileName), dwWrited = 0;

	for (DWORD i = 0; i < dwContentLength; i += dwWrited, lpContent += dwWrited) {
		dwWrited = lstrlen(lpContent)+1;
		LPTSTR iPos = lstrchr(lpContent, '=');
		if (NULL == iPos)
		{
			continue;
		}
		//=设置为空，相当于分割字符串.
		*(iPos++) = 0;
		int iLenKey = lstrlen(lpContent) + 1, iLenValue = lstrlen(iPos) + 1;

		LPTSTR lpKey = (LPTSTR)LocalAlloc(LMEM_ZEROINIT, iLenKey * sizeof(TCHAR)), lpValue = NULL;
		if (NULL == lpKey)continue;
		lpValue = (LPTSTR)LocalAlloc(LMEM_ZEROINIT, iLenValue * sizeof(TCHAR));
		if (NULL == lpValue) {
			LocalFree((HLOCAL)lpKey);
			continue;
		}
		lstrcpy(lpKey, lpContent);
		lstrcpy(lpValue, iPos);

		//profile->map.insert(std::pair<LPTSTR,LPTSTR>(lpKey,lpValue));
		if (!lstrcmp(TEXT("startCmd"), lpKey)) {
			profile->pStartCmd = lpValue;
		}
		else if (!lstrcmp(TEXT("stopCmd"), lpKey)) {
			profile->pStopCmd = lpValue;
		}
		else if (!lstrcmp(TEXT("reloadCmd"), lpKey)) {
			profile->pReloadCmd = lpValue;
		}
		else if (!lstrcmp(TEXT("workPath"), lpKey)) {
			profile->pWorkPath = lpValue;
		}

		LocalFree((HLOCAL)lpKey);
		(profile->iConfigCount)++;
	}

	LocalFree((HLOCAL)hContent);
	if (profile->pStartCmd == NULL || profile->iConfigCount > 0) {
		appendSmap(&lpProfileMap, lpNames, profile);
		return profile;
	}
	else {
		LocalFree((HLOCAL)profile);
		return NULL;
	}
	return profile;
}

LPCGI_PROFILE ReadCGIProfile() {
	LPCGI_PROFILE profile = (LPCGI_PROFILE)malloc(sizeof(CGI_PROFILE));
	if (NULL == profile)return NULL;
	profile->cgiStartCmd = (char*)malloc(STR_BUFF_SIZE);
	profile->cgiWorkPath = (char*)malloc(STR_BUFF_SIZE);
	if (NULL == profile->cgiStartCmd) {
		free(profile);
		return NULL;
	}
	
	GetPrivateProfileStringA("PHP_FPM", "startCmd", NULL, profile->cgiStartCmd, STR_BUFF_SIZE, szPathA);
	GetPrivateProfileStringA("PHP_FPM", "workPath", NULL, profile->cgiWorkPath, STR_BUFF_SIZE, szPathA);
	
	profile->cgiMinChildren = GetPrivateProfileIntA("PHP_FPM", "MinChildren", 2, szPathA);
	profile->cgiMaxChildren = GetPrivateProfileIntA("PHP_FPM", "MaxChildren", 4, szPathA);
	profile->cgiPort = GetPrivateProfileIntA("PHP_FPM", "LocalPort", 9000, szPathA);
	profile->cgiRestartDelay = GetPrivateProfileIntA("PHP_FPM", "RestartDelay", 0, szPathA);
	if (strlen(profile->cgiStartCmd) <= 0) {
		free(profile->cgiStartCmd);
		free(profile);
		return NULL;
	}
	return profile;
}

BOOL StartSpawner(LPCGI_PROFILE lpProfile) {
	HANDLE h = CreateThread(NULL, 0, &RunSpawnerProfile, lpProfile, 0, NULL);
	if (h == NULL)
		return FALSE;

	CloseHandle(h);
	return TRUE;
}