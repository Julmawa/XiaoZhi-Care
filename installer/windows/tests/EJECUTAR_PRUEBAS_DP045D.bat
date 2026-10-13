@echo off
setlocal
chcp 65001 >nul 2>&1
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0PRUEBAS_DP045D.ps1"
set "RC=%ERRORLEVEL%"
echo.
pause
exit /b %RC%
