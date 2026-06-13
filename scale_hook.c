#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <string.h>
#include <stdint.h>

typedef BOOL (WINAPI *PFN_BitBlt)(HDC,int,int,int,int,HDC,int,int,DWORD);
typedef BOOL (WINAPI *PFN_StretchBlt)(HDC,int,int,int,int,HDC,int,int,int,int,DWORD);
typedef BOOL (WINAPI *PFN_SetWindowPos)(HWND,HWND,int,int,int,int,UINT);
typedef BOOL (WINAPI *PFN_MoveWindow)(HWND,int,int,int,int,BOOL);
typedef BOOL (WINAPI *PFN_ScreenToClient)(HWND,LPPOINT);

static PFN_BitBlt         g_BitBlt         = NULL;
static PFN_StretchBlt     g_StretchBlt     = NULL;
static PFN_SetWindowPos   g_SetWindowPos   = NULL;
static PFN_MoveWindow     g_MoveWindow     = NULL;
static PFN_ScreenToClient g_ScreenToClient = NULL;
static HWND              g_hwnd         = NULL;
static int               g_ow = 0, g_oh = 0;
static WNDPROC           g_orig_proc    = NULL;

/* ── compute letterboxed destination rect (aspect-ratio preserved, centered) */
static void letterbox_rect(int cw, int ch, int *ox, int *oy, int *ow, int *oh)
{
    /* Fit g_ow x g_oh inside cw x ch, preserving aspect ratio. */
    int sw = MulDiv(ch, g_ow, g_oh);   /* width if we fit to height */
    if (sw <= cw) {
        *ow = sw; *oh = ch;
    } else {
        *ow = cw; *oh = MulDiv(cw, g_oh, g_ow);
    }
    *ox = (cw - *ow) / 2;
    *oy = (ch - *oh) / 2;
}

/* ── translate mouse coords: stretched/letterboxed client → stage space ───── */
static LPARAM scale_mouse_lp(LPARAM lp)
{
    RECT rc; GetClientRect(g_hwnd, &rc);
    int cw = rc.right, ch = rc.bottom;
    if (cw <= 0 || ch <= 0 || g_ow <= 0) return lp;
    int mx = (int)(short)LOWORD(lp);
    int my = (int)(short)HIWORD(lp);
    int ox, oy, ow, oh;
    letterbox_rect(cw, ch, &ox, &oy, &ow, &oh);
    /* Clamp to the game rect, then map to stage coords. */
    mx = MulDiv(mx - ox, g_ow, ow);
    my = MulDiv(my - oy, g_oh, oh);
    return MAKELPARAM((WORD)mx, (WORD)my);
}

/* ── subclassed wndproc ─────────────────────────────────────────────────── */
static LRESULT CALLBACK ScaleProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN: case WM_LBUTTONUP:   case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN: case WM_RBUTTONUP:   case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP:   case WM_MBUTTONDBLCLK:
        lp = scale_mouse_lp(lp);
        break;
    case WM_SIZE: case WM_SIZING:
        InvalidateRect(hwnd, NULL, TRUE);
        break;
    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect((HDC)wp, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        return 1;
    }
    }
    return CallWindowProcW(g_orig_proc, hwnd, msg, wp, lp);
}

/* ── hooked BitBlt ─────────────────────────────────────────────────────── */
static BOOL WINAPI Hook_BitBlt(HDC hDst, int x, int y, int w, int h,
                                HDC hSrc, int sx, int sy, DWORD rop)
{
    if (g_hwnd && w > 0 && h > 0) {
        /* Capture original stage size on the first sizeable blit. */
        if (g_ow == 0 && w > 64 && h > 64) { g_ow = w; g_oh = h; }
        if (g_ow > 0 && WindowFromDC(hDst) == g_hwnd) {
            RECT rc; GetClientRect(g_hwnd, &rc);
            int cw = rc.right, ch = rc.bottom;
            /* Window is stretched AND source covers at least the original stage.
               Flash renders a full-client-size buffer: movie clip at top-left
               (g_ow x g_oh) with grey border filling the rest.  Scale just
               the movie-clip region to fill the whole client area. */
            if ((cw > g_ow || ch > g_oh) && w >= g_ow && h >= g_oh) {
                int ox, oy, ow, oh;
                letterbox_rect(cw, ch, &ox, &oy, &ow, &oh);
                /* Black out letterbox bars before drawing. */
                RECT full = {0, 0, cw, ch};
                FillRect(hDst, &full, (HBRUSH)GetStockObject(BLACK_BRUSH));
                SetStretchBltMode(hDst, HALFTONE);
                SetBrushOrgEx(hDst, 0, 0, NULL);
                return g_StretchBlt(hDst, ox, oy, ow, oh, hSrc, sx, sy, g_ow, g_oh, rop);
            }
        }
    }
    return g_BitBlt(hDst, x, y, w, h, hSrc, sx, sy, rop);
}

