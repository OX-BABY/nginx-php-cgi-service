#include <windows.h>
#include <stdio.h>
#include <tchar.h>
#include <aclapi.h>
#include <sddl.h>
#include "nginx-php-cgi-service.h"
#include "s-pipe.h"
#pragma comment(lib, "advapi32.lib")
#define PIPE_TIMEOUT 5000

DWORD WINAPI ServerAMainThread(HANDLE unused);
VOID WINAPI CompletedReadRoutine(DWORD dwErr, DWORD cbBytesRead, LPOVERLAPPED lpOverLap);

LPPIPEINST lpPipeInstance;

VOID GetAnswerToRequest(LPPIPEINST);
/*
 ************************************
* CompletedWriteRoutine
*     д��pipe��������ɺ���
*    �ӿڲμ�FileIOCompletionRoutine�ص���������**    ��д�������ʱ�����ã���ʼ������һ���ͻ��˵�����
*************************************
	   */
VOID WINAPI CompletedWriteRoutine(DWORD dwErr, DWORD cbWritten, LPOVERLAPPED lpOverLap) {
	printf("CompletedWriteRoutine dwErr %d,cbWritten %d,lpOverLap %0llx\r\n", dwErr, cbWritten, (INT64)lpOverLap);
	LPPIPEINST lpPipeInst = (LPPIPEINST)lpOverLap;
	BOOL fRead = FALSE;
	// ����overlapʵ��
	// ���û�д���
	if ((dwErr == 0) && (cbWritten == lpPipeInst->cbToWrite)) {
		fRead = ReadFileEx(lpPipeInst->hPipeInst,
			lpPipeInst->chRequest,
			STR_BUFF_SIZE,
			(LPOVERLAPPED)lpPipeInst,
			// д��������ɺ󣬵���CompletedReadRoutine
			(LPOVERLAPPED_COMPLETION_ROUTINE)CompletedReadRoutine);
	}
	if (!fRead) {
		// �����Ͽ�����
		printf("READ ERROR %d\r\n", GetLastError());
		//ERROR_BROKEN_PIPE
		DisconnectAndClose(lpPipeInst);
	}
	else {
		WriteConsole(GetStdHandle(STD_OUTPUT_HANDLE), lpPipeInst->chRequest, fRead, NULL, NULL);
	}
}
/*
 ************************************
* CompletedReadRoutine
*     ��ȡpipe��������ɺ���
*    �ӿڲμ�FileIOCompletionRoutine�ص���������**    �����������ʱ�����ã�д��ظ�
*************************************
*/
VOID WINAPI CompletedReadRoutine(DWORD dwErr, DWORD cbBytesRead, LPOVERLAPPED lpOverLap)
{
	// ����overlapʵ��
	LPPIPEINST lpPipeInst = (LPPIPEINST)lpOverLap;
	printf("CompletedReadRoutine dwErr %d,cbBytesRead %d,lstrlen %d,lpOverLap %0llx\r\n", dwErr, cbBytesRead,lstrlen(lpPipeInst->chRequest), (INT64)lpOverLap);
	
	BOOL fWrite = FALSE;
	// ���û�д���
	if ((dwErr == 0) && (cbBytesRead != 0)) {
		memset(lpPipeInst->chRequest + cbBytesRead / sizeof(TCHAR), 0, sizeof(TCHAR));
		// ���ݿͻ��˵��������ɻظ�
		GetAnswerToRequest(lpPipeInst);
		// ���ظ�д�뵽pipe
		fWrite = WriteFileEx(lpPipeInst->hPipeInst,
			lpPipeInst->chReply,
			// ����Ӧд��pipe
			lpPipeInst->cbToWrite,
			(LPOVERLAPPED)lpPipeInst,
			// д����ɺ󣬵���CompletedWriteRoutine
			(LPOVERLAPPED_COMPLETION_ROUTINE)CompletedWriteRoutine);
		//ZeroMemory(lpPipeInst->chRequest, cbBytesRead);
	}
	if (!fWrite) // �����Ͽ�����
		DisconnectAndClose(lpPipeInst);
	else {
		printf("CompletedReadRoutine Done, Writed %d\r\n", fWrite);
	}
}

BOOL RunPipe(LPCTSTR lpPipeName) {
	HANDLE hThread = CreateThread(NULL, 0, &ServerAMainThread, (LPVOID)lpPipeName, 0, NULL);
	if (NULL != hThread && INVALID_HANDLE_VALUE != hThread) {
		CloseHandle(hThread);
		return TRUE;
	}
	return FALSE;
}

/*
 ************************************
* int main(VOID)
* ����    pipe ͨ�ŷ����������
*************************************
	   */

