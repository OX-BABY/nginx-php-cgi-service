#define FD_SETSIZE 1
#include <winsock.h>
#include<string.h>
#include "php-cgi-spawner.h"
#pragma comment( lib, "kernel32.lib")
#pragma comment( lib, "ws2_32.lib")

#define MAX_SPAWN_HANDLES MAXIMUM_WAIT_OBJECTS

typedef struct _PHPSPWCTX
{
	SOCKET s;
	CRITICAL_SECTION cs;
	char* cmd;
	unsigned port;
	unsigned fcgis;
	unsigned helpers;
	unsigned restart_delay;
	char PHP_FCGI_MAX_REQUESTS[16];
	char PHP_HELP_MAX_REQUESTS[16];
	PROCESS_INFORMATION pi;//ProcessInfomation
	STARTUPINFOA si;//StartupInfo
	HANDLE hFCGIs[MAX_SPAWN_HANDLES];
	unsigned helpers_delay;
	volatile LONG helpers_running;
} PHPSPWCTX;

static PHPSPWCTX ctx;
static bool RUNABLE;


static __forceinline void memsym(void* mem, size_t size, char sym)
{
	while (size--)
		((volatile char*)mem)[size] = sym;
}
void printError() {
	DWORD systemLocale = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
	HLOCAL hLocal = NULL;
	if (!FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_ALLOCATE_BUFFER,
		NULL, GetLastError(), systemLocale, (LPWSTR)&hLocal, 0, NULL))
	{
		MessageBoxA(0, "Format message failed with 0x%x\n",0,0);
		return;
	}

	MessageBox(0, (LPCWSTR)LocalLock(hLocal), 0, 0);

}
static char spawn_fcgi(HANDLE* hFCGI, BOOL is_perm)
{
	char isok = 1;

	EnterCriticalSection(&ctx.cs);

	for (;; )
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

		if (!CreateProcessA(NULL, ctx.cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW,
			NULL, NULL, &ctx.si, &ctx.pi))
		{
			printError();
			isok = 0;
			break;
		}

		CloseHandle(ctx.pi.hThread);
		*hFCGI = ctx.pi.hProcess;
		break;
	}

	LeaveCriticalSection(&ctx.cs);

	return isok;
}

static DWORD WINAPI helper_holder(HANDLE hFCGI)
{
	WaitForSingleObject(hFCGI, INFINITE);
	CloseHandle(hFCGI);
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
		DWORD dwTick = GetTickCount();
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

				if (GetTickCount() - dwTick > ctx.helpers_delay)
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

			if (!spawn_fcgi(&h, FALSE))
				break;

			h = CreateThread(NULL, 0, &helper_holder, h, 0, NULL);

			if (h == NULL)
				break;

			CloseHandle(h);
		}
	}

	return 0;
}

static void perma_thread(BOOL helpers)
{
	for (;RUNABLE; )
	{
		unsigned i;

		for (i = 0; i < ctx.fcgis && RUNABLE; i++)
		{
			if ((ctx.hFCGIs[i] == INVALID_HANDLE_VALUE &&
				!spawn_fcgi(&ctx.hFCGIs[i], TRUE))|| !RUNABLE)
				return;
		}
		if (helpers && RUNABLE)
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

		for (i = 0; i < ctx.fcgis && RUNABLE; i++)
		{
			if (ctx.hFCGIs[i] != INVALID_HANDLE_VALUE)
			{
				DWORD dwExitCode;
				if (!GetExitCodeProcess(ctx.hFCGIs[i], &dwExitCode))
					return;

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

#define IS_QUOTE( c ) ( c == '"' )
#define IS_SPACE( c ) ( c == ' ' || c == '\t' )


void RunSpawner(const char* cmd, int iLocalPort, int iStartServers,int iMaxChildren, int iRestartDelay) {
	int iCmdLen = strlen(cmd);

	char *sCmd = (char*)malloc(iCmdLen+1);
	ZeroMemory(sCmd, iCmdLen + 1);
	strcpy_s(sCmd, iCmdLen + 1, cmd);
	RUNABLE = true;
	for (;RUNABLE; )
	{
		BOOL is_helpers = FALSE;

		ctx.cmd = sCmd;
		ctx.port = iLocalPort;

		// permanent fcgis + helpers count
		{
			ctx.helpers = iStartServers;
			if (ctx.helpers)
				is_helpers = TRUE;
		}

		ctx.fcgis = iMaxChildren;
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

			if (-1 == bind(ctx.s, (struct sockaddr*) & fcgi_addr_in,
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

	//return  0;
}
void StopSpawner() {
	if (!RUNABLE)return;
	RUNABLE = false;
	unsigned i;
	for (i = 0; i < ctx.fcgis; i++)
	{
		DWORD exitCode; //退出码
		//PROCESS_INFORMATION pro_info = ctx.pi;
		HANDLE hProcess = ctx.hFCGIs[i];
		GetExitCodeProcess(hProcess, &exitCode); //获取退出码
		TerminateProcess(hProcess, exitCode);
		// 关闭句柄
		//CloseHandle(pro_info.hThread);
		CloseHandle(hProcess);
		//if (ctx.hFCGIs[i] != INVALID_HANDLE_VALUE &&
		//	!spawn_fcgi(&ctx.hFCGIs[i], TRUE))
		//	return;
	}
}
BOOL WINAPI DllMain(
	_In_ HINSTANCE hinstDLL,
	_In_ DWORD     fdwReason,
	_In_ LPVOID    lpvReserved
) {
	return TRUE;
}
