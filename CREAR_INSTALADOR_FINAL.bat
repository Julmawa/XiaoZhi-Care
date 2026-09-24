@echo off
setlocal
chcp 65001 >nul 2>&1
cd /d "%~dp0"
title XiaoZhi Care - Crear instalador final

where powershell.exe >nul 2>&1
if errorlevel 1 (
  echo ERROR: No se encontro Windows PowerShell.
  pause
  exit /b 1
)

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0preparar_paquete.ps1"
set "RC=%ERRORLEVEL%"
echo.
if not "%RC%"=="0" echo El preparador termino con codigo %RC%.
pause
exit /b %RC%
