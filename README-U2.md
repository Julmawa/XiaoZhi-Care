# XiaoZhi Care Universal U2 - archivos para reemplazar

Este paquete fue armado a partir de los DP que subiste:

- DP-016-r5.1 Stable Restore Point
- DP-017A r6 Perfil + Preferencias + Logo Corazon Azul
- DP-017B r2 Clima Argentina Open-Meteo Build Fix
- DP-017B r3 Weather MCP Minimal Compile Test, dejado como fallback en extras
- DP-018 r1 Audios por Recordatorio en Mantenimiento
- DP-018 r2 Reproduccion Automatica de Recordatorios

## Que incluye como reemplazo principal

```text
components/care-web
components/care-voice
components/care-mcp
components/care-daily/CMakeLists.txt
main/main.cc
```

## Decisiones de seguridad para U2 universal

1. No se toca `main/boards`.
2. No se toca `config.h`.
3. No se cambian GPIOs.
4. No se cambia pantalla, audio, touch ni LEDs.
5. No se cambia tabla de particiones.
6. `care-daily` queda sin dependencia obligatoria de `led_strip`.
7. `care-voice` puede compilar aunque la placa no tenga la particion `voice`; si no existe, la funcion de audios queda limitada/no disponible en esa configuracion.

## Como aplicar

Desde PowerShell:

```powershell
cd <carpeta donde descomprimiste este paquete>
.\scripts\windows\Aplicar-U2.ps1 -ProjectPath "C:\xiaozhi-esp32-128"
cd C:\xiaozhi-esp32-128
idf.py build
```

## Importante sobre DP-018-r2

El paquete incluye el componente `care-voice` con `ReminderVoiceRuntime`.

Para que la reproduccion automatica use el sistema real de alertas de XiaoZhi hace falta integrar el hook de `main/application.cc` y `main/application.h`.

Por seguridad, esos archivos NO deben reemplazarse a ciegas sobre XiaoZhi 2.5.0. Los deje en:

```text
extras/DP-018-r2-opcional-NO_REEMPLAZAR_A_CIEGAS/
```

Primero conviene compilar U2 con los componentes y el panel. Luego hacemos el merge del hook de reproduccion automatica en un paso separado.

## Clima

El reemplazo principal de `care-mcp` usa DP-017B-r2, que conserva Open-Meteo real.

DP-017B-r3 era una prueba minima de compilacion sin red real; queda en `extras` como fallback si Open-Meteo diera problemas de build.

## Validacion esperada

En monitor deberian aparecer logs similares a:

```text
CARE_CORE: XiaoZhi Care ready
CARE_ALARM_RT: Alarm runtime started
CARE_WEB: XiaoZhi Care web panel started on port 8080
CARE_MCP: Registered ... XiaoZhi Care MCP tools
```

Y el panel web deberia mostrar las pestanas con Perfil/Preferencias, Personas, Cuidados, Pastillero, Recordatorios y Mantenimiento/Audios.
