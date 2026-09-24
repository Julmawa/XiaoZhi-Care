@echo off
setlocal
chcp 65001 >nul 2>&1
cd /d "%~dp0"
title XiaoZhi Care - Prueba visual de progreso
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0PROBAR_BARRA.ps1"
echo.
pause
