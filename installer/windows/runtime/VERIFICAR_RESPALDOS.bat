@echo off
setlocal
chcp 65001 >nul 2>&1
cd /d "%~dp0"
title XiaoZhi Care - Verificar respaldos (solo lectura)
where powershell.exe >nul 2>&1
if errorlevel 1 (
  echo ERROR: Se requiere Windows PowerShell.
  pause
  exit /b 1
)
set "UI_CULTURE="
for /f "usebackq delims=" %%I in (`powershell.exe -NoLogo -NoProfile -Command "try {(Get-UICulture).Name} catch {'en-US'}"`) do set "UI_CULTURE=%%I"
if not defined UI_CULTURE set "UI_CULTURE=en-US"
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0restaurar.ps1" -Mode Verify -DetectedCulture "%UI_CULTURE%"
set "RC=%ERRORLEVEL%"
echo.
if not "%RC%"=="0" echo Verificacion terminada con error / Verification error: %RC%
pause
exit /b %RC%
