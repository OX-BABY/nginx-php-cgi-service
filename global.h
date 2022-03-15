#pragma once
#ifndef _global
#define STR_BUFF_SIZE_A 1024
#define STR_BUFF_SIZE STR_BUFF_SIZE_A*sizeof(TCHAR)
#define _global 1

#ifdef UNICODE
#define lstrchr  wcschr
#define lstrrchr  wcsrchr
#define lstrstr  wcsstr
#define latoi  _wtoi
#else
#define lstrstr  strstr
#define lstrrchr  strrchr
#define lstrchr  strchr
#define latoi  atoi
#endif // !UNICODE

#endif