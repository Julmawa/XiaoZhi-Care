@echo off
setlocal

set "TOOLS_DIR=%~dp0"
set "FFMPEG=%TOOLS_DIR%ffmpeg.exe"
set "FFPROBE=%TOOLS_DIR%ffprobe.exe"

if not exist "%FFMPEG%" (
  where ffmpeg >nul 2>nul
  if errorlevel 1 (
    echo.
    echo ERROR: No se encontro FFmpeg.
    echo.
    echo Solucion simple:
    echo   1) Copia ffmpeg.exe dentro de esta carpeta:
    echo      %TOOLS_DIR%
    echo.
    echo   2) Opcional: copia tambien ffprobe.exe para ver la verificacion tecnica.
    echo.
    pause
    exit /b 1
  )
  set "FFMPEG=ffmpeg"
)

if not exist "%FFPROBE%" (
  where ffprobe >nul 2>nul
  if errorlevel 1 (
    set "FFPROBE="
  ) else (
    set "FFPROBE=ffprobe"
  )
)

if "%~1"=="" (
  echo.
  echo Uso:
  echo   convertir_audio_care.bat entrada.opus salida.ogg
  echo.
  echo Ejemplos:
  echo   convertir_audio_care.bat audio_whatsapp.opus casillero_1.ogg
  echo   convertir_audio_care.bat mensaje.mp3 recordatorio.ogg
  echo.
  pause
  exit /b 1
)

if "%~2"=="" (
  echo.
  echo Falta el nombre del archivo de salida .ogg
  echo.
  echo Ejemplo:
  echo   convertir_audio_care.bat "%~1" salida.ogg
  echo.
  pause
  exit /b 1
)

echo.
echo Convirtiendo audio para XiaoZhi Care...
echo Entrada: "%~1"
echo Salida : "%~2"
echo.

"%FFMPEG%" -y -i "%~1" -vn -ac 1 -ar 48000 -c:a libopus -application voip -b:a 16k -vbr on -compression_level 10 "%~2"

if errorlevel 1 (
  echo.
  echo ERROR: FFmpeg no pudo convertir el audio.
  echo.
  pause
  exit /b 1
)

echo.
echo Archivo convertido correctamente.

if not "%FFPROBE%"=="" (
  echo.
  echo Verificando archivo generado...
  "%FFPROBE%" -v error -show_entries format=duration,size -show_entries stream=codec_name,sample_rate,channels -of default=noprint_wrappers=1 "%~2"
) else (
  echo.
  echo Nota: ffprobe.exe no esta disponible. La conversion igualmente se realizo.
)

echo.
echo Listo.
echo Ahora carga "%~2" en XiaoZhi Care.
echo.
pause
exit /b 0
