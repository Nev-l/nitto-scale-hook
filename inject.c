#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static DWORD find_pid(void)
{
    const char *keywords[] = { "nitto", "legend", "1320" };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32 pe; pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32First(snap, &pe)) {
        do {
            char name[MAX_PATH];
            strncpy(name, pe.szExeFile, MAX_PATH - 1);
            name[MAX_PATH - 1] = '\0';
            CharLowerA(name);
            for (int i = 0; i < 3; i++)
                if (strstr(name, keywords[i])) { pid = pe.th32ProcessID; break; }
        } while (!pid && Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

int main(int argc, char *argv[])
{
    /* Build path to scale_hook.dll next to this exe */
    char dll_path[MAX_PATH];
    GetModuleFileNameA(NULL, dll_path, MAX_PATH);
    char *slash = strrchr(dll_path, '\\');
    if (slash) strcpy(slash + 1, "scale_hook.dll");
    else       strcpy(dll_path, "scale_hook.dll");

    if (GetFileAttributesA(dll_path) == INVALID_FILE_ATTRIBUTES) {
        fprintf(stderr, "scale_hook.dll not found next to inject.exe\n");
        return 1;
    }

    DWORD pid = (argc > 1) ? (DWORD)atoi(argv[1]) : find_pid();
    if (!pid) { fprintf(stderr, "Game process not found — is the game running?\n"); return 1; }

    printf("PID: %lu\n", pid);
    printf("DLL: %s\n", dll_path);

    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProc) { fprintf(stderr, "OpenProcess failed (%lu)\n", GetLastError()); return 1; }

    SIZE_T len  = strlen(dll_path) + 1;
    LPVOID addr = VirtualAllocEx(hProc, NULL, len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!addr) { CloseHandle(hProc); fprintf(stderr, "VirtualAllocEx failed\n"); return 1; }

    WriteProcessMemory(hProc, addr, dll_path, len, NULL);

    LPVOID load_lib = (LPVOID)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
    HANDLE hThread  = CreateRemoteThread(hProc, NULL, 0,
                          (LPTHREAD_START_ROUTINE)load_lib, addr, 0, NULL);
    if (!hThread) {
        VirtualFreeEx(hProc, addr, 0, MEM_RELEASE);
        CloseHandle(hProc);
        fprintf(stderr, "CreateRemoteThread failed (%lu)\n", GetLastError());
        return 1;
    }

    WaitForSingleObject(hThread, 5000);
    DWORD exit_code = 0;
    GetExitCodeThread(hThread, &exit_code);
    CloseHandle(hThread);
    VirtualFreeEx(hProc, addr, 0, MEM_RELEASE);
    CloseHandle(hProc);

    if (!exit_code) { fprintf(stderr, "Injection failed (LoadLibraryA returned NULL)\n"); return 1; }
    printf("Injected OK (handle: 0x%08lX)\n", exit_code);
    return 0;
}
