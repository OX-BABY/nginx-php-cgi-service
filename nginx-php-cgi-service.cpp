#include <windows.h>
#include <tchar.h>
#include <strsafe.h>
#include <atlconv.h>  
#include "php-cgi-spawner.h"
#include "nginx-php-cgi.h"
#pragma comment(lib, "advapi32.lib")

#define SVCNAME "nginx-php"
#define STR_BUFF_SIZE 1024
SERVICE_STATUS          gSvcStatus;
SERVICE_STATUS_HANDLE   gSvcStatusHandle;
HANDLE                  ghSvcStopEvent = NULL;
FILE* fpFile;
char* nginxStartCmd, * nginxStopCmd, * nginxWorkPath;
char* cgiStartCmd;
int cgiMaxChildren, cgiStartServers, cgiPort, cgiRestartDelay;

char szPath[MAX_PATH];
void SvcInstall(void);
void OutputLastError();
void SvcUninstall(void);
void WINAPI SvcCtrlHandler(DWORD);
void WINAPI SvcMain(DWORD, LPTSTR*);

void ReportSvcStatus(DWORD, DWORD, DWORD);
void SvcInit(DWORD, LPTSTR*);
void SvcReportEvent(LPTSTR);

static int WriteLogFile(const char* msg) {
	if (NULL != fpFile) {
		int count = fputs(msg, fpFile);
		fflush(fpFile);
		return count;
	}
	return -1;
}
static void GetLastFormatError(char* buf, size_t buf_size) {
	DWORD systemLocale = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),err_code= GetLastError();
	HLOCAL hLocal = NULL;
	if (!FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_ALLOCATE_BUFFER,
		NULL, err_code, systemLocale, (LPSTR)&hLocal, 0, NULL))
	{
		sprintf_s(buf, buf_size, "Format message failed with 0x%x\n", GetLastError());
		return;
	}
	sprintf_s(buf, buf_size, "Error Code: %u %s\n", err_code, (const char *)hLocal);
	LocalFree(hLocal);
}
void OutputLastError()
{
	char* buff = (char*)malloc(STR_BUFF_SIZE);
	GetLastFormatError(buff, STR_BUFF_SIZE);
	printf(buff);
	free(buff);
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
	if (argc > 1 && !GetModuleFileNameA(NULL, szPath, MAX_PATH))
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
	// TO_DO: Add any additional services for the process to this table.
	SERVICE_TABLE_ENTRY DispatchTable[] =
	{
		{ TEXT(SVCNAME), (LPSERVICE_MAIN_FUNCTION)SvcMain },
		{ NULL, NULL }
	};

	// This call returns when the service has stopped. 
	// The process should simply terminate when the call returns.

	if (!StartServiceCtrlDispatcher(DispatchTable))
	{
		SvcReportEvent(TEXT("StartServiceCtrlDispatcher"));
	}

}
PROCESS_INFORMATION nginxPi;//nginxProcessInfomation
STARTUPINFOA nginxSi;
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

	schService = CreateServiceA(
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

	schService = OpenServiceA(
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

	gSvcStatusHandle = RegisterServiceCtrlHandlerA(SVCNAME, SvcCtrlHandler);

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

	ghSvcStopEvent = CreateEventA(
		NULL,    // default security attributes
		TRUE,    // manual reset event
		FALSE,   // not signaled
		NULL);   // no name

	if (ghSvcStopEvent == NULL)
	{
		ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
		return;
	}

	// Report running status when initialization is complete.


	ReportSvcStatus(SERVICE_RUNNING, NO_ERROR, 0);
	if (!GetModuleFileNameA(NULL, szPath, MAX_PATH))
	{
		ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
		return;
	}
	int szPathLen = strnlen_s(szPath, STR_BUFF_SIZE);
	char szHFilePath[MAX_PATH] = { 0 };
	memcpy_s(szHFilePath, szPathLen, szPath, szPathLen);

	szPath[szPathLen - 3] = 'i';
	szPath[szPathLen - 2] = 'n';
	szPath[szPathLen - 1] = 'i';

	szHFilePath[szPathLen - 3] = 'l';
	szHFilePath[szPathLen - 2] = 'o';
	szHFilePath[szPathLen - 1] = 'g';
	fopen_s(&fpFile, szHFilePath, "a+");
	if (NULL == fpFile) {
		ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
		return;
	}
	// TO_DO: Perform work until service stops.
	nginxStartCmd = (char*)malloc(STR_BUFF_SIZE);
	nginxStopCmd = (char*)malloc(STR_BUFF_SIZE);
	nginxWorkPath = (char*)malloc(STR_BUFF_SIZE);
	GetPrivateProfileStringA("Nginx", "startCmd", NULL, nginxStartCmd, STR_BUFF_SIZE, szPath);
	GetPrivateProfileStringA("Nginx", "stopCmd", NULL, nginxStopCmd, STR_BUFF_SIZE, szPath);
	GetPrivateProfileStringA("Nginx", "workPath", NULL, nginxWorkPath, STR_BUFF_SIZE, szPath);

	cgiStartCmd = (char*)malloc(STR_BUFF_SIZE);
	GetPrivateProfileStringA("PHP_FPM", "startCmd", NULL, cgiStartCmd, STR_BUFF_SIZE, szPath);
	cgiMaxChildren = GetPrivateProfileIntA("PHP_FPM", "StartServers", 2, szPath);
	cgiStartServers = GetPrivateProfileIntA("PHP_FPM", "MaxChildren", 4, szPath);
	cgiPort = GetPrivateProfileIntA("PHP_FPM", "LocalPort", 9000, szPath);
	cgiRestartDelay = GetPrivateProfileIntA("PHP_FPM", "RestartDelay", 0, szPath);

	//WriteLogFile(nginxStartCmd);
	//WriteLogFile(nginxStopCmd);
	//WriteLogFile(nginxWorkPath);
	//WriteLogFile(cgiStartCmd);
	//GetPrivateProfileStringA("Main", "Fpm", "2+4", fpm, STR_BUFF_SIZE, szPath);

	ZeroMemory(&nginxSi, sizeof(nginxSi));
	nginxSi.cb = sizeof(nginxSi);
	ZeroMemory(&nginxPi, sizeof(nginxPi));

	if (strlen(nginxStartCmd) > 0) {
		DWORD systemLocale = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
		HLOCAL hLocal = NULL;
		char* buf = (char*)malloc(STR_BUFF_SIZE);
		if (!CreateProcessA(NULL, nginxStartCmd, NULL, NULL, TRUE, CREATE_NO_WINDOW,
			NULL, nginxWorkPath, &nginxSi, &nginxPi)) {
			OutputLastError();
			sprintf_s(buf, STR_BUFF_SIZE, "Failed create process%s.\n", nginxStartCmd);
			WriteLogFile((LPSTR)&hLocal);
			free(buf);
			free(nginxStartCmd);
			free(nginxStopCmd);
			ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
			return;
		}
		//CloseHandle(nginxPi.hProcess);
		CloseHandle(nginxPi.hThread);
		free(buf);
	}
	else {
		WriteLogFile("Please confirm the configuration file\n");
		free(nginxStartCmd);
		free(nginxStopCmd);
		ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
		return;
	}

	WriteLogFile("Nginx Process Start up Success");
	RunSpawner(cgiStartCmd, cgiPort, cgiStartServers, cgiMaxChildren, cgiRestartDelay);
	WriteLogFile("Run php-cgi Done");
	while (1)
	{
		// Check whether to stop the service.

		WaitForSingleObject(ghSvcStopEvent, INFINITE);
		ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
		free(nginxStartCmd);
		free(nginxStopCmd);
		return;
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
void WINAPI SvcCtrlHandler(DWORD dwCtrl)
{
	// Handle the requested control code. 

	switch (dwCtrl)
	{
	case SERVICE_CONTROL_SHUTDOWN:
	case SERVICE_CONTROL_STOP:
		ReportSvcStatus(SERVICE_STOP_PENDING, NO_ERROR, 0);
		STARTUPINFOA si;
		PROCESS_INFORMATION pi;
		ZeroMemory(&si, sizeof(si));
		nginxSi.cb = sizeof(si);
		ZeroMemory(&pi, sizeof(pi));
		StopSpawner();
		WriteLogFile("StopSpawner Done");
		CreateProcessA(NULL, nginxStopCmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, nginxWorkPath, &si, &pi);
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		WaitForSingleObject(nginxPi.hProcess, 2000);
		WriteLogFile("Stop Nginx Done");
		//CloseHandle(pi.hProcess);
		//DWORD dwExitCode;
		//GetExitCodeProcess(pi.hProcess, &dwExitCode);

		// Signal the service to stop.
		SetEvent(ghSvcStopEvent);
		ReportSvcStatus(gSvcStatus.dwCurrentState, NO_ERROR, 0);

		return;

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
	LPCSTR lpszStrings[2];
	char Buffer[80];

	hEventSource = RegisterEventSourceA(NULL, SVCNAME);

	if (NULL != hEventSource)
	{
		StringCchPrintfA(Buffer, 80, "%s failed with %d", szFunction, GetLastError());

		lpszStrings[0] = SVCNAME;
		lpszStrings[1] = Buffer;

		ReportEventA(hEventSource,        // event log handle
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
