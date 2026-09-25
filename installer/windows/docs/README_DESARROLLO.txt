XIAOZHI CARE - CREACION DE LA RELEASE INSTALABLE
=================================================

Este archivo es para quien prepara la release, no para el usuario final.

OBJETIVO
Tomar el build final ya validado de <RAIZ_DEL_REPOSITORIO>\build\default
y generar un ZIP que el usuario pueda instalar con doble clic, sin ESP-IDF,
Python, Visual Studio ni VS Code.

USO
1. Verificar primero en hardware que <RAIZ_DEL_REPOSITORIO> sea la version estable.
2. Ejecutar CREAR_INSTALADOR_FINAL.bat.
3. El preparador valida:
   - xiaozhi.bin / proyecto y version;
   - perfil exacto bread-compact-wifi;
   - tamanos de app y assets;
   - tabla Care con NVS/OTA sin mover;
   - presencia de bootloader, OTA data y assets.
4. La salida queda en SALIDA\ y se crea tambien un ZIP para distribuir.
5. Probar ese ZIP en una ESP32-S3 N16R8 de prueba antes de publicarlo.

FIRMWARE QUE SE COPIA
- bootloader\bootloader.bin -> firmware\bootloader.bin
- partition_table\partition-table.bin -> firmware\partition-table.bin
- ota_data_initial.bin -> firmware\ota_data_initial.bin
- xiaozhi.bin -> firmware\xiaozhi.bin
- generated_assets.bin -> firmware\generated_assets.bin

IMPORTANTE
El usuario final no recibe este preparador. Recibe solamente el ZIP generado.

LIMITACION IMPORTANTE DE V1.3
Esta release NO conserva de forma automatica cualquier cableado personalizado que
haya sido compilado manualmente bajo el mismo nombre de SKU. El binario Care lleva
una configuracion de hardware compilada. Por eso V1 es solamente para el perfil
exacto validado en el prototipo. Para otros displays/codecs/pines se debe generar
otra variante de firmware Care y validarla en hardware real antes de agregarla al
instalador.
