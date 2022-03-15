#define FD_SETSIZE 1
#include <winsock.h>
#include "php-cgi-spawner.h"

#pragma comment( lib, "kernel32.lib")
#pragma comment( lib, "ws2_32.lib")

#define MAX_SPAWN_HANDLES MAXIMUM_WAIT_OBJECTS

typedef struct _PHPSPWCTX
{
	SOCKET s;
	CRITICAL_SECTION cs, cs_helper;
	char* cmd;
	char* path;
	unsigned port;
	unsigned fcgis;
	unsigned helpers;
	unsigned restart_delay;
	char PHP_FCGI_MAX_REQUESTS[16];
	char PHP_HELP_MAX_REQUESTS[16];
	PROCESS_INFORMATION pi;
	STARTUPINFOA si;
	HANDLE hFCGIs[MAX_SPAWN_HANDLES];
	LPHANDLE hHelperFCGIs;
	unsigned helpers_delay;
	volatile LONG helpers_running;
} PHPSPWCTX;
static PHPSPWCTX ctx;
static BOOL RUNABLE;

static __forceinline void memsym(void* mem, size_t size, char sym)
{
	while (size--)
		((volatile char*)mem)[size] = sym;
}

static char spawn_fcgi(HANDLE* hFCGI, BOOL is_perm)
{
	char isok = 1;
	if(is_perm) EnterCriticalSection(&ctx.cs);
	for (; RUNABLE; )
	{
		// set correct PHP_FCGI_MAX_REQUESTS
		{
			char* val;

			if (is_perm)
				val = ctx.PHP_FCGI_MAX_REQUESTS;
			else
				val = ctx.PHP_HELP_MAX_REQUESTS;

			if (val[0] == 0)
				val = NULL;

			SetEnvironmentVariableA("PHP_FCGI_MAX_REQUESTS", val);
		}

		if (!RUNABLE || !CreateProcessA(NULL, ctx.cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, ctx.path, &ctx.si, &ctx.pi))
		{
			isok = 0;
			break;
		}

		CloseHandle(ctx.pi.hThread);
		*hFCGI = ctx.pi.hProcess;
		break;
	}
	if (is_perm) LeaveCriticalSection(&ctx.cs);
	return isok;
}

static DWORD WINAPI helper_holder(HANDLE _lphFCGI)
{
	LPHANDLE lphFCGI = (LPHANDLE)_lphFCGI;
	HANDLE hFCGI = *lphFCGI;
	WaitForSingleObject(hFCGI, INFINITE);
	CloseHandle(hFCGI);
	//*lphFCGI = INVALID_HANDLE_VALUE;
	InterlockedDecrement(&ctx.helpers_running);
	return 0;
}

