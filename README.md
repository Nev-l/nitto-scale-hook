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

The launcher injects `scale_hook.dll` into the running game process. The DLL patches the game's import table to intercept Windows API calls:

| Hooked API | What it does |
|---|---|
| `BitBlt` / `StretchBlt` | Scales the Flash framebuffer edge-to-edge using `HALFTONE` quality |
| `SetWindowPos` / `MoveWindow` | Prevents Flash from snapping the window back to its original size |
| `ScreenToClient` | Maps cursor positions from scaled window space back to stage space |
| `PatBlt` / `FillRect` | Suppresses the grey border Flash paints around the stage |

Flash adapts its render buffer to the new window size and centres the original 800×600 stage within it. The hook samples from that centre region and stretches it to fill the full window, then applies the inverse offset to all mouse coordinates so every click lands exactly where it should.

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
