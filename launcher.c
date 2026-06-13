#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define IDR_SCALE_HOOK_DLL 101

/* ── config ──────────────────────────────────────────────────────────────── */
static void config_dir(char *out, DWORD sz)
{
    ExpandEnvironmentStringsA("%APPDATA%\\nitto-scale-hook", out, sz);
}

static void config_path(char *out, DWORD sz)
{
    char dir[MAX_PATH];
    config_dir(dir, MAX_PATH);
    _snprintf(out, sz, "%s\\config.txt", dir);
}

static int load_config(char *exe_path, DWORD sz)
{
    char cfg[MAX_PATH];
    config_path(cfg, MAX_PATH);
    HANDLE f = CreateFileA(cfg, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD n = 0;
    ReadFile(f, exe_path, sz - 1, &n, NULL);
    CloseHandle(f);
    exe_path[n] = '\0';
    while (n > 0 && (exe_path[n-1] == '\r' || exe_path[n-1] == '\n' || exe_path[n-1] == ' '))
        exe_path[--n] = '\0';
    return n > 0 && GetFileAttributesA(exe_path) != INVALID_FILE_ATTRIBUTES;
}

static void save_config(const char *exe_path)
{
    char dir[MAX_PATH], cfg[MAX_PATH];
    config_dir(dir, MAX_PATH);
    CreateDirectoryA(dir, NULL);
    config_path(cfg, MAX_PATH);
    HANDLE f = CreateFileA(cfg, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD n;
    WriteFile(f, exe_path, (DWORD)strlen(exe_path), &n, NULL);
    CloseHandle(f);
}

static void clear_config(void)
{
    char cfg[MAX_PATH];
    config_path(cfg, MAX_PATH);
    DeleteFileA(cfg);
}

/* ── resource extraction ─────────────────────────────────────────────────── */
static int extract_dll(char *dll_path_out, DWORD sz)
{
    HRSRC   hRes  = FindResourceA(NULL, MAKEINTRESOURCEA(IDR_SCALE_HOOK_DLL), RT_RCDATA);
    if (!hRes) { fprintf(stderr, "Embedded DLL resource not found\n"); return 0; }
    HGLOBAL hGlob = LoadResource(NULL, hRes);
    if (!hGlob) return 0;
    LPVOID  data  = LockResource(hGlob);
    DWORD   dsz   = SizeofResource(NULL, hRes);

    char tmp[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp);

    char dir[MAX_PATH];
    _snprintf(dir, MAX_PATH, "%snsh", tmp);
    CreateDirectoryA(dir, NULL);

    _snprintf(dll_path_out, sz, "%s\\scale_hook.dll", dir);

    HANDLE f = CreateFileA(dll_path_out, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Cannot write DLL to temp (%s)\n", dll_path_out);
        return 0;
    }
    DWORD written;
    WriteFile(f, data, dsz, &written, NULL);
    CloseHandle(f);
    return written == dsz;
}

/* ── process / window ────────────────────────────────────────────────────── */
static DWORD find_pid(void)
{
    const char *kw[] = { "nitto", "legend", "1320" };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32 pe; pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32First(snap, &pe)) do {
        char name[MAX_PATH];
        strncpy(name, pe.szExeFile, MAX_PATH - 1);
        name[MAX_PATH - 1] = '\0';
        CharLowerA(name);
        for (int i = 0; i < 3; i++)
            if (strstr(name, kw[i])) { pid = pe.th32ProcessID; break; }
    } while (!pid && Process32Next(snap, &pe));
    CloseHandle(snap);
    return pid;
}

typedef struct { DWORD pid; HWND hwnd; } FindWnd;
static BOOL CALLBACK enum_cb(HWND h, LPARAM lp)
{
    FindWnd *d = (FindWnd *)lp;
    DWORD wpid = 0;
    GetWindowThreadProcessId(h, &wpid);
    if (wpid == d->pid && IsWindowVisible(h) && GetParent(h) == NULL) {
        char t[64];
        GetWindowTextA(h, t, 64);
        if (strlen(t) > 0) { d->hwnd = h; return FALSE; }
    }
    return TRUE;
}

static HWND find_window_for_pid(DWORD pid)
{
    FindWnd d = { pid, NULL };
    EnumWindows(enum_cb, (LPARAM)&d);
    return d.hwnd;
}

/* ── injection ───────────────────────────────────────────────────────────── */
static int inject(DWORD pid, const char *dll_path)
{
    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProc) { fprintf(stderr, "OpenProcess failed (%lu)\n", GetLastError()); return 0; }

    SIZE_T len  = strlen(dll_path) + 1;
    LPVOID addr = VirtualAllocEx(hProc, NULL, len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!addr) { CloseHandle(hProc); return 0; }

    WriteProcessMemory(hProc, addr, dll_path, len, NULL);

    LPVOID ll = (LPVOID)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
    HANDLE ht = CreateRemoteThread(hProc, NULL, 0, (LPTHREAD_START_ROUTINE)ll, addr, 0, NULL);
    if (!ht) {
        VirtualFreeEx(hProc, addr, 0, MEM_RELEASE);
        CloseHandle(hProc);
        fprintf(stderr, "CreateRemoteThread failed (%lu)\n", GetLastError());
        return 0;
    }

    WaitForSingleObject(ht, 5000);
    DWORD ec = 0;
    GetExitCodeThread(ht, &ec);
    CloseHandle(ht);
    VirtualFreeEx(hProc, addr, 0, MEM_RELEASE);
    CloseHandle(hProc);
    return ec != 0;
}

