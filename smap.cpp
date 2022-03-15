#include<windows.h>
#include "smap.h"

BOOL initSmap(LPSMAP* dst) {
	*dst = (LPSMAP)LocalAlloc(LMEM_ZEROINIT, sizeof(SMAP));
	return(*dst != NULL);
}
BOOL existsSmap(LPSMAP map, LPSMAP child) {
	while (NULL != map && map->next != map) {
		if (map == child) return TRUE;
		map = map->next;
	}
	return FALSE;
}
BOOL appendSmap(LPSMAP *map, LPSMAP child) {
	if (map == NULL)return FALSE;
	LPSMAP lpSmap = *map;
	if (lpSmap == NULL) {
		*map = child;
		return TRUE;
	}

	while (NULL != lpSmap && lpSmap->next != lpSmap && child != lpSmap) {
		if (lpSmap == child) return TRUE;
		if (lpSmap->next == NULL) {
			lpSmap->next = child;
			return TRUE;
		}
		lpSmap = lpSmap->next;
	}
	return FALSE;
}
BOOL appendSmap(LPSMAP* map, LPCTSTR name, VOID* child) {

	LPSMAP smap = (LPSMAP)LocalAlloc(LMEM_ZEROINIT, sizeof(SMAP));
	if (NULL == smap) return false;
	LPTSTR lName = (LPTSTR)LocalAlloc(LMEM_ZEROINIT, lstrlen(name) * sizeof(TCHAR) + sizeof(TCHAR));
	if (NULL == lName) {
		LocalFree(smap);
		return FALSE;
	}
	lstrcpy(lName, name);
	smap->key = lName;
	smap->value = child;
	if (!appendSmap(map, smap)) {
		LocalFree(lName);
		LocalFree(smap); return FALSE;
	}
	return TRUE;
}
LPSMAP findSmap(LPSMAP map, LPCTSTR name) {
	while (map != NULL && map->next != map)
	{
		if (!lstrcmp(map->key, name)) {
			return map;
		}
		map = map->next;
	}
	return NULL;
}
int deleteSmap(LPSMAP map, LPCTSTR name) {
	LPSMAP map_prev = map;
	while (map != NULL && map->next != map) {
		if (!lstrcmp(map->key, name)) {
			map_prev->next = map->next;
			LocalFree((HLOCAL)map->key);
			LocalFree(map->value);
			GlobalFree(map);
			return TRUE;
		}
		map_prev = map;
		map = map->next;
	}return FALSE;
}
BOOL deleteSmap(LPSMAP map, LPSMAP map2) {
	if (map2 == NULL)return TRUE;
	while (map != NULL && map->next != map) {
		if (map->next == map2) {
			map->next = map2->next;
			LocalFree((HLOCAL)map2->key);
			LocalFree(map2->value);
			GlobalFree(map2);
			return TRUE;
		}
		map = map->next;
	}return FALSE;
}
VOID destorySmap(LPSMAP* map) {
	if (NULL == map) return;
	LPSMAP smap = *map, smapT = NULL;
	while (smap!=NULL)
	{
		smapT = smap->next;
		LocalFree(smap);
		smap = smapT;
	}
	*map = NULL;
}