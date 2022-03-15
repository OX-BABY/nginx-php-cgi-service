#pragma once
#include "global.h"
#ifndef _NGINX_SERVICE
#define _NGINX_SERVICE 1
#define FACILITY_SYSTEM                  0x0
#define FACILITY_STUBS                   0x3
#define FACILITY_RUNTIME                 0x2
#define FACILITY_IO_ERROR_CODE           0x4
#define STATUS_SEVERITY_WARNING          0x2
#define STATUS_SEVERITY_SUCCESS          0x0
#define STATUS_SEVERITY_INFORMATIONAL    0x1
#define STATUS_SEVERITY_ERROR            0x3
#define SVC_ERROR                        ((DWORD)0xC0020001L)

#define CMD_START 1
#define CMD_STOP 2
#define CMD_RELOAD 4


size_t WriteLog(LPCTSTR);
size_t WriteLogF(LPCTSTR fmt, ...);
BOOL RunExec(LPCTSTR name, int option, LPCTSTR args);
void OutputLastError();
#endif // !_NGINX_SERVICE
