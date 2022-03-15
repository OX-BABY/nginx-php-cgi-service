#pragma once
#ifndef __CGI_SPAWNER
#define __CGI_SPAWNER 1
typedef struct _CGI_PROFILE
{
	char* cgiStartCmd;
	char* cgiWorkPath;
	int cgiMinChildren;
	int cgiMaxChildren;
	int cgiPort;
	int cgiRestartDelay;

} CGI_PROFILE, * LPCGI_PROFILE;
//void RunSpawner(const char* cmd, int iLocalPort, int iStartServers, int iMaxChildren, int iRestartDelay);
DWORD  RunSpawnerProfile(void*);

void StopSpawner();
#endif // !__CGI_SPAWNER

