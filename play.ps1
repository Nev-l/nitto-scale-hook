$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$configFile = "$scriptDir\config.txt"

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public class NittoWin {
    [DllImport("user32.dll")] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int w, int ht, uint flags);
    [DllImport("user32.dll")] public static extern IntPtr MonitorFromWindow(IntPtr h, uint flags);
    [DllImport("user32.dll")] public static extern bool GetMonitorInfo(IntPtr hMon, ref MONITORINFO mi);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [StructLayout(LayoutKind.Sequential)] public struct MONITORINFO {
        public int cbSize, L, T, R, B, wL, wT, wR, wB, flags;
    }
}
"@

# ── first-run: locate the game exe ───────────────────────────────────────────
if (Test-Path $configFile) {
    $gameExe = (Get-Content $configFile -Raw).Trim()
} else {
    $gameExe = $null
}

while (-not $gameExe -or -not (Test-Path $gameExe)) {
    Write-Host ""
    Write-Host "Game client not found. Paste the full path to NittoLegendsBeta.exe:"
    Write-Host "(e.g. C:\Users\you\Desktop\NittoLegends-...\NittoLegendsBeta.exe)"
    Write-Host ""
    $gameExe = (Read-Host "Path").Trim().Trim('"')
    if (-not (Test-Path $gameExe)) {
        Write-Host "File not found, try again."
    }
}

# Save for next time
$gameExe | Set-Content $configFile -Encoding UTF8

# ── launch ───────────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "Launching Nitto Legends..."
Start-Process $gameExe

Write-Host "Waiting for game window..."
$hwnd = [IntPtr]::Zero
$exeName = [System.IO.Path]::GetFileNameWithoutExtension($gameExe)
for ($i = 0; $i -lt 30; $i++) {
    Start-Sleep -Seconds 1
    $proc = Get-Process -Name $exeName -ErrorAction SilentlyContinue |
            Where-Object { $_.MainWindowHandle -ne 0 } |
            Select-Object -First 1
    if ($proc) {
        $hwnd = $proc.MainWindowHandle
        Write-Host "Found window: '$($proc.MainWindowTitle)' (PID $($proc.Id))"
        break
    }
    Write-Host "  waiting... ($i)"
}
if ($hwnd -eq [IntPtr]::Zero) { Write-Host "Game window not found after 30s."; Read-Host; exit 1 }

Start-Sleep -Seconds 2

# ── inject ───────────────────────────────────────────────────────────────────
Write-Host "Injecting scale hook..."
& "$scriptDir\inject.exe"
Start-Sleep -Milliseconds 500

# ── capture original window size ─────────────────────────────────────────────
$wr = New-Object NittoWin+RECT
$cr = New-Object NittoWin+RECT
[NittoWin]::GetWindowRect($hwnd, [ref]$wr) | Out-Null
[NittoWin]::GetClientRect($hwnd, [ref]$cr) | Out-Null
$origCW  = $cr.R
$origCH  = $cr.B
$borderW = ($wr.R - $wr.L) - $origCW
$borderH = ($wr.B - $wr.T) - $origCH

# ── scale menu ───────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "Original client: ${origCW} x ${origCH}"
Write-Host ""
Write-Host "  1) 1.25x  ($([int]($origCW*1.25)) x $([int]($origCH*1.25)))"
Write-Host "  2) 1.50x  ($([int]($origCW*1.50)) x $([int]($origCH*1.50)))"
Write-Host "  3) 1.75x  ($([int]($origCW*1.75)) x $([int]($origCH*1.75)))"
Write-Host "  4) 2.00x  ($([int]($origCW*2.00)) x $([int]($origCH*2.00)))"
Write-Host "  5) Free scale"
Write-Host "  6) Change client path"
Write-Host ""

$choice = Read-Host "Choice [1-6]"

if ($choice -eq "6") {
    Remove-Item $configFile -Force
    Write-Host "Path cleared. Re-run to set a new path."
    Read-Host "Press Enter to exit"
    exit 0
}

switch ($choice) {
    "1" { $scale = 1.25 }
    "2" { $scale = 1.50 }
    "3" { $scale = 1.75 }
    "4" { $scale = 2.00 }
    "5" {
        $input = Read-Host "Scale factor (e.g. 1.6)"
        $scale = [double]$input
    }
    default { Write-Host "Invalid choice, defaulting to 1.5x"; $scale = 1.5 }
}

# ── resize ───────────────────────────────────────────────────────────────────
$newCW = [int]($origCW * $scale)
$newCH = [int]($origCH * $scale)
$newW  = $newCW + $borderW
$newH  = $newCH + $borderH

# Centre window on its current monitor so the full client fits on screen
$MONITOR_DEFAULTTONEAREST = 2
$mi = New-Object NittoWin+MONITORINFO
$mi.cbSize = [System.Runtime.InteropServices.Marshal]::SizeOf($mi)
$hMon = [NittoWin]::MonitorFromWindow($hwnd, $MONITOR_DEFAULTTONEAREST)
[NittoWin]::GetMonitorInfo($hMon, [ref]$mi) | Out-Null
$workW = $mi.wR - $mi.wL
$workH = $mi.wB - $mi.wT
$posX  = $mi.wL + [int](($workW - $newW) / 2)
$posY  = $mi.wT + [int](($workH - $newH) / 2)
# Clamp so window never starts above or left of the work area
if ($posX -lt $mi.wL) { $posX = $mi.wL }
if ($posY -lt $mi.wT) { $posY = $mi.wT }

$SWP_NOZORDER = 0x0004
[NittoWin]::SetWindowPos($hwnd, [IntPtr]::Zero, $posX, $posY, $newW, $newH, $SWP_NOZORDER) | Out-Null

Write-Host ""
Write-Host "Scaled to ${scale}x  (client: ${newCW} x ${newCH})"
