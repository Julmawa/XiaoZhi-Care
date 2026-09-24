from pathlib import Path
from datetime import datetime
import shutil

ROOT = Path(r"C:\xiaozhi-esp32_prueba3")
TARGET = ROOT / "components" / "care-radio" / "radio_service.cc"
MP3_HEADER = (
    ROOT / "managed_components" / "espressif__esp_audio_codec" /
    "include" / "decoder" / "impl" / "esp_mp3_dec.h"
)

if not TARGET.exists():
    raise SystemExit(f"ERROR: no existe {TARGET}")

if not MP3_HEADER.exists():
    raise SystemExit(
        f"ERROR: no existe {MP3_HEADER}\n"
        "No se modificó ningún archivo."
    )

header_text = MP3_HEADER.read_text(encoding="utf-8", errors="ignore")
if "esp_mp3_dec_register(void)" not in header_text:
    raise SystemExit(
        "ERROR: la versión instalada de esp_audio_codec no expone "
        "esp_mp3_dec_register().\nNo se modificó ningún archivo."
    )

text = TARGET.read_text(encoding="utf-8")

if "DP042A_MP3_REGISTER_FIX" in text:
    print("El fix de registro MP3 ya parece aplicado. No se hicieron cambios.")
    raise SystemExit(0)

if "ESP_AUDIO_SIMPLE_DEC_TYPE_MP3" not in text:
    raise SystemExit(
        "ERROR: no encontré el decoder MP3 de DP-042A en radio_service.cc.\n"
        "No se modificó ningún archivo."
    )

include_anchor = '#include "esp_audio_simple_dec.h"\n'
include_line = '#include "decoder/impl/esp_mp3_dec.h"\n'

if include_anchor not in text:
    raise SystemExit(
        "ERROR: no encontré el include esp_audio_simple_dec.h esperado.\n"
        "No se modificó ningún archivo."
    )

text = text.replace(
    include_anchor,
    include_anchor + include_line,
    1
)

anchor = '''    if (!LoadSettings()) {
        ESP_LOGI(kTag, "Using built-in default station");
    }

    if (task_handle_ == nullptr) {
'''

replacement = '''    if (!LoadSettings()) {
        ESP_LOGI(kTag, "Using built-in default station");
    }

    // DP042A_MP3_REGISTER_FIX
    // esp_audio_simple_dec usa el registro común de decoders.
    // Tener CONFIG_AUDIO_DECODER_MP3_SUPPORT=y compila MP3, pero no lo
    // registra automáticamente en runtime.
    const esp_audio_err_t mp3_register_ret =
        esp_mp3_dec_register();

    if (mp3_register_ret != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(
            kTag,
            "Unable to register MP3 decoder: %d",
            static_cast<int>(mp3_register_ret));
        return false;
    }

    ESP_LOGI(kTag, "MP3 decoder registered");

    if (task_handle_ == nullptr) {
'''

if anchor not in text:
    raise SystemExit(
        "ERROR: no encontré el bloque esperado dentro de RadioService::Init().\n"
        "No se modificó ningún archivo."
    )

text = text.replace(anchor, replacement, 1)

checks = {
    "include esp_mp3_dec": '#include "decoder/impl/esp_mp3_dec.h"' in text,
    "registro MP3": "esp_mp3_dec_register();" in text,
    "control de error": "mp3_register_ret != ESP_AUDIO_ERR_OK" in text,
    "log de éxito": 'ESP_LOGI(kTag, "MP3 decoder registered");' in text,
    "simple decoder preservado": "ESP_AUDIO_SIMPLE_DEC_TYPE_MP3" in text,
    "stream preservado": "esp_http_client_read" in text,
}
bad = [name for name, ok in checks.items() if not ok]

if bad:
    raise SystemExit(
        "ERROR: validación final falló:\n  - "
        + "\n  - ".join(bad)
        + "\nNo se modificó ningún archivo."
    )

stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
backup = TARGET.with_name(
    TARGET.name + f".before-dp042a-mp3-register-{stamp}.bak"
)
shutil.copy2(TARGET, backup)

TARGET.write_text(text, encoding="utf-8")

print()
print("====================================================")
print(" DP-042A - REGISTRO MP3 CORREGIDO")
print("====================================================")
print()
print("Backup:")
print(backup)
print()
print("Se agregó:")
print('  #include "decoder/impl/esp_mp3_dec.h"')
print("  esp_mp3_dec_register();")
print()
print("Motivo:")
print("  El Simple Decoder usa el registro común de decoders.")
print("  MP3 estaba compilado, pero no registrado en runtime.")
print()
print("Después del flash debería aparecer:")
print("  CARE_RADIO: MP3 decoder registered")
print()
print("Y NO debería aparecer:")
print("  AUDIO_DEC: Decoder MP3(...) not registered")
print()
print("Ahora ejecutá:")
print("  idf.py build")
print("  idf.py flash")
