@echo off
setlocal
chcp 65001 >nul 2>&1
cd /d "%~dp0"
title XiaoZhi Care

where powershell.exe >nul 2>&1
if errorlevel 1 (
  echo.
  echo ERROR: Windows PowerShell is required.
  echo ERROR: Se requiere Windows PowerShell.
  echo ERRO: Windows PowerShell e necessario.
  echo.
  pause
  exit /b 1
)

set "UI_CULTURE="
for /f "usebackq delims=" %%I in (`powershell.exe -NoLogo -NoProfile -Command "try {(Get-UICulture).Name} catch {'en-US'}"`) do set "UI_CULTURE=%%I"
if not defined UI_CULTURE set "UI_CULTURE=en-US"

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0installer.ps1" -DetectedCulture "%UI_CULTURE%"
set "RC=%ERRORLEVEL%"

echo.
if not "%RC%"=="0" echo Process finished with code / Proceso finalizado con codigo / Processo finalizado com codigo: %RC%
pause
exit /b %RC%