static DWORD WINAPI helpers_thread(void* unused)
{
	struct timeval tv = { 0, 0 };
	struct timeval* timeout;

	(void)unused;

	for (;; )
	{
		ULONGLONG dwTick = GetTickCount64();
		timeout = &tv;

		for (;; )
		{
			int err;

			fd_set fs;
			fs.fd_count = 1;
			fs.fd_array[0] = ctx.s;

			err = select(0, &fs, NULL, NULL, timeout);

			if (err == SOCKET_ERROR)
				return 0;

			if (timeout)
			{
				if (err == 0)
				{
					timeout = NULL;
					continue;
				}

				if (GetTickCount64() - dwTick > ctx.helpers_delay)
					timeout = NULL;
			}

			if (err == 1 && timeout == NULL)
				break;

			if (timeout)
				Sleep(1);
		}

		if (ctx.helpers_running >= (LONG)ctx.helpers)
			continue;

		InterlockedIncrement(&ctx.helpers_running);

		{
			HANDLE h;
			LPHANDLE hHelper = ctx.hHelperFCGIs;
			EnterCriticalSection(&ctx.cs_helper);
			for (; *hHelper != INVALID_HANDLE_VALUE; hHelper++);
			if (!spawn_fcgi(hHelper, FALSE)){
				LeaveCriticalSection(&ctx.cs_helper);
			    break;
			}
			LeaveCriticalSection(&ctx.cs_helper);
			h = CreateThread(NULL, 0, &helper_holder, hHelper, 0, NULL);

			if (h == NULL)
				break;

			CloseHandle(h);
		}
	}

	return 0;
}
static void closeFCGIhHandles(LPHANDLE hHandles, size_t size) {
	if (NULL == hHandles)return;
	size_t i = 0;
	for (; i < size; i++) {
		HANDLE hProcess = hHandles[i];
		if (INVALID_HANDLE_VALUE != hProcess) {
			DWORD dwExitCode;
			GetExitCodeProcess(hProcess, &dwExitCode); //»ñÈ¡ÍË³öÂë
			if (dwExitCode == STILL_ACTIVE)
			{
				if (TerminateProcess(hProcess, 0))
				{
					WaitForSingleObject(hProcess, INFINITE);
				}
				hHandles[i] = INVALID_HANDLE_VALUE;
			}
			CloseHandle(hProcess);
		}
	}
}
static void perma_thread(BOOL helpers)
{
	for (; RUNABLE; )
	{
		unsigned i;

		for (i = 0; i < ctx.fcgis; i++)
		{
			if (!RUNABLE) return;
			if (ctx.hFCGIs[i] == INVALID_HANDLE_VALUE && !spawn_fcgi(&ctx.hFCGIs[i], TRUE))
				return;
		}

		if (helpers)
		{
			HANDLE h;

			h = CreateThread(NULL, 0, &helpers_thread, NULL, 0, NULL);

			if (h == NULL)
				return;

			CloseHandle(h);
			helpers = FALSE;
		}

		if (ctx.fcgis == 0)
			Sleep(INFINITE);

		WaitForMultipleObjects(ctx.fcgis, ctx.hFCGIs, FALSE, INFINITE);
		for (i = 0; i < ctx.fcgis; i++)
		{
			if (ctx.hFCGIs[i] != INVALID_HANDLE_VALUE)
			{
				DWORD dwExitCode;
				if (!GetExitCodeProcess(ctx.hFCGIs[i], &dwExitCode))
					continue;

				if (dwExitCode != STILL_ACTIVE)
				{
					CloseHandle(ctx.hFCGIs[i]);
					ctx.hFCGIs[i] = INVALID_HANDLE_VALUE;
				}
			}
		}

		// optional restart delay
		// https://github.com/deemru/php-cgi-spawner/issues/3
		if (ctx.restart_delay)
			Sleep(ctx.restart_delay);
	}
}

