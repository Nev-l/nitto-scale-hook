# Nitto Legends Scale Hook

Scale the Nitto Legends game window to **1.25×, 1.5×, 1.75×, 2×, or any custom size** while keeping full mouse accuracy and edge-to-edge content — no grey bars, no offset clicks.

---

## Download

Go to **[Releases](../../releases)** and download `nitto-scale-hook.zip`.  
Extract it anywhere. No install required.

---

## Usage

**Double-click `nitto-launcher.exe`**

On first run, click **Browse…** and point it at your `NittoLegendsBeta.exe`. The path is remembered for next time. Pick a scale and click **Launch & Scale** — that's it.

> `play.bat` / `play.ps1` are included as a PowerShell alternative if you prefer that workflow.

---

## How it works

### 1. DLL injection via `CreateRemoteThread`

The launcher uses the standard Windows injection technique:

1. `OpenProcess` on the target PID with `PROCESS_ALL_ACCESS`
2. `VirtualAllocEx` to allocate a small buffer in the game's address space
3. `WriteProcessMemory` to write the full path of `scale_hook.dll` into that buffer
4. `CreateRemoteThread` with `LoadLibraryA` as the start routine and the buffer address as its argument

This causes the game process itself to call `LoadLibraryA("...\\scale_hook.dll")`, which runs the DLL's `DllMain` and triggers the hooks.

---

### 2. IAT patching (Import Address Table hooking)

Rather than writing detour trampolines into function code, the DLL patches the **import address table** of every loaded module in the process. The IAT is a table of function pointers that Windows fills in at load time — one entry per imported function. Overwriting an entry redirects all calls to that function through our hook.

The patch loop:
1. Enumerate all loaded modules with `EnumProcessModules`
2. For each module, walk its PE headers to find the IAT (`IMAGE_DIRECTORY_ENTRY_IMPORT`)
3. For each IAT entry that matches a target function (e.g. `BitBlt` from `gdi32.dll`), use `VirtualProtect` to make the page writable, swap in the hook function pointer, then restore the original protection

The hook stores the original function pointer so it can call through when it decides not to intercept. The DLL skips patching its own module to prevent infinite recursion on internal calls.

---

### 3. Detecting and capturing the original stage size

On the first `BitBlt` or `StretchBlt` call with dimensions larger than 64×64, the hook records `width` and `height` as `g_ow` / `g_oh` (original width/height). For Nitto Legends this is 800×600. All subsequent scaling decisions compare against these stored values.

---

### 4. Flash's render buffer behaviour after a window resize

This is the key insight that makes the hook non-trivial.

When you resize the Flash Player window, Flash **does not** re-render at the new size. Instead it:

1. Allocates a new off-screen buffer sized to the **full new window** (e.g. 1200×900)
2. Renders the original 800×600 stage **centred** inside that buffer, at offset `((newW − 800) / 2, (newH − 600) / 2)`
3. Calls `BitBlt` with the full new buffer dimensions (1200×900) as the source

If you naively `StretchBlt` from `(0, 0)` you get the grey border, not the game — the actual game pixels start at the centre offset. The hook corrects for this:

```c
int src_x = sx + (src_width  - g_ow) / 2;
int src_y = sy + (src_height - g_oh) / 2;
```

This skips the border Flash composited into the source buffer and samples only the real game region.

---

### 5. Edge-to-edge scaling with DC transform reset

Before calling `StretchBlt` to scale the content to the window, the hook resets any active GDI coordinate transforms on the destination DC — otherwise Flash's own viewport/window origin settings distort the output:

```c
SaveDC(hDst);
SetMapMode(hDst, MM_TEXT);
SetViewportOrgEx(hDst, 0, 0, NULL);
SetWindowOrgEx(hDst, 0, 0, NULL);
SetStretchBltMode(hDst, HALFTONE);  // bilinear-quality downscale
SetBrushOrgEx(hDst, 0, 0, NULL);   // required after HALFTONE
StretchBlt(hDst, 0, 0, clientW, clientH, hSrc, src_x, src_y, g_ow, g_oh, rop);
RestoreDC(hDst, saved);
```

