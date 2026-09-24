from pathlib import Path
from datetime import datetime
import shutil

ROOT = Path(r"C:\xiaozhi-esp32_prueba3")
TARGET = ROOT / "main" / "application.cc"

if not TARGET.exists():
    raise SystemExit(f"ERROR: no existe {TARGET}")

text = TARGET.read_text(encoding="utf-8")

MARKER = "DP042C_RADIO_WIFI_PERFORMANCE"
if MARKER in text:
    print("DP-042C ya parece aplicado. No se hicieron cambios.")
    raise SystemExit(0)

required = {
    "care-radio include": '#include "care_radio/radio_service.h"' in text,
    "radio state hook": "SetSystemPaused(new_state != kDeviceStateIdle)" in text,
    "audio channel close": "protocol_->OnAudioChannelClosed" in text,
    "power save low": "board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);" in text,
}
bad = [name for name, ok in required.items() if not ok]
if bad:
    raise SystemExit(
        "ERROR: el archivo no coincide con el estado esperado:\n  - "
        + "\n  - ".join(bad)
        + "\nNo se modificó ningún archivo."
    )

# -------------------------------------------------------------------------
# 1) Al cerrarse el canal MQTT de voz:
#    si la radio sigue activa, mantener WiFi PERFORMANCE.
# -------------------------------------------------------------------------
old_close = '''    protocol_->OnAudioChannelClosed([this, &board]() {
        board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        Schedule([this]() {
'''

new_close = '''    protocol_->OnAudioChannelClosed([this, &board]() {
        // DP042C_RADIO_WIFI_PERFORMANCE
        // La radio sigue usando WiFi aunque el canal MQTT de voz se cierre.
        // Si bajamos a MAX_MODEM durante el streaming aparecen underruns.
        auto& care_radio =
            xiaozhi_care::radio::RadioService::GetInstance();

        if (care_radio.WantsPlaying() &&
            !care_radio.IsUserPaused()) {
            board.SetPowerSaveLevel(
                PowerSaveLevel::PERFORMANCE);
            ESP_LOGI(
                TAG,
                "Radio active: keeping WiFi power save at PERFORMANCE");
        } else {
            board.SetPowerSaveLevel(
                PowerSaveLevel::LOW_POWER);
        }

        Schedule([this]() {
'''

if old_close not in text:
    raise SystemExit(
        "ERROR: no encontré exactamente el callback OnAudioChannelClosed esperado.\n"
        "No se modificó ningún archivo."
    )
text = text.replace(old_close, new_close, 1)

# -------------------------------------------------------------------------
# 2) Al terminar una notificación XiaoZhi:
#    no bajar WiFi si la radio debe continuar.
# -------------------------------------------------------------------------
old_stop_notification = '''    auto& board = Board::GetInstance();
    board.GetDisplay()->SetChatMessage("assistant", "");
    board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
    if (GetDeviceState() == kDeviceStateNotifying) {
'''

new_stop_notification = '''    auto& board = Board::GetInstance();
    board.GetDisplay()->SetChatMessage("assistant", "");

    // DP042C_RADIO_WIFI_PERFORMANCE
    // Una notificación no debe degradar la conexión de una radio activa.
    auto& care_radio =
        xiaozhi_care::radio::RadioService::GetInstance();

    if (care_radio.WantsPlaying() &&
        !care_radio.IsUserPaused()) {
        board.SetPowerSaveLevel(
            PowerSaveLevel::PERFORMANCE);
    } else {
        board.SetPowerSaveLevel(
            PowerSaveLevel::LOW_POWER);
    }

    if (GetDeviceState() == kDeviceStateNotifying) {
'''

if old_stop_notification not in text:
    raise SystemExit(
        "ERROR: no encontré exactamente StopNotification esperado.\n"
        "No se modificó ningún archivo."
    )
text = text.replace(old_stop_notification, new_stop_notification, 1)

# -------------------------------------------------------------------------
# Validación antes de escribir
# -------------------------------------------------------------------------
checks = {
    "marker": MARKER in text,
    "radio active condition":
        "care_radio.WantsPlaying()" in text and
        "!care_radio.IsUserPaused()" in text,
    "performance log":
        "Radio active: keeping WiFi power save at PERFORMANCE" in text,
    "channel callback preserved":
        "protocol_->OnAudioChannelClosed" in text,
    "notification preserved":
        "void Application::StopNotification()" in text,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit(
        "ERROR: validación final falló:\n  - "
        + "\n  - ".join(failed)
        + "\nNo se modificó ningún archivo."
    )

stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
backup = TARGET.with_name(
    TARGET.name + f".before-dp042c-radio-wifi-{stamp}.bak"
)
shutil.copy2(TARGET, backup)

TARGET.write_text(text, encoding="utf-8")

print()
print("====================================================")
print(" DP-042C - WIFI PERFORMANCE DURANTE RADIO")
print("====================================================")
print()
print("Backup:")
print(backup)
print()
print("Cambio principal:")
print(" - Si la radio está activa, el cierre del canal MQTT")
print("   YA NO cambia WiFi a LOW_POWER/MAX_MODEM.")
print(" - Mantiene PowerSaveLevel::PERFORMANCE mientras")
print("   la radio esté reproduciendo.")
print(" - Al terminar una notificación se aplica la misma regla.")
print()
print("No cambia:")
print(" - buffer de 1 segundo")
print(" - decoder MP3")
print(" - resampling 44100 -> 24000")
print(" - AudioService / I2S")
print(" - MCP")
print()
print("Log esperado cuando MQTT cierre durante la radio:")
print("  Radio active: keeping WiFi power save at PERFORMANCE")
print("  WifiStation: Setting WiFi power save level: PERFORMANCE (NONE)")
print()
print("Y NO debería aparecer en ese momento:")
print("  LOW_POWER (MAX_MODEM)")
print()
print("Ahora ejecutá:")
print("  idf.py build")
print("  idf.py flash")
