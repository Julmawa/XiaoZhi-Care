@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0installer.ps1" -PreviewGraphics
set "RC=%ERRORLEVEL%"
echo.
pause
exit /b %RC%
