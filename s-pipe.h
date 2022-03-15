#pragma once
#include "global.h"
typedef struct {
	OVERLAPPED oOverlap;
	HANDLE hPipeInst;
	TCHAR chRequest[STR_BUFF_SIZE];
	DWORD cbRead;
	TCHAR chReply[STR_BUFF_SIZE];
	DWORD cbToWrite;
} PIPEINST, * LPPIPEINST;

VOID DisconnectAndClose(LPPIPEINST);
BOOL CreateAndConnectInstance(LPOVERLAPPED, LPHANDLE, LPCTSTR);
BOOL ConnectToNewClient(HANDLE, LPOVERLAPPED);
VOID GetAnswerToRequest(LPPIPEINST);
VOID WINAPI CompletedWriteRoutine(DWORD, DWORD, LPOVERLAPPED);
VOID WINAPI CompletedReadRoutine(DWORD, DWORD, LPOVERLAPPED);
BOOL RunPipe(LPCTSTR);
BOOL CreateDACL(SECURITY_ATTRIBUTES*);