/* ── resize ──────────────────────────────────────────────────────────────── */
static void resize_window(HWND hwnd, double scale)
{
    RECT wr, cr;
    GetWindowRect(hwnd, &wr);
    GetClientRect(hwnd, &cr);
    int origCW  = cr.right;
    int origCH  = cr.bottom;
    int borderW = (wr.right  - wr.left) - origCW;
    int borderH = (wr.bottom - wr.top)  - origCH;
    int newCW   = (int)(origCW * scale);
    int newCH   = (int)(origCH * scale);
    int newW    = newCW + borderW;
    int newH    = newCH + borderH;

    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    GetMonitorInfoA(hMon, &mi);
    int workW = mi.rcWork.right  - mi.rcWork.left;
    int workH = mi.rcWork.bottom - mi.rcWork.top;
    int posX  = mi.rcWork.left + (workW - newW) / 2;
    int posY  = mi.rcWork.top  + (workH - newH) / 2;
    if (posX < mi.rcWork.left) posX = mi.rcWork.left;
    if (posY < mi.rcWork.top)  posY = mi.rcWork.top;

    SetWindowPos(hwnd, NULL, posX, posY, newW, newH, SWP_NOZORDER);
    printf("Scaled to %.2fx  (client: %d x %d)\n", scale, newCW, newCH);
}

/* ── main ────────────────────────────────────────────────────────────────── */
int main(void)
{
    SetConsoleTitleA("Nitto Legends Scale Hook");

    /* Extract embedded DLL */
    char dll_path[MAX_PATH];
    if (!extract_dll(dll_path, MAX_PATH)) {
        printf("Press Enter to exit..."); getchar();
        return 1;
    }

    /* Load or prompt for game exe */
    char game_exe[MAX_PATH] = {0};

    while (!load_config(game_exe, MAX_PATH)) {
        printf("\nGame client not found. Paste the full path to NittoLegendsBeta.exe:\n");
        printf("(e.g. C:\\Users\\you\\Desktop\\NittoLegends\\NittoLegendsBeta.exe)\n\n> ");
        fflush(stdout);
        fgets(game_exe, MAX_PATH, stdin);

        /* trim */
        int n = (int)strlen(game_exe);
        while (n > 0 && (game_exe[n-1] == '\r' || game_exe[n-1] == '\n' || game_exe[n-1] == ' '))
            game_exe[--n] = '\0';
        /* strip quotes */
        if (n > 0 && game_exe[0] == '"') { memmove(game_exe, game_exe + 1, n); n--; }
        if (n > 0 && game_exe[n-1] == '"') game_exe[--n] = '\0';

        if (GetFileAttributesA(game_exe) == INVALID_FILE_ATTRIBUTES) {
            printf("File not found, try again.\n");
            game_exe[0] = '\0';
        } else {
            save_config(game_exe);
        }
    }

    /* Launch game */
    printf("\nLaunching Nitto Legends...\n");
    STARTUPINFOA si = {0};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {0};
    if (!CreateProcessA(game_exe, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "Failed to launch game (%lu)\n", GetLastError());
        printf("Press Enter..."); getchar();
        return 1;
    }
    CloseHandle(pi.hThread);
    DWORD game_pid = pi.dwProcessId;

    /* Wait for window */
    printf("Waiting for game window");
    fflush(stdout);
    HWND hwnd = NULL;
    for (int i = 0; i < 30; i++) {
        Sleep(1000);
        printf(".");
        fflush(stdout);
        hwnd = find_window_for_pid(game_pid);
        if (hwnd) break;
    }
    printf("\n");
    CloseHandle(pi.hProcess);

    if (!hwnd) {
        fprintf(stderr, "Game window not found after 30s.\n");
        printf("Press Enter..."); getchar();
        return 1;
    }

    char title[256];
    GetWindowTextA(hwnd, title, 256);
    printf("Found window: '%s'\n", title);

    Sleep(2000);

    /* Inject */
    printf("Injecting scale hook...\n");
    if (!inject(game_pid, dll_path))
        fprintf(stderr, "Injection failed.\n");
    else
        printf("Injected OK.\n");
    Sleep(500);

    /* Scale menu */
    RECT cr;
    GetClientRect(hwnd, &cr);
    int ow = cr.right, oh = cr.bottom;
    char buf[32];

    for (;;) {
        printf("\nOriginal client: %d x %d\n\n", ow, oh);
        printf("  1) 1.25x  (%d x %d)\n", (int)(ow * 1.25), (int)(oh * 1.25));
        printf("  2) 1.50x  (%d x %d)\n", (int)(ow * 1.50), (int)(oh * 1.50));
        printf("  3) 1.75x  (%d x %d)\n", (int)(ow * 1.75), (int)(oh * 1.75));
        printf("  4) 2.00x  (%d x %d)\n", (int)(ow * 2.00), (int)(oh * 2.00));
        printf("  5) Free scale\n");
        printf("  6) Change game path\n\n");
        printf("Choice [1-6]: ");
        fflush(stdout);
        fgets(buf, sizeof(buf), stdin);
        int choice = atoi(buf);

        double scale = 0.0;
        switch (choice) {
            case 1: scale = 1.25; break;
            case 2: scale = 1.50; break;
            case 3: scale = 1.75; break;
            case 4: scale = 2.00; break;
            case 5:
                printf("Scale factor (e.g. 1.6): ");
                fflush(stdout);
                fgets(buf, sizeof(buf), stdin);
                scale = atof(buf);
                break;
            case 6:
                clear_config();
                printf("Path cleared. Re-run to set a new path.\n");
                printf("Press Enter to exit..."); getchar();
                return 0;
            default:
                printf("Invalid choice.\n");
                continue;
        }

        if (scale > 0.5 && scale < 10.0) {
            resize_window(hwnd, scale);
            break;
        }
        printf("Invalid scale value.\n");
    }

    printf("\nDone. You can close this window.\n");
    printf("Press Enter to exit...");
    getchar();
    return 0;
}