/* ── hooked StretchBlt ─────────────────────────────────────────────────── */
static BOOL WINAPI Hook_StretchBlt(HDC hDst, int x, int y, int w, int h,
                                    HDC hSrc, int sx, int sy, int sw, int sh, DWORD rop)
{
    if (g_hwnd && sw > 0 && sh > 0) {
        if (g_ow == 0 && sw > 64 && sh > 64) { g_ow = sw; g_oh = sh; }
        if (g_ow > 0 && WindowFromDC(hDst) == g_hwnd) {
            RECT rc; GetClientRect(g_hwnd, &rc);
            int cw = rc.right, ch = rc.bottom;
            if ((cw > g_ow || ch > g_oh) && sw >= g_ow && sh >= g_oh) {
                int ox, oy, ow, oh;
                letterbox_rect(cw, ch, &ox, &oy, &ow, &oh);
                RECT full = {0, 0, cw, ch};
                FillRect(hDst, &full, (HBRUSH)GetStockObject(BLACK_BRUSH));
                SetStretchBltMode(hDst, HALFTONE);
                SetBrushOrgEx(hDst, 0, 0, NULL);
                return g_StretchBlt(hDst, ox, oy, ow, oh, hSrc, sx, sy, g_ow, g_oh, rop);
            }
        }
    }
    return g_StretchBlt(hDst, x, y, w, h, hSrc, sx, sy, sw, sh, rop);
}

/* ── hooked SetWindowPos ────────────────────────────────────────────────── */
static BOOL WINAPI Hook_SetWindowPos(HWND hwnd, HWND hwndAfter,
                                      int x, int y, int cx, int cy, UINT flags)
{
    /* Block all in-process attempts to resize the game window.
       Legitimate user resizes come from the window manager, not in-process calls. */
    if (hwnd == g_hwnd && !(flags & SWP_NOSIZE))
        flags |= SWP_NOSIZE;
    return g_SetWindowPos(hwnd, hwndAfter, x, y, cx, cy, flags);
}

/* ── hooked MoveWindow ──────────────────────────────────────────────────── */
static BOOL WINAPI Hook_MoveWindow(HWND hwnd, int x, int y,
                                    int w, int h, BOOL repaint)
{
    if (hwnd == g_hwnd && g_ow > 0) {
        RECT cur; GetWindowRect(hwnd, &cur);
        w = cur.right - cur.left;
        h = cur.bottom - cur.top;
    }
    return g_MoveWindow(hwnd, x, y, w, h, repaint);
}


/* ── hooked ScreenToClient ──────────────────────────────────────────────── */
static BOOL WINAPI Hook_ScreenToClient(HWND hwnd, LPPOINT pt)
{
    BOOL r = g_ScreenToClient(hwnd, pt);
    if (r && hwnd == g_hwnd && g_ow > 0) {
        RECT rc; GetClientRect(g_hwnd, &rc);
        int cw = rc.right, ch = rc.bottom;
        int ox, oy, ow, oh;
        letterbox_rect(cw, ch, &ox, &oy, &ow, &oh);
        pt->x = MulDiv(pt->x - ox, g_ow, ow);
        pt->y = MulDiv(pt->y - oy, g_oh, oh);
    }
    return r;
}


