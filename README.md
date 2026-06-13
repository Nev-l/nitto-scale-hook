# Nitto Legends Scale Hook

Lets you resize the Nitto Legends game window to **1.25×, 1.5×, 1.75×, 2×, or any custom scale** while keeping full mouse accuracy. Works by intercepting the game's GDI rendering calls and scaling them to fill the window.

## Download & Run (no build required)

1. Go to the [Releases](../../releases) page and download the latest `nitto-scale-hook.zip`
2. Extract anywhere

**Option A — single exe (easiest):**
Double-click **`nitto-launcher.exe`** — it embeds the hook DLL and handles everything.

**Option B — PowerShell launcher:**
Double-click **`play.bat`** — same flow, requires PowerShell 5+.

On first run either launcher will ask for the path to your `NittoLegendsBeta.exe` and remember it. Pick a scale — done.

No Python, no Visual Studio, no admin rights required.

## How it works

The hook DLL is injected into the game process at startup. It patches the game's import table to intercept three groups of Windows API calls:

- **`BitBlt` / `StretchBlt`** — the game renders its Flash stage at a fixed resolution; the hook scales that framebuffer to fill the current window size using `HALFTONE` quality
- **`SetWindowPos` / `MoveWindow`** — prevents the Flash runtime from snapping the window back to the original size after every frame
- **`ScreenToClient`** — translates cursor coordinates from stretched window space back to the original stage space so all clicks land correctly
- **`PatBlt` / `FillRect`** — suppresses the Flash runtime's grey border fills so the scaled content reaches every edge

`nitto-launcher.exe` has `scale_hook.dll` embedded as a Windows resource. On launch it extracts it to `%TEMP%\nsh\` before injection, so no extra files need to be in the same folder.

## Build from source

Requires [Visual Studio Build Tools](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022) with the **MSVC v143 x86 toolset**.

```
do_build.bat
```

Produces `scale_hook.dll`, `inject.exe`, and `nitto-launcher.exe`.

## Files

| File | Purpose |
|---|---|
| `scale_hook.c` | Hook DLL source — GDI / window / cursor hooks |
| `inject.c` | Injector source — loads the DLL into the game process |
| `launcher.c` | Standalone launcher source — embeds DLL, no extra files needed |
| `launcher.rc` | Resource script — embeds scale_hook.dll into the launcher |
| `play.ps1` | PowerShell launcher — starts game, injects, resizes |
| `play.bat` | Entry point for play.ps1 — double-click to run |
| `do_build.bat` | Build script for developers |

## License

MIT — see [LICENSE](LICENSE)
