@echo off
setlocal
chcp 65001 >nul 2>&1
cd /d "%~dp0"
where py >nul 2>&1
if errorlevel 1 (
 echo Se necesita Python para esta demostracion de DP-045.
 pause
 exit /b 1
)
py -3 seleccionar_graficos_dp045.py
pause