DWORD WINAPI ServerAMainThread(HANDLE hParam)
{
	HANDLE hConnectEvent, hPipe;
	OVERLAPPED oConnect;
	LPPIPEINST lpPipeInst;
	DWORD dwWait, cbRet;
	BOOL fSuccess, fPendingIO;
	LPCTSTR lpPipeName = hParam != NULL ? (LPCTSTR)hParam : TEXT("\\\\.\\pipe\\ConsoleApplication1");
	// �������Ӳ������¼�����
	hConnectEvent = CreateEvent(NULL,		// Ĭ������
		TRUE, // �ֹ�reset
		TRUE, // ��ʼ״̬ signaled
		NULL); // δ����
	if (hConnectEvent == NULL||hConnectEvent == INVALID_HANDLE_VALUE)
	{
		printf("CreateEvent failed with % d.\n\r\n", GetLastError());
		return 0;
	}
	// OVERLAPPED �¼�
	oConnect.hEvent = hConnectEvent;
	// ��������ʵ�����ȴ�����
	fPendingIO = CreateAndConnectInstance(&oConnect,&hPipe, lpPipeName);
	while (1) {
		// �ȴ��ͻ������ӻ��д�������
		printf("�ȴ��ͻ������ӻ��д�������\r\n");
		dwWait = WaitForSingleObjectEx(hConnectEvent, // �ȴ����¼�
			INFINITE, // ���޵ȴ�
			TRUE);
		printf("dwWait Result %d\r\n", dwWait);
		switch (dwWait) {
			case 0:
				// pending
				if (fPendingIO)
				{
					// ��ȡ Overlapped I / O �Ľ��
					fSuccess = GetOverlappedResult(hPipe,
						&oConnect,// pipe ��� OVERLAPPED �ṹ
						&cbRet,// �Ѿ����͵�������
						FALSE);// ���ȴ�

					if (!fSuccess) {
						printf("ConnectNamedPipe(% d)\n\r\n", GetLastError());
						return 0;
					}
				}
				// �����ڴ�
				lpPipeInst = (LPPIPEINST)HeapAlloc(GetProcessHeap(), 0, sizeof(PIPEINST));
				if (lpPipeInst == NULL) {
					printf("GlobalAlloc failed(% d)\n\r\n", GetLastError());
					return 0;
				}
				lpPipeInst->hPipeInst = hPipe;
				// ����д��ע��CompletedWriteRoutine��CompletedReadRoutine���໥����
				lpPipeInst->cbToWrite = 0;
				CompletedWriteRoutine(0, 0, (LPOVERLAPPED)lpPipeInst);
				// �ٴ���һ������ʵ��������Ӧ��һ���ͻ��˵�����
				fPendingIO = CreateAndConnectInstance(&oConnect,&hPipe,lpPipeName);
				break;
				// ��д���
			case WAIT_IO_COMPLETION:
				break;
			default:
				printf("WaitForSingleObjectEx Error(% d)\n\r\n", GetLastError());
				return 0;
		}
	}
	return 0;
}

/*
 ************************************
* VOID DisconnectAndClose(LPPIPEINST lpPipeInst)
* ����    �Ͽ�һ�����ӵ�ʵ��
* ����    lpPipeInst���Ͽ����رյ�ʵ�����
*************************************
	   */
VOID DisconnectAndClose(LPPIPEINST lpPipeInst)
{
	if (NULL == lpPipeInst) return;
	// �ر�����ʵ��
	// �ر� pipe ʵ���ľ��
	if (NULL != lpPipeInst->hPipeInst && INVALID_HANDLE_VALUE != lpPipeInst->hPipeInst) {
		DisconnectNamedPipe(lpPipeInst->hPipeInst);
		CloseHandle(lpPipeInst->hPipeInst);
	}
	// �ͷ�
	if (lpPipeInst != NULL)
		HeapFree(GetProcessHeap(), 0, lpPipeInst);
}
/*
 ************************************
* BOOL CreateAndConnectInstance(LPOVERLAPPED lpoOverlap,LPHANDLE lphPipe)
* ����    ��������ʵ��
* ����    lpoOverlap������overlapped IO�Ľṹ
* ����ֵ    �Ƿ�ɹ�
*************************************
	   */
BOOL CreateAndConnectInstance(LPOVERLAPPED lpoOverlap, LPHANDLE lphPipe,LPCTSTR lpPipeName)
{
	SECURITY_ATTRIBUTES sa;
	if (!CreateDACL(&sa)) {
		printf("Create DACL ERROR %d\r\n",GetLastError());
		return FALSE;
	}
	// ����named pipe
	*lphPipe = CreateNamedPipe(lpPipeName, // pipe ��
		PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,// �ɶ���д | overlapped ģʽ
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_ACCEPT_REMOTE_CLIENTS,// pipeģʽ  ��Ϣ���� pipe| ��Ϣ��ģʽ | ����ģʽ
		PIPE_UNLIMITED_INSTANCES, // ������ʵ��
		STR_BUFF_SIZE, // ��������С
		STR_BUFF_SIZE, // ���뻺���С
		PIPE_TIMEOUT, // �ͻ��˳�ʱ
		&sa); // Ĭ�ϰ�ȫ����
	
	if (*lphPipe == INVALID_HANDLE_VALUE) {
		printf("CreateNamedPipe failed with % d.\n\r\n", GetLastError());
		return 0;
	}
	// ���ӵ��µĿͻ���
	return ConnectToNewClient(*lphPipe, lpoOverlap);
}
/*
 ************************************
* BOOL ConnectToNewClient(HANDLE hPipe, LPOVERLAPPED lpo)
* ����    ��������ʵ��
* ����    lpoOverlap������overlapped IO�Ľṹ
* ����ֵ    �Ƿ�ɹ�
*************************************
	   */
BOOL ConnectToNewClient(HANDLE hPipe, LPOVERLAPPED lpo) {
	BOOL fConnected, fPendingIO = FALSE;
	// ��ʼһ�� overlapped ����
	fConnected = ConnectNamedPipe(hPipe, lpo);
	if (fConnected) {
		printf("ConnectNamedPipe failed with % d.\n\r\n", GetLastError());
		return 0;
	}
	switch (GetLastError()) {
		// overlapped���ӽ�����.
	case ERROR_IO_PENDING:
		fPendingIO = TRUE;
		break;
		// �Ѿ����ӣ����Eventδ��λ
	case ERROR_PIPE_CONNECTED:
		if (SetEvent(lpo->hEvent))
			break;
		// error
	default:
		printf("ConnectNamedPipe failed with % d.\n\r\n", GetLastError());
		return 0;
	}
	return fPendingIO;
}
//TODO���ݿͻ��˵����󣬸�����Ӧ
VOID GetAnswerToRequest(LPPIPEINST pipe)
{
	_tprintf(TEXT("GetAnswerToRequest [% lld] % s\n"), (INT64)pipe->hPipeInst, pipe->chRequest);
	LPTSTR pStr = pipe->chRequest, pSubStr = NULL, pSubCmd = NULL;
	BOOL bSuccess = FALSE;
	int iToWrite = 0;

	if (pSubStr = lstrstr(pStr, TEXT("Nginx"))) {
		//stop, quit, reopen, reload
		if (pSubCmd = lstrstr (pSubStr, TEXT("Restart"))) {
			bSuccess = RunExec(TEXT("Nginx"), CMD_START | CMD_STOP, NULL);
		}
		else if (pSubCmd = lstrstr(pSubStr, TEXT("Reload"))) {
			bSuccess = RunExec(TEXT("Nginx"), CMD_RELOAD, NULL);
		}
		else if (pSubCmd = lstrstr(pSubStr, TEXT("Stop"))) {
			bSuccess = RunExec(TEXT("Nginx"), CMD_STOP, NULL);
		}
		else if (pSubCmd = lstrstr(pSubStr, TEXT("Start"))) {
			bSuccess = RunExec(TEXT("Nginx"), CMD_START, NULL);
		}
		iToWrite = _stprintf_s(pipe->chReply, bSuccess==1?TEXT("%d RUN NGINX SUCCESS. %s"): TEXT("%d RUN NGINX FAILED. %s"), bSuccess, pStr);
	}
	else if (pSubStr = lstrstr(pStr, TEXT("PHP-FPM"))) {
		iToWrite = _stprintf_s(pipe->chReply, TEXT("%d PHP-FPM NOW IS NOT SUPPORT. %s"), bSuccess, pStr);
	}
	else {
		iToWrite = _stprintf_s(pipe->chReply, TEXT("%d UNKNOW COMMAND. %s"), bSuccess, pStr);
	}
	_tprintf(TEXT("iToWrite[%d] lstrlen[%d] %s\n"), iToWrite, lstrlen(pipe->chReply), pipe->chReply);
	pipe->cbToWrite = (iToWrite + 1) * sizeof(TCHAR);
}


BOOL CreateDACL(SECURITY_ATTRIBUTES* pSA)
{
	if (NULL == pSA) return FALSE;
	TCHAR* szSDA = TEXT("D:")
		TEXT("(A;;CCLCSWRPWPDTLOCRRC;;;SY)")           // default permissions for local system
		TEXT("(A;;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;BA)")   // default permissions for administrators
		TEXT("(A;;CCLCSWLOCRRC;;;AU)")                 // default permissions for authenticated users
		TEXT("(A;;CCLCSWRPWPDTLOCRRC;;;PU)")           // default permissions for power users
		TEXT("(A;;RP;;;IU)")                           // added permission: start service for interactive users
		TEXT("(A;;GA;;;WD)");                           // added permission: start service for interactive users
	TCHAR* szSD = TEXT("D:")       // Discretionary ACL
		TEXT("(D;OICI;GA;;BG)")     // Deny access to built-in guests  ��ֹguest�û�
		TEXT("(D;OICI;GA;;;AN)")     // Deny access to anonymous logon ��˼�ǽ�ֹδ����Ȩ���û�
		TEXT("(A;OICI;GRGWGX;;;AU)") // Allow read/write/execute to authenticated users ��֤�û�׼��
		TEXT("(A;OICI;GA;;;BA)");    // Allow full control to administrators

	return ConvertStringSecurityDescriptorToSecurityDescriptor(//��������������ַ���ת��Ϊ��ȫ������
		szSDA,
		SDDL_REVISION_1,
		&(pSA->lpSecurityDescriptor),
		NULL);
}
