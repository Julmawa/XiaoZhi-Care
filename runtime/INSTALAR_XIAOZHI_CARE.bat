@echo off
setlocal
chcp 65001 >nul 2>&1
cd /d "%~dp0"
title XiaoZhi Care - Instalador

where powershell.exe >nul 2>&1
if errorlevel 1 (
  echo.
  echo ERROR: Este instalador necesita Windows PowerShell.
  echo No se encontro powershell.exe en este equipo.
  echo.
  pause
  exit /b 1
)

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0installer.ps1"
set "RC=%ERRORLEVEL%"

echo.
if not "%RC%"=="0" echo El instalador termino con codigo %RC%.
pause
exit /b %RC%
