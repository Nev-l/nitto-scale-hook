@echo off
echo Building scale_hook.dll (32-bit)...
cl /nologo /LD /O2 /W3 /Fe:scale_hook.dll scale_hook.c /link user32.lib gdi32.lib psapi.lib /MACHINE:X86
if %ERRORLEVEL% == 0 (
    echo.
    echo Build OK -- scale_hook.dll ready
) else (
    echo.
    echo Build FAILED
)
