# Nitto Legends Scale Hook

Lets you resize the Nitto Legends game window to **1.25×, 1.5×, 1.75×, 2×, or any custom scale** while keeping full mouse accuracy. Works by intercepting the game's GDI rendering calls and scaling them to fill the window.

## Download & Run (no build required)

1. Go to the [Releases](../../releases) page and download the latest `nitto-scale-hook.zip`
2. Extract anywhere
3. Double-click **`play.bat`**
4. On first run, paste the path to your `NittoLegendsBeta.exe` when prompted
5. Pick a scale — done

No Python, no Visual Studio, no admin rights required.

## How it works

The hook DLL is injected into the game process at startup. It patches the game's import table to intercept three groups of Windows API calls:

- **`BitBlt` / `StretchBlt`** — the game renders its Flash stage at a fixed resolution; the hook scales that framebuffer to fill the current window size using `HALFTONE` quality
- **`SetWindowPos` / `MoveWindow`** — prevents the Flash runtime from snapping the window back to the original size after every frame
- **`ScreenToClient`** — translates cursor coordinates from stretched window space back to the original stage space so all clicks land correctly

## Build from source

Requires [Visual Studio Build Tools](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022) with the **MSVC v143 x86 toolset**.

```
do_build.bat
```

Produces `scale_hook.dll` and `inject.exe`.

## Files

| File | Purpose |
|---|---|
| `scale_hook.c` | Hook DLL source — GDI / window / cursor hooks |
| `inject.c` | Injector source — loads the DLL into the game process |
| `play.ps1` | Launcher — starts game, injects, resizes to chosen scale |
| `play.bat` | Entry point — double-click to run |
| `do_build.bat` | Build script for developers |

## License

MIT — see [LICENSE](LICENSE)