`HALFTONE` mode produces noticeably smoother results than the default `COLORONCOLOR` when stretching pixel art / UI content.

---

### 6. Mouse coordinate correction

Flash computes stage coordinates internally as:

```
stage_x = client_x - offset_x
stage_y = client_y - offset_y
```

Where `offset_x = (clientW - 800) / 2` and `offset_y = (clientH - 600) / 2` — the same border offset it adds when compositing the render buffer.

After scaling the window, a raw client coordinate `cx` maps to a pixel that visually represents stage position `cx * 800 / clientW`. But Flash still applies its internal subtraction. So the hooks output a *pre-compensated* coordinate:

```c
corrected_x = MulDiv(cx, g_ow, clientW) + offset_x;
```

After Flash subtracts `offset_x`, the result is exactly `cx * 800 / clientW` — the correct stage position.

This correction is applied in two places:
- `Hook_ScreenToClient` — for mouse move / click events routed through `ScreenToClient`
- The window subclass `WndProc` — for `WM_MOUSEMOVE`, `WM_LBUTTONDOWN`, etc. delivered directly to the window

---

### 7. Suppressing Flash's grey border repaints

Every frame, Flash calls `PatBlt` and `FillRect` to paint the grey letterbox border around the stage before compositing the game content. After our hook has already drawn edge-to-edge scaled content, these calls would overwrite it with grey bars.

The fix: when the destination DC belongs to a window larger than the original stage size, return immediately without calling the real function:

```c
static BOOL WINAPI Hook_PatBlt(HDC hdc, int x, int y, int w, int h, DWORD rop) {
    if (dc_is_scaled(hdc, NULL, NULL)) return TRUE;  // suppress
    return g_PatBlt(hdc, x, y, w, h, rop);
}
```

`dc_is_scaled` uses `WindowFromDC` → `GetClientRect` to check the destination window size against `g_ow` / `g_oh`.

---

### 8. Preventing Flash from resizing the window

Flash periodically calls `SetWindowPos` and `MoveWindow` to enforce its preferred window dimensions. The hooks intercept both and drop any call that would shrink the window below the current client size, keeping the user's chosen scale intact.

---

### Applying this to other Flash-based games

The same approach works for any windowed Flash Player game:

- Hook `BitBlt` / `StretchBlt` in the process's IAT
- Record the first large blit as the stage size
- Apply the centre-sample + stretch pattern
- Hook `PatBlt` / `FillRect` to suppress border fills
- Correct mouse coords with the same `MulDiv + offset` formula
- Hook `SetWindowPos` / `MoveWindow` to hold the window size

The only game-specific value is the original stage resolution (800×600 here), which the hook discovers automatically from the first blit.

---

`nitto-launcher.exe` has `scale_hook.dll` embedded as a Windows resource. On launch it extracts it to `%TEMP%\nsh\` automatically — no loose files needed.

---

## Build from source

Requires [Visual Studio Build Tools 2022](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022) with the **MSVC v143 x86 toolset** and **Windows 10 SDK**.

```
do_build.bat
```

Produces `scale_hook.dll`, `inject.exe`, and `nitto-launcher.exe`.

---

## Files

| File | Purpose |
|---|---|
| `nitto-launcher.exe` | **Start here.** GUI launcher — embeds the DLL, no extra files needed |
| `scale_hook.c` | Hook DLL — GDI / window / cursor intercepts |
| `inject.c` | Injector — loads the DLL into the game via `CreateRemoteThread` |
| `launcher.c` | GUI launcher source |
| `launcher.rc` | Resource script — embeds DLL + icon + manifest |
| `resource.h` | Shared resource IDs |
| `play.ps1` | PowerShell launcher (alternative to the exe) |
| `play.bat` | Double-click entry point for `play.ps1` |
| `do_build.bat` | Build script |

---

## License

MIT — see [LICENSE](LICENSE)