/* ── IAT patch ──────────────────────────────────────────────────────────── */
static void patch_iat(HMODULE hMod, const char *dll, const char *fn,
                       void *hook, void **orig_out)
{
    BYTE *base = (BYTE *)hMod;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;

    DWORD imp_rva = nt->OptionalHeader
                      .DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!imp_rva) return;
    IMAGE_IMPORT_DESCRIPTOR *imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + imp_rva);

    for (; imp->Name; imp++) {
        if (_stricmp((char *)(base + imp->Name), dll) != 0) continue;
        IMAGE_THUNK_DATA *thunk  = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        IMAGE_THUNK_DATA *othunk = (IMAGE_THUNK_DATA *)(base + imp->OriginalFirstThunk);
        for (; thunk->u1.Function; thunk++, othunk++) {
            if (IMAGE_SNAP_BY_ORDINAL(othunk->u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME *ibn =
                (IMAGE_IMPORT_BY_NAME *)(base + (DWORD)othunk->u1.AddressOfData);
            if (strcmp((char *)ibn->Name, fn) != 0) continue;
            DWORD old;
            VirtualProtect(&thunk->u1.Function, sizeof(void *), PAGE_READWRITE, &old);
            if (orig_out && !*orig_out)
                *orig_out = (void *)(uintptr_t)thunk->u1.Function;
            *(uintptr_t *)&thunk->u1.Function = (uintptr_t)hook;
            VirtualProtect(&thunk->u1.Function, sizeof(void *), old, &old);
            return;
        }
    }
}

static void patch_all(void)
{
    HMODULE mods[512]; DWORD needed = 0;
    EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed);
    int n = (int)(needed / sizeof(HMODULE));
    const char *gdi_names[]  = { "gdi32.dll",  "GDI32.dll",  "gdi32full.dll", NULL };
    const char *user_names[] = { "user32.dll", "USER32.dll", NULL };
    for (int i = 0; i < n; i++) {
        for (int j = 0; gdi_names[j]; j++) {
            patch_iat(mods[i], gdi_names[j], "BitBlt",       Hook_BitBlt,       (void**)&g_BitBlt);
            patch_iat(mods[i], gdi_names[j], "StretchBlt",   Hook_StretchBlt,   (void**)&g_StretchBlt);
        }
        for (int j = 0; user_names[j]; j++) {
            patch_iat(mods[i], user_names[j], "SetWindowPos",   Hook_SetWindowPos,   (void**)&g_SetWindowPos);
            patch_iat(mods[i], user_names[j], "MoveWindow",     Hook_MoveWindow,     (void**)&g_MoveWindow);
            patch_iat(mods[i], user_names[j], "ScreenToClient", Hook_ScreenToClient, (void**)&g_ScreenToClient);
        }
    }
    HMODULE hgdi  = GetModuleHandleA("gdi32.dll");
    HMODULE huser = GetModuleHandleA("user32.dll");
    if (!g_BitBlt)       g_BitBlt       = (PFN_BitBlt)      GetProcAddress(hgdi,  "BitBlt");
    if (!g_StretchBlt)   g_StretchBlt   = (PFN_StretchBlt)  GetProcAddress(hgdi,  "StretchBlt");
    if (!g_SetWindowPos)   g_SetWindowPos   = (PFN_SetWindowPos)   GetProcAddress(huser, "SetWindowPos");
    if (!g_MoveWindow)     g_MoveWindow     = (PFN_MoveWindow)     GetProcAddress(huser, "MoveWindow");
    if (!g_ScreenToClient) g_ScreenToClient = (PFN_ScreenToClient) GetProcAddress(huser, "ScreenToClient");
}

/* ── find main window of the current process ────────────────────────────── */
static BOOL CALLBACK FindMainWnd(HWND hwnd, LPARAM lp)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(hwnd) && GetParent(hwnd) == NULL) {
        *(HWND *)lp = hwnd;
        return FALSE; /* stop enumeration */
    }
    return TRUE;
}

/* ── DllMain ───────────────────────────────────────────────────────────── */
BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hInst);
        EnumWindows(FindMainWnd, (LPARAM)&g_hwnd);
        if (g_hwnd) {
            RECT rc; GetClientRect(g_hwnd, &rc);
            if (rc.right > 64 && rc.bottom > 64) { g_ow = rc.right; g_oh = rc.bottom; }
        }
        patch_all();
        if (g_hwnd) {
            g_orig_proc = (WNDPROC)SetWindowLongPtrW(
                g_hwnd, GWLP_WNDPROC, (LONG_PTR)ScaleProc);
            LONG s = GetWindowLongW(g_hwnd, GWL_STYLE);
            SetWindowLongW(g_hwnd, GWL_STYLE, s | WS_SIZEBOX | WS_MAXIMIZEBOX);
            SetWindowPos(g_hwnd, NULL, 0,0,0,0,
                SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED);
        }
    }
    return TRUE;
}
