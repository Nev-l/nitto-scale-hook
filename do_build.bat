@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" > nul 2>&1
cd /d C:\tmp\scale_hook

echo Building scale_hook.dll...
cl /nologo /LD /O2 /W3 /Fe:scale_hook.dll scale_hook.c /link user32.lib gdi32.lib psapi.lib /MACHINE:X86
if %ERRORLEVEL% neq 0 ( echo scale_hook.dll FAILED & exit /b 1 )

echo Building inject.exe...
cl /nologo /O2 /W3 /Fe:inject.exe inject.c /link user32.lib /MACHINE:X86
if %ERRORLEVEL% neq 0 ( echo inject.exe FAILED & exit /b 1 )

echo Building nitto-launcher.exe...
rc /nologo launcher.rc
if %ERRORLEVEL% neq 0 ( echo rc FAILED & exit /b 1 )
cl /nologo /O2 /W3 /Fe:nitto-launcher.exe launcher.c launcher.res /link user32.lib kernel32.lib comdlg32.lib /SUBSYSTEM:WINDOWS /MACHINE:X86
if %ERRORLEVEL% neq 0 ( echo nitto-launcher.exe FAILED & exit /b 1 )

echo.
echo Build OK
