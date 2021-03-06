#include <windows.h>
#include <stdio.h>
#include <tchar.h>

#include "helpers.h"
#include "shellapihelper.h"
#include "logging.h"
#include "service.h"

static CHAR wuauservdllA[MAX_PATH];
static WCHAR wuauservdllW[MAX_PATH];

BOOL get_svcdllA(LPCSTR lpServiceName, LPSTR lpServiceDll, DWORD dwSize) {
    CHAR lpSubKey[257];
    sprintf_s(lpSubKey, _countof(lpSubKey), "SYSTEM\\CurrentControlSet\\services\\%s\\Parameters", lpServiceName);
    DWORD cb = dwSize;
    if (RegGetValueA(HKEY_LOCAL_MACHINE, lpSubKey, "ServiceDll", RRF_RT_REG_SZ, NULL, lpServiceDll, &cb))
        return FALSE;
    
    trace(L"Service \"%S\" DLL path: %S", lpServiceName, lpServiceDll);
    return TRUE;
}

BOOL get_svcdllW(LPCWSTR lpServiceName, LPWSTR lpServiceDll, DWORD dwSize) {
    WCHAR lpSubKey[257];
    swprintf_s(lpSubKey, _countof(lpSubKey), L"SYSTEM\\CurrentControlSet\\services\\%s\\Parameters", lpServiceName);
    DWORD cb = dwSize;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, lpSubKey, L"ServiceDll", RRF_RT_REG_SZ, NULL, lpServiceDll, &cb))
        return FALSE;

    trace(L"Service \"%s\" DLL path: %s", lpServiceName, lpServiceDll);
    return TRUE;
}

LPSTR get_wuauservdllA(void) {
    if (!wuauservdllA[0])
        get_svcdllA("wuauserv", wuauservdllA, _countof(wuauservdllA));
    
    return wuauservdllA;
}

LPWSTR get_wuauservdllW(void) {
    if (!wuauservdllW[0])
        get_svcdllW(L"wuauserv", wuauservdllW, _countof(wuauservdllW));
    
    return wuauservdllW;
}

BOOL get_svcpid(SC_HANDLE hSCManager, LPCTSTR lpServiceName, DWORD *lpdwProcessId) {
    SC_HANDLE hService = OpenService(hSCManager, lpServiceName, SERVICE_QUERY_STATUS);
    if (!hService)
        return FALSE;

    SERVICE_STATUS_PROCESS lpBuffer;
    DWORD cbBytesNeeded;
    BOOL result = FALSE;
    if (QueryServiceStatusEx(hService, SC_STATUS_PROCESS_INFO, (LPBYTE)&lpBuffer, sizeof(lpBuffer), &cbBytesNeeded)
        && lpBuffer.dwProcessId) {

        *lpdwProcessId = lpBuffer.dwProcessId;
#ifdef _UNICODE
        trace(L"Service \"%s\" process ID: %d", lpServiceName, *lpdwProcessId);
#else
        trace(L"Service \"%S\" process ID: %d", lpServiceName, *lpdwProcessId);
#endif
        result = TRUE;
    }
    CloseServiceHandle(hService);
    return result;
}

BOOL get_svcgname(SC_HANDLE hSCManager, LPCTSTR lpServiceName, LPTSTR lpGroupName, SIZE_T dwSize) {
    TCHAR lpBinaryPathName[0x8000];
    if (!get_svcpath(hSCManager, lpServiceName, lpBinaryPathName, _countof(lpBinaryPathName)))
        return FALSE;
    
    int numArgs;
    LPWSTR *argv = CommandLineToArgv(lpBinaryPathName, &numArgs);
    if (numArgs < 3)
        return FALSE;

    TCHAR fname[_MAX_FNAME];
    _tsplitpath_s(argv[0], NULL, 0, NULL, 0, fname, _countof(fname), NULL, 0);

    BOOL result = FALSE;
    if (!_tcsicmp(fname, _T("svchost"))) {
        LPWSTR *p = argv;
        for (int i = 1; i < numArgs; i++) {
            if (!_tcsicmp(*(p++), _T("-k")) && !_tcscpy_s(lpGroupName, dwSize, *p)) {
                result = TRUE;
#ifdef _UNICODE
                trace(L"Service \"%s\" group name: %s", lpServiceName, lpGroupName);
#else
                trace(L"Service \"%S\" group name: %S", lpServiceName, lpGroupName);
#endif
                break;
            }
        }
    }
    return result;
}

BOOL get_svcpath(SC_HANDLE hSCManager, LPCTSTR lpServiceName, LPTSTR lpBinaryPathName, SIZE_T dwSize) {
    HANDLE hService = OpenService(hSCManager, lpServiceName, SERVICE_QUERY_CONFIG);
    if (!hService)
        return FALSE;
    
    DWORD cbBytesNeeded;
    BOOL result = FALSE;
    if (!QueryServiceConfig(hService, NULL, 0, &cbBytesNeeded) && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        LPQUERY_SERVICE_CONFIG sc = malloc(cbBytesNeeded);
        if (QueryServiceConfig(hService, sc, cbBytesNeeded, &cbBytesNeeded) && !_tcscpy_s(lpBinaryPathName, dwSize, sc->lpBinaryPathName))
            result = TRUE;
        free(sc);
    }
    CloseServiceHandle(hService);
    return result;
}

BOOL get_svcgpid(SC_HANDLE hSCManager, LPTSTR lpServiceGroupName, DWORD *lpdwProcessId) {
    DWORD uBytes = 1 << 20;
    LPBYTE pvData = malloc(uBytes);
    RegGetValue(HKEY_LOCAL_MACHINE, _T("SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Svchost"),
        lpServiceGroupName, RRF_RT_REG_MULTI_SZ, NULL, pvData, &uBytes);

    BOOL result = FALSE;
    for (LPTSTR p = (LPTSTR)pvData; *p; p += _tcslen(p) + 1) {
        DWORD dwProcessId;
        TCHAR group[256];
        if (get_svcpid(hSCManager, p, &dwProcessId)
            && (get_svcgname(hSCManager, p, group, _countof(group)) && !_tcsicmp(group, lpServiceGroupName))) {

            *lpdwProcessId = dwProcessId;
            result = TRUE;
#ifdef _UNICODE
            trace(L"Service group \"%s\" process ID: %d", lpServiceGroupName, *lpdwProcessId);
#else
            trace(L"Service group \"%S\" process ID: %d", lpServiceGroupName, *lpdwProcessId);
#endif
            break;
        }
    }
    free(pvData);
    return result;
}