void __cdecl RunSpawner(const char* cmd, const char* path, int iLocalPort, int iMinChildren, int iMaxChildren, int iRestartDelay)
{
	if (NULL == cmd) return;
	size_t iCmdLen = strlen(cmd) + 1, iPathLen = strlen(path) + 1;
	LPSTR sCmd = (LPSTR)LocalAlloc(LMEM_ZEROINIT, iCmdLen), sPath = NULL;
	if (NULL == sCmd) { return; }
	if (NULL != path) {
		sPath = (LPSTR)LocalAlloc(LMEM_ZEROINIT, iCmdLen);
		if (NULL == sPath) {
			LocalFree(sCmd);
			return;
		}
		strcpy_s(sPath, iPathLen, path);
	}
	strcpy_s(sCmd, iCmdLen, cmd);

	for (;; )
	{
		BOOL is_helpers = FALSE;

		ctx.cmd = sCmd;
		ctx.path = sPath;
		ctx.port = iLocalPort;

		// fcgis(min) + helpers = iMaxChildren
		if (iMaxChildren > iMinChildren) {
			ctx.helpers = iMaxChildren - iMinChildren;
			is_helpers = TRUE;
			// (LPHANDLE)LocalAlloc(LMEM_ZEROINIT, sizeof(HANDLE) * ctx.helpers);
			ctx.hHelperFCGIs = (LPHANDLE)malloc(sizeof(HANDLE) * ctx.helpers);
			//memsym(ctx.hFCGIs, sizeof(ctx.hFCGIs), -1);
			for (size_t i = 0; i < ctx.helpers; i++)ctx.hHelperFCGIs[i] = INVALID_HANDLE_VALUE;
		}

		ctx.fcgis = iMinChildren;
		ctx.restart_delay = iRestartDelay;
		ctx.helpers_delay = ctx.restart_delay ? ctx.restart_delay : 100;

		if ((ctx.fcgis < 1 && !is_helpers) || ctx.fcgis > MAX_SPAWN_HANDLES)
			break;

		// SOCKET
		{
			WSADATA wsaData;
			struct sockaddr_in fcgi_addr_in;
			int opt = 1;

			if (WSAStartup(MAKEWORD(2, 0), &wsaData))
				break;

			if (-1 == (ctx.s = socket(AF_INET, SOCK_STREAM, 0)))
				break;

			if (setsockopt(ctx.s, SOL_SOCKET, SO_REUSEADDR,
				(const char*)&opt, sizeof(opt)) < 0)
				break;

			fcgi_addr_in.sin_family = AF_INET;
			fcgi_addr_in.sin_addr.s_addr = 0x0100007f; // 127.0.0.1
			fcgi_addr_in.sin_port = htons((unsigned short)ctx.port);

			if (-1 == bind(ctx.s, (struct sockaddr*)&fcgi_addr_in,
				sizeof(fcgi_addr_in)))
				break;

			if (-1 == listen(ctx.s, SOMAXCONN))
				break;
		}

		// close before cgis (msdn: All processes start at shutdown level 0x280)
		if (!SetProcessShutdownParameters(0x380, SHUTDOWN_NORETRY))
			break;

		// php-cgi crash silently if restart delay is >= 1000 ms
		// https://github.com/deemru/php-cgi-spawner/issues/3
		if (ctx.restart_delay >= 1000)
			SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);

		InitializeCriticalSection(&ctx.cs);
		InitializeCriticalSection(&ctx.cs_helper);

		ctx.PHP_FCGI_MAX_REQUESTS[0] = 0;
		GetEnvironmentVariableA("PHP_FCGI_MAX_REQUESTS",
			ctx.PHP_FCGI_MAX_REQUESTS,
			sizeof(ctx.PHP_FCGI_MAX_REQUESTS));

		ctx.PHP_HELP_MAX_REQUESTS[0] = 0;
		GetEnvironmentVariableA("PHP_HELP_MAX_REQUESTS",
			ctx.PHP_HELP_MAX_REQUESTS,
			sizeof(ctx.PHP_HELP_MAX_REQUESTS));

		memsym(&ctx.si, sizeof(ctx.si), 0);
		ctx.si.cb = sizeof(STARTUPINFO);
		ctx.si.dwFlags = STARTF_USESTDHANDLES;
		ctx.si.hStdOutput = INVALID_HANDLE_VALUE;
		ctx.si.hStdError = INVALID_HANDLE_VALUE;
		ctx.si.hStdInput = (HANDLE)ctx.s;

		memsym(ctx.hFCGIs, sizeof(ctx.hFCGIs), -1);

		perma_thread(is_helpers);
		break;
	}
	if (sCmd != NULL)LocalFree(sCmd);
	if (sPath != NULL)LocalFree(sPath);
	StopSpawner();
	//ExitProcess(0);
}

void StopSpawner() {
	if (!RUNABLE)return;
	RUNABLE = FALSE;
	EnterCriticalSection(&ctx.cs_helper);
	closeFCGIhHandles(ctx.hHelperFCGIs, ctx.helpers);
	LeaveCriticalSection(&ctx.cs_helper);

	EnterCriticalSection(&ctx.cs);
	closeFCGIhHandles(ctx.hFCGIs, ctx.fcgis);
	LeaveCriticalSection(&ctx.cs);
}

void ReloadSpawner(LPCGI_PROFILE profile) {
	if (!RUNABLE) RunSpawnerProfile(profile);
	EnterCriticalSection(&ctx.cs_helper);
	closeFCGIhHandles(ctx.hHelperFCGIs, ctx.helpers);
	LeaveCriticalSection(&ctx.cs_helper);

	closeFCGIhHandles(ctx.hFCGIs, ctx.fcgis);
}
DWORD RunSpawnerProfile(void* vProfile) {
	if (NULL == vProfile) return -1;
	LPCGI_PROFILE profile = (LPCGI_PROFILE)vProfile;
	RUNABLE = TRUE;
	RunSpawner(profile->cgiStartCmd, profile->cgiWorkPath, profile->cgiPort, profile->cgiMinChildren, profile->cgiMaxChildren, profile->cgiRestartDelay);
	return 0;
}