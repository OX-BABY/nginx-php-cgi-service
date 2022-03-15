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
*     写入pipe操作的完成函数
*    接口参见FileIOCompletionRoutine回调函数定义**    当写操作完成时被调用，开始读另外一个客户端的请求
*************************************
	   */
VOID WINAPI CompletedWriteRoutine(DWORD dwErr, DWORD cbWritten, LPOVERLAPPED lpOverLap) {
	printf("CompletedWriteRoutine dwErr %d,cbWritten %d,lpOverLap %0llx\r\n", dwErr, cbWritten, (INT64)lpOverLap);
	LPPIPEINST lpPipeInst = (LPPIPEINST)lpOverLap;
	BOOL fRead = FALSE;
	// 保存overlap实例
	// 如果没有错误
	if ((dwErr == 0) && (cbWritten == lpPipeInst->cbToWrite)) {
		fRead = ReadFileEx(lpPipeInst->hPipeInst,
			lpPipeInst->chRequest,
			STR_BUFF_SIZE,
			(LPOVERLAPPED)lpPipeInst,
			// 写读操作完成后，调用CompletedReadRoutine
			(LPOVERLAPPED_COMPLETION_ROUTINE)CompletedReadRoutine);
	}
	if (!fRead) {
		// 出错，断开连接
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
*     读取pipe操作的完成函数
*    接口参见FileIOCompletionRoutine回调函数定义**    当读操作完成时被调用，写入回复
*************************************
*/
VOID WINAPI CompletedReadRoutine(DWORD dwErr, DWORD cbBytesRead, LPOVERLAPPED lpOverLap)
{
	// 保存overlap实例
	LPPIPEINST lpPipeInst = (LPPIPEINST)lpOverLap;
	printf("CompletedReadRoutine dwErr %d,cbBytesRead %d,lstrlen %d,lpOverLap %0llx\r\n", dwErr, cbBytesRead,lstrlen(lpPipeInst->chRequest), (INT64)lpOverLap);
	
	BOOL fWrite = FALSE;
	// 如果没有错误
	if ((dwErr == 0) && (cbBytesRead != 0)) {
		memset(lpPipeInst->chRequest + cbBytesRead / sizeof(TCHAR), 0, sizeof(TCHAR));
		// 根据客户端的请求，生成回复
		GetAnswerToRequest(lpPipeInst);
		// 将回复写入到pipe
		fWrite = WriteFileEx(lpPipeInst->hPipeInst,
			lpPipeInst->chReply,
			// 将响应写入pipe
			lpPipeInst->cbToWrite,
			(LPOVERLAPPED)lpPipeInst,
			// 写入完成后，调用CompletedWriteRoutine
			(LPOVERLAPPED_COMPLETION_ROUTINE)CompletedWriteRoutine);
		//ZeroMemory(lpPipeInst->chRequest, cbBytesRead);
	}
	if (!fWrite) // 出错，断开连接
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
* 功能    pipe 通信服务端主函数
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
	// 用于连接操作的事件对象
	hConnectEvent = CreateEvent(NULL,		// 默认属性
		TRUE, // 手工reset
		TRUE, // 初始状态 signaled
		NULL); // 未命名
	if (hConnectEvent == NULL||hConnectEvent == INVALID_HANDLE_VALUE)
	{
		printf("CreateEvent failed with % d.\n\r\n", GetLastError());
		return 0;
	}
	// OVERLAPPED 事件
	oConnect.hEvent = hConnectEvent;
	// 创建连接实例，等待连接
	fPendingIO = CreateAndConnectInstance(&oConnect,&hPipe, lpPipeName);
	while (1) {
		// 等待客户端连接或读写操作完成
		printf("等待客户端连接或读写操作完成\r\n");
		dwWait = WaitForSingleObjectEx(hConnectEvent, // 等待的事件
			INFINITE, // 无限等待
			TRUE);
		printf("dwWait Result %d\r\n", dwWait);
		switch (dwWait) {
			case 0:
				// pending
				if (fPendingIO)
				{
					// 获取 Overlapped I / O 的结果
					fSuccess = GetOverlappedResult(hPipe,
						&oConnect,// pipe 句柄 OVERLAPPED 结构
						&cbRet,// 已经传送的数据量
						FALSE);// 不等待

					if (!fSuccess) {
						printf("ConnectNamedPipe(% d)\n\r\n", GetLastError());
						return 0;
					}
				}
				// 分配内存
				lpPipeInst = (LPPIPEINST)HeapAlloc(GetProcessHeap(), 0, sizeof(PIPEINST));
				if (lpPipeInst == NULL) {
					printf("GlobalAlloc failed(% d)\n\r\n", GetLastError());
					return 0;
				}
				lpPipeInst->hPipeInst = hPipe;
				// 读和写，注意CompletedWriteRoutine和CompletedReadRoutine的相互调用
				lpPipeInst->cbToWrite = 0;
				CompletedWriteRoutine(0, 0, (LPOVERLAPPED)lpPipeInst);
				// 再创建一个连接实例，以响应下一个客户端的连接
				fPendingIO = CreateAndConnectInstance(&oConnect,&hPipe,lpPipeName);
				break;
				// 读写完成
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
* 功能    断开一个连接的实例
* 参数    lpPipeInst，断开并关闭的实例句柄
*************************************
	   */
VOID DisconnectAndClose(LPPIPEINST lpPipeInst)
{
	if (NULL == lpPipeInst) return;
	// 关闭连接实例
	// 关闭 pipe 实例的句柄
	if (NULL != lpPipeInst->hPipeInst && INVALID_HANDLE_VALUE != lpPipeInst->hPipeInst) {
		DisconnectNamedPipe(lpPipeInst->hPipeInst);
		CloseHandle(lpPipeInst->hPipeInst);
	}
	// 释放
	if (lpPipeInst != NULL)
		HeapFree(GetProcessHeap(), 0, lpPipeInst);
}
/*
 ************************************
* BOOL CreateAndConnectInstance(LPOVERLAPPED lpoOverlap,LPHANDLE lphPipe)
* 功能    建立连接实例
* 参数    lpoOverlap，用于overlapped IO的结构
* 返回值    是否成功
*************************************
	   */
BOOL CreateAndConnectInstance(LPOVERLAPPED lpoOverlap, LPHANDLE lphPipe,LPCTSTR lpPipeName)
{
	SECURITY_ATTRIBUTES sa;
	if (!CreateDACL(&sa)) {
		printf("Create DACL ERROR %d\r\n",GetLastError());
		return FALSE;
	}
	// 创建named pipe
	*lphPipe = CreateNamedPipe(lpPipeName, // pipe 名
		PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,// 可读可写 | overlapped 模式
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_ACCEPT_REMOTE_CLIENTS,// pipe模式  消息类型 pipe| 消息读模式 | 阻塞模式
		PIPE_UNLIMITED_INSTANCES, // 无限制实例
		STR_BUFF_SIZE, // 输出缓存大小
		STR_BUFF_SIZE, // 输入缓存大小
		PIPE_TIMEOUT, // 客户端超时
		&sa); // 默认安全属性
	
	if (*lphPipe == INVALID_HANDLE_VALUE) {
		printf("CreateNamedPipe failed with % d.\n\r\n", GetLastError());
		return 0;
	}
	// 连接到新的客户端
	return ConnectToNewClient(*lphPipe, lpoOverlap);
}
/*
 ************************************
* BOOL ConnectToNewClient(HANDLE hPipe, LPOVERLAPPED lpo)
* 功能    建立连接实例
* 参数    lpoOverlap，用于overlapped IO的结构
* 返回值    是否成功
*************************************
	   */
BOOL ConnectToNewClient(HANDLE hPipe, LPOVERLAPPED lpo) {
	BOOL fConnected, fPendingIO = FALSE;
	// 开始一个 overlapped 连接
	fConnected = ConnectNamedPipe(hPipe, lpo);
	if (fConnected) {
		printf("ConnectNamedPipe failed with % d.\n\r\n", GetLastError());
		return 0;
	}
	switch (GetLastError()) {
		// overlapped连接进行中.
	case ERROR_IO_PENDING:
		fPendingIO = TRUE;
		break;
		// 已经连接，因此Event未置位
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
//TODO根据客户端的请求，给出响应
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
		TEXT("(D;OICI;GA;;BG)")     // Deny access to built-in guests  禁止guest用户
		TEXT("(D;OICI;GA;;;AN)")     // Deny access to anonymous logon 意思是禁止未经授权的用户
		TEXT("(A;OICI;GRGWGX;;;AU)") // Allow read/write/execute to authenticated users 认证用户准许
		TEXT("(A;OICI;GA;;;BA)");    // Allow full control to administrators

	return ConvertStringSecurityDescriptorToSecurityDescriptor(//吧上面的描述符字符串转换为安全描述符
		szSDA,
		SDDL_REVISION_1,
		&(pSA->lpSecurityDescriptor),
		NULL);
}
