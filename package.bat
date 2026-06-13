@echo off
cd /d "%~dp0"

echo Building...
call do_build.bat
if %ERRORLEVEL% neq 0 ( echo Build failed & pause & exit /b 1 )

echo Packaging release...
set OUT=nitto-scale-hook
if exist "%OUT%" rd /s /q "%OUT%"
mkdir "%OUT%"

copy scale_hook.dll     "%OUT%\"
copy inject.exe         "%OUT%\"
copy nitto-launcher.exe "%OUT%\"
copy play.bat           "%OUT%\"
copy play.ps1           "%OUT%\"
copy README.md          "%OUT%\"
copy LICENSE            "%OUT%\"

powershell -NoProfile -Command "Compress-Archive -Path '%OUT%\*' -DestinationPath '%OUT%.zip' -Force"
rd /s /q "%OUT%"

echo.
echo Release: %OUT%.zip
pause
