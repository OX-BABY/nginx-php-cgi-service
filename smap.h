#pragma once
#ifndef __SMAP
#define __SMAP 1
typedef struct SMAP {
	LPCTSTR key;
	LPVOID value;
	SMAP * next;
} SMAP, * LPSMAP;

BOOL initSmap(LPSMAP* dst);
BOOL existsSmap(LPSMAP map, LPSMAP child);
BOOL appendSmap(LPSMAP* map, LPSMAP child);
BOOL appendSmap(LPSMAP* map,LPCTSTR name ,VOID *child);
LPSMAP findSmap(LPSMAP map, LPCTSTR name);
int deleteSmap(LPSMAP map, LPCTSTR name);
BOOL deleteSmap(LPSMAP map, LPSMAP map2);
VOID destorySmap(LPSMAP* map);
#endif // !_SMAP

