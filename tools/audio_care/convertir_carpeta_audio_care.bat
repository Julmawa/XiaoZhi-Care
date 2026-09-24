@echo off
setlocal enabledelayedexpansion

set "BASE_DIR=%~dp0"
set "ENTRADA=%BASE_DIR%entrada"
set "SALIDA=%BASE_DIR%convertidos"
set "ERROR_DIR=%BASE_DIR%error"
set "FFMPEG=%BASE_DIR%ffmpeg.exe"
set "FFPROBE=%BASE_DIR%ffprobe.exe"
set "REPORTE=%BASE_DIR%reporte.txt"

if not exist "%ENTRADA%" mkdir "%ENTRADA%"
if not exist "%SALIDA%" mkdir "%SALIDA%"
if not exist "%ERROR_DIR%" mkdir "%ERROR_DIR%"

echo XiaoZhi Care - Conversor de audios > "%REPORTE%"
echo Fecha: %date% %time% >> "%REPORTE%"
echo. >> "%REPORTE%"

if not exist "%FFMPEG%" (
  echo ERROR: No se encontro ffmpeg.exe en esta carpeta.
  echo Copia ffmpeg.exe junto a este BAT.
  pause
  exit /b 1
)

echo.
echo ================================================
echo  XiaoZhi Care - Conversor de audios
echo ================================================
echo.
echo Carpeta de entrada:
echo %ENTRADA%
echo.
echo Carpeta de salida:
echo %SALIDA%
echo.
echo Copia tus audios en la carpeta ENTRADA.
echo El sistema generara archivos .ogg listos en CONVERTIDOS.
echo.

set /a TOTAL=0
set /a OK=0
set /a FAIL=0

for %%F in ("%ENTRADA%\*.opus" "%ENTRADA%\*.ogg" "%ENTRADA%\*.mp3" "%ENTRADA%\*.wav" "%ENTRADA%\*.m4a" "%ENTRADA%\*.aac" "%ENTRADA%\*.webm") do (
  if exist "%%~fF" (
    set /a TOTAL+=1

    set "NAME=%%~nF"
    set "OUT=%SALIDA%\!NAME!.ogg"

    echo -----------------------------------------------
    echo Convirtiendo: %%~nxF
    echo Salida: !NAME!.ogg
    echo.

    "%FFMPEG%" -y -i "%%~fF" -vn -ac 1 -ar 48000 -c:a libopus -application voip -b:a 16k -vbr on -compression_level 10 "!OUT!" >nul 2>nul

    if errorlevel 1 (
      set /a FAIL+=1
      echo ERROR: No se pudo convertir %%~nxF
      echo ERROR: %%~nxF >> "%REPORTE%"
      copy "%%~fF" "%ERROR_DIR%\%%~nxF" >nul
    ) else (
      for %%S in ("!OUT!") do set "SIZE=%%~zS"

      if !SIZE! GTR 32768 (
        echo Archivo mayor a 32 KB. Probando version mas liviana en 12k...

        "%FFMPEG%" -y -i "%%~fF" -vn -ac 1 -ar 48000 -c:a libopus -application voip -b:a 12k -vbr on -compression_level 10 "!OUT!" >nul 2>nul

        if errorlevel 1 (
          set /a FAIL+=1
          echo ERROR: No se pudo convertir %%~nxF en 12k
          echo ERROR_12K: %%~nxF >> "%REPORTE%"
          copy "%%~fF" "%ERROR_DIR%\%%~nxF" >nul
        ) else (
          for %%S in ("!OUT!") do set "SIZE=%%~zS"

          if !SIZE! GTR 32768 (
            echo ERROR: !NAME!.ogg sigue superando 32 KB.
            echo ERROR_SIZE: %%~nxF -- mayor a 32KB size=!SIZE! >> "%REPORTE%"
            set /a FAIL+=1
            copy "%%~fF" "%ERROR_DIR%\%%~nxF" >nul
          ) else (
            set /a OK+=1
            echo OK: !NAME!.ogg generado en 12k - !SIZE! bytes
            echo OK_12K: %%~nxF --^> !NAME!.ogg size=!SIZE! >> "%REPORTE%"
          )
        )
      ) else (
        set /a OK+=1
        echo OK: !NAME!.ogg - !SIZE! bytes
        echo OK: %%~nxF --^> !NAME!.ogg size=!SIZE! >> "%REPORTE%"
      )
    )
  )
)

echo.
echo ================================================
echo  Conversion finalizada
echo ================================================
echo.
echo Archivos encontrados : %TOTAL%
echo Convertidos OK      : %OK%
echo Con error           : %FAIL%
echo.
echo Ver reporte:
echo %REPORTE%
echo.
echo Los audios listos estan en:
echo %SALIDA%
echo.

echo. >> "%REPORTE%"
echo Resumen: >> "%REPORTE%"
echo Archivos encontrados: %TOTAL% >> "%REPORTE%"
echo Convertidos OK: %OK% >> "%REPORTE%"
echo Con error: %FAIL% >> "%REPORTE%"

pause
exit /b 0
