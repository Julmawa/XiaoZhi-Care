# DP-045 — Instalador Windows V2 experimental

Este paquete incluye los recursos graficos completos. El usuario final no necesita
Python ni aportar archivos .bin adicionales.

Abra PREVISUALIZAR_GRAFICOS.bat para verificar el perfil predeterminado sin conectar
la ESP32: PuHuiTi 30 px, Twemoji 64 x 64, tema oscuro. El instalador real permite
elegir entre los perfiles incluidos; por ahora son oscuro y claro con esos tamanos.
Fuentes 22/26 y emojis 32 siguen pendientes de recursos verificados. No se sustituyen
silenciosamente. El selector de etapa 1 conserva preferencias, pero no habilita
combinaciones ausentes del catalogo distribuido.

Para reutilizar el JSON del selector de etapa 1, desde la carpeta del paquete:
`powershell -File .\installer.ps1 -PreviewGraphics -GraphicSelectionPath RUTA\seleccion_grafica_dp045.json`
La preferencia se resuelve contra los paquetes incluidos; ningun campo del JSON
puede autorizar grabacion ni omitir la verificacion de voz e integridad.

Destino de esta compilacion: ESP32-S3 N16R8, bread-compact-wifi-lcd,
ST7789 240 x 240 de 7 pines. El codigo de LEDs existente conserva 7 WS2812B.
XiaoZhi base compilado: 2.5.0. El manifiesto historico de 2.5.1 no identifica este build.

Los perfiles contienen exactamente los modelos srmodels del build utilizado para
preparar el paquete. Se verifican limites de 6 MiB, hashes, referencias internas,
resolucion y vinculacion al firmware. El archivo original assets.bin del generador
no contiene voz y nunca se graba directamente.

El tema se aplica cuando el firmware carga el paquete grafico y utiliza la preferencia
display/theme existente. Un cambio manual posterior del tema dura hasta que se
vuelven a aplicar estos recursos, incluido el reinicio. Los assets anteriores sin
la clave DP-045 conservan su comportamiento. Wi-Fi y los demas datos NVS no se
borran por esta personalizacion; el instalador mantiene su respaldo completo previo.

Estado: compilado y probado sin hardware. Falta verificar fisicamente texto, emojis,
palabra de activacion, captura/reproduccion de audio, reconexion y 7 LEDs en una
placa de prueba antes de publicar. Los hashes verifican integridad y correspondencia;
no son una firma digital ni certifican por si solos el origen de un ZIP externo.

Desarrollo:
- preparar_paquete.ps1 en la raiz delega en installer/windows/preparar_paquete.ps1.
- -ValidateOnly comprueba los requisitos sin crear ZIP ni tocar hardware.
- Cada salida usa una carpeta nueva; nunca elimina paquetes anteriores.
- La preparacion requiere Python 3.11; el instalador distribuido usa PowerShell.
- Origen grafico informado por el usuario: https://xiaozhi.me/tools/assets-generator/
- PuHuiTi 30 coincide con el recurso de esp_emote_assets 0.1.2 instalado.
- Los modelos se toman del build actual, no del paquete de emojis.
- No distribuir publicamente hasta completar prueba fisica y revisar las licencias
  de los recursos del generador y las atribuciones correspondientes.
