# Escribe la marca de este firmware (placa + bot + fecha y hora de compilación).
# Se ejecuta en cada compilación, así la fecha siempre es la real.
# La usa la actualización por Telegram (CONFIG_TELEGRAM_OTA).
string(TIMESTAMP ahora "%d/%m/%Y %H:%M:%S")
file(WRITE "${OUT}"
"// Generado en cada compilación por main/telegram/build_stamp.cmake. No editar.\n"
"#include \"sdkconfig.h\"\n"
"const char xz_ota_id[] = \"XZOTA|\" BOARD_NAME \"|\" CONFIG_TELEGRAM_BOT_TOKEN \"|${ahora}|\";\n")
