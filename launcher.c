#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <tlhelp32.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "resource.h"

#define WM_SET_STATUS  (WM_APP + 1)   /* lParam = heap-alloc'd char*, caller posts, dlg frees */
#define WM_LAUNCH_DONE (WM_APP + 2)   /* wParam = 1 success, 0 fail */

static HWND g_dlg      = NULL;
static char g_dll_path[MAX_PATH];
static char g_game_exe[MAX_PATH];

/* ── status line (thread-safe) ───────────────────────────────────────────── */
static void set_status(const char *fmt, ...)
{
    char *buf = (char *)malloc(512);
    if (!buf) return;
    va_list va; va_start(va, fmt);
    _vsnprintf(buf, 511, fmt, va);
    buf[511] = '\0';
    va_end(va);
    PostMessageA(g_dlg, WM_SET_STATUS, 0, (LPARAM)buf);
}

/* ── config ──────────────────────────────────────────────────────────────── */
static void cfg_path(char *out, DWORD sz)
{
    char dir[MAX_PATH];
    ExpandEnvironmentStringsA("%APPDATA%\\nitto-scale-hook", dir, MAX_PATH);
    CreateDirectoryA(dir, NULL);
    _snprintf(out, sz, "%s\\config.txt", dir);
}

static int load_config(char *out, DWORD sz)
{
    char cfg[MAX_PATH]; cfg_path(cfg, MAX_PATH);
    HANDLE f = CreateFileA(cfg, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD n = 0; ReadFile(f, out, sz - 1, &n, NULL); CloseHandle(f);
    out[n] = '\0';
    while (n > 0 && (out[n-1]=='\r'||out[n-1]=='\n'||out[n-1]==' ')) out[--n] = '\0';
    return n > 0 && GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES;
}

static void save_config(const char *path)
{
    char cfg[MAX_PATH]; cfg_path(cfg, MAX_PATH);
    HANDLE f = CreateFileA(cfg, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD n; WriteFile(f, path, (DWORD)strlen(path), &n, NULL); CloseHandle(f);
}

/* ── DLL extraction ──────────────────────────────────────────────────────── */
static int extract_dll(void)
{
    HRSRC hRes = FindResourceA(NULL, MAKEINTRESOURCEA(IDR_SCALE_HOOK_DLL), RT_RCDATA);
    if (!hRes) return 0;
    HGLOBAL hG  = LoadResource(NULL, hRes);
    LPVOID  data = LockResource(hG);
    DWORD   dsz  = SizeofResource(NULL, hRes);

    char tmp[MAX_PATH]; GetTempPathA(MAX_PATH, tmp);
    char dir[MAX_PATH]; _snprintf(dir, MAX_PATH, "%snsh", tmp);
    CreateDirectoryA(dir, NULL);
    _snprintf(g_dll_path, MAX_PATH, "%s\\scale_hook.dll", dir);

    HANDLE f = CreateFileA(g_dll_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD w; WriteFile(f, data, dsz, &w, NULL); CloseHandle(f);
    return w == dsz;
}

/* ── process / window / inject ───────────────────────────────────────────── */
typedef struct { DWORD pid; HWND hwnd; } FindWnd;
static BOOL CALLBACK enum_cb(HWND h, LPARAM lp)
{
    FindWnd *d = (FindWnd *)lp; DWORD wpid = 0;
    GetWindowThreadProcessId(h, &wpid);
    if (wpid == d->pid && IsWindowVisible(h) && !GetParent(h)) {
        char t[64]; GetWindowTextA(h, t, 64);
        if (*t) { d->hwnd = h; return FALSE; }
    }
    return TRUE;
}
static HWND find_hwnd(DWORD pid) { FindWnd d={pid,NULL}; EnumWindows(enum_cb,(LPARAM)&d); return d.hwnd; }

static int inject(DWORD pid)
{
    HANDLE hp = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hp) return 0;
    SIZE_T len  = strlen(g_dll_path) + 1;
    LPVOID addr = VirtualAllocEx(hp, NULL, len, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
    if (!addr) { CloseHandle(hp); return 0; }
    WriteProcessMemory(hp, addr, g_dll_path, len, NULL);
    LPVOID ll = (LPVOID)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
    HANDLE ht = CreateRemoteThread(hp, NULL, 0, (LPTHREAD_START_ROUTINE)ll, addr, 0, NULL);
    if (!ht) { VirtualFreeEx(hp,addr,0,MEM_RELEASE); CloseHandle(hp); return 0; }
    WaitForSingleObject(ht, 5000);
    DWORD ec = 0; GetExitCodeThread(ht, &ec);
    CloseHandle(ht); VirtualFreeEx(hp, addr, 0, MEM_RELEASE); CloseHandle(hp);
    return ec != 0;
}

static void resize_window(HWND hwnd, double scale)
{
    RECT wr, cr; GetWindowRect(hwnd, &wr); GetClientRect(hwnd, &cr);
    int bw = (wr.right-wr.left)-cr.right, bh = (wr.bottom-wr.top)-cr.bottom;
    int nw = (int)(cr.right*scale)+bw,    nh = (int)(cr.bottom*scale)+bh;
    HMONITOR hm = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi; mi.cbSize = sizeof(mi); GetMonitorInfoA(hm, &mi);
    int px = mi.rcWork.left + (mi.rcWork.right -mi.rcWork.left-nw)/2;
    int py = mi.rcWork.top  + (mi.rcWork.bottom-mi.rcWork.top -nh)/2;
    if (px < mi.rcWork.left) px = mi.rcWork.left;
    if (py < mi.rcWork.top)  py = mi.rcWork.top;
    SetWindowPos(hwnd, NULL, px, py, nw, nh, SWP_NOZORDER);
}

/* ── worker thread ───────────────────────────────────────────────────────── */
typedef struct { char exe[MAX_PATH]; double scale; } LaunchParams;

static DWORD WINAPI worker(LPVOID arg)
{
    LaunchParams *p = (LaunchParams *)arg;

    STARTUPINFOA si = {0}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {0};
    if (!CreateProcessA(p->exe, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        set_status("Failed to launch game (error %lu).", GetLastError());
        free(p); PostMessageA(g_dlg, WM_LAUNCH_DONE, 0, 0); return 0;
    }
    CloseHandle(pi.hThread);
    DWORD pid = pi.dwProcessId;

    set_status("Waiting for game window...");
    HWND hwnd = NULL;
    for (int i = 0; i < 30 && !hwnd; i++) { Sleep(1000); hwnd = find_hwnd(pid); }
    CloseHandle(pi.hProcess);

    if (!hwnd) {
        set_status("Game window not found after 30s.");
        free(p); PostMessageA(g_dlg, WM_LAUNCH_DONE, 0, 0); return 0;
    }

    Sleep(2000);
    set_status("Injecting scale hook...");
    if (!inject(pid)) {
        set_status("Injection failed.");
        free(p); PostMessageA(g_dlg, WM_LAUNCH_DONE, 0, 0); return 0;
    }
    Sleep(500);

    set_status("Scaling to %.2fx...", p->scale);
    resize_window(hwnd, p->scale);
    set_status("Done! Scaled to %.2fx — enjoy.", p->scale);

    free(p); PostMessageA(g_dlg, WM_LAUNCH_DONE, 1, 0); return 0;
}

/* ── dialog proc ─────────────────────────────────────────────────────────── */
static void sync_custom(HWND dlg)
{
    EnableWindow(GetDlgItem(dlg, IDC_CUSTOM_SCALE),
        IsDlgButtonChecked(dlg, IDC_RADIO_CUSTOM) == BST_CHECKED);
}

static INT_PTR CALLBACK DlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG:
        g_dlg = dlg;
        CheckDlgButton(dlg, IDC_RADIO_150, BST_CHECKED);
        SetDlgItemTextA(dlg, IDC_CUSTOM_SCALE, "1.5");
        EnableWindow(GetDlgItem(dlg, IDC_CUSTOM_SCALE), FALSE);
        if (load_config(g_game_exe, MAX_PATH))
            SetDlgItemTextA(dlg, IDC_GAME_PATH, g_game_exe);
        else
            SetDlgItemTextA(dlg, IDC_STATUS, "Browse to NittoLegendsBeta.exe to get started.");
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wp)) {

        case IDC_BROWSE: {
            OPENFILENAMEA ofn = {0};
            char path[MAX_PATH] = {0};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner   = dlg;
            ofn.lpstrFilter = "Executable\0*.exe\0All Files\0*.*\0";
            ofn.lpstrFile   = path;
            ofn.nMaxFile    = MAX_PATH;
            ofn.lpstrTitle  = "Select NittoLegendsBeta.exe";
            ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameA(&ofn)) {
                strcpy(g_game_exe, path);
                save_config(path);
                SetDlgItemTextA(dlg, IDC_GAME_PATH, path);
                SetDlgItemTextA(dlg, IDC_STATUS, "Ready.");
            }
            break;
        }

        case IDC_RADIO_125: case IDC_RADIO_150: case IDC_RADIO_175:
        case IDC_RADIO_200: case IDC_RADIO_CUSTOM:
            sync_custom(dlg);
            break;

        case IDC_LAUNCH: {
            if (!*g_game_exe || GetFileAttributesA(g_game_exe) == INVALID_FILE_ATTRIBUTES) {
                MessageBoxA(dlg, "Select NittoLegendsBeta.exe first.", "No game path", MB_ICONWARNING);
                break;
            }
            double scale = 1.5;
            if      (IsDlgButtonChecked(dlg, IDC_RADIO_125)    == BST_CHECKED) scale = 1.25;
            else if (IsDlgButtonChecked(dlg, IDC_RADIO_150)    == BST_CHECKED) scale = 1.50;
            else if (IsDlgButtonChecked(dlg, IDC_RADIO_175)    == BST_CHECKED) scale = 1.75;
            else if (IsDlgButtonChecked(dlg, IDC_RADIO_200)    == BST_CHECKED) scale = 2.00;
            else if (IsDlgButtonChecked(dlg, IDC_RADIO_CUSTOM) == BST_CHECKED) {
                char buf[32]; GetDlgItemTextA(dlg, IDC_CUSTOM_SCALE, buf, 32);
                scale = atof(buf);
                if (scale < 0.5 || scale > 10.0) {
                    MessageBoxA(dlg, "Enter a scale between 0.5 and 10.", "Invalid scale", MB_ICONWARNING);
                    break;
                }
            }
            EnableWindow(GetDlgItem(dlg, IDC_LAUNCH), FALSE);
            EnableWindow(GetDlgItem(dlg, IDC_BROWSE), FALSE);
            SetDlgItemTextA(dlg, IDC_STATUS, "Launching game...");

            LaunchParams *params = (LaunchParams *)malloc(sizeof(LaunchParams));
            strcpy(params->exe, g_game_exe);
            params->scale = scale;
            HANDLE ht = CreateThread(NULL, 0, worker, params, 0, NULL);
            if (ht) CloseHandle(ht);
            break;
        }

        case IDCANCEL:
            EndDialog(dlg, 0);
            break;
        }
        return TRUE;

    case WM_SET_STATUS: {
        char *s = (char *)lp;
        SetDlgItemTextA(dlg, IDC_STATUS, s);
        free(s);
        return TRUE;
    }

    case WM_LAUNCH_DONE:
        EnableWindow(GetDlgItem(dlg, IDC_LAUNCH), TRUE);
        EnableWindow(GetDlgItem(dlg, IDC_BROWSE), TRUE);
        return TRUE;
    }
    return FALSE;
}

/* ── entry point ─────────────────────────────────────────────────────────── */
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
    (void)hPrev; (void)lpCmd; (void)nShow;
    if (!extract_dll()) {
        MessageBoxA(NULL, "Failed to extract embedded scale_hook.dll.", "Error", MB_ICONERROR);
        return 1;
    }
    DialogBoxA(hInst, MAKEINTRESOURCEA(IDD_MAIN), NULL, DlgProc);
    return 0;
}
