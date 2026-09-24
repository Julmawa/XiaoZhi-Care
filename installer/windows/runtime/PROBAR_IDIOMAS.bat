@echo off
setlocal
chcp 65001 >nul 2>&1
cd /d "%~dp0"
set "UI_CULTURE="
for /f "usebackq delims=" %%I in (`powershell.exe -NoLogo -NoProfile -Command "try {(Get-UICulture).Name} catch {'en-US'}"`) do set "UI_CULTURE=%%I"
if not defined UI_CULTURE set "UI_CULTURE=en-US"
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0PROBAR_IDIOMAS.ps1" -DetectedCulture "%UI_CULTURE%"
echo.
pause
