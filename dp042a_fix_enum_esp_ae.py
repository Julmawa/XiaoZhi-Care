from pathlib import Path
from datetime import datetime
import shutil

ROOT = Path(r"C:\xiaozhi-esp32_prueba3")
TARGET = ROOT / "main" / "audio" / "audio_service.cc"

if not TARGET.exists():
    raise SystemExit(f"ERROR: no existe {TARGET}")

text = TARGET.read_text(encoding="utf-8")

MARKER = "DP042A_RADIO_MP3_BASE"
if MARKER not in text:
    raise SystemExit(
        "ERROR: no encontré la marca DP042A_RADIO_MP3_BASE.\n"
        "No se modificó ningún archivo."
    )

old = '''        if (ret != ESP_AUDIO_ERR_OK) {
            ESP_LOGW(
                TAG,
                "Radio resample failed: %d",
                ret);
            return false;
        }
'''

new = '''        if (ret != ESP_AE_ERR_OK) {
            ESP_LOGW(
                TAG,
                "Radio resample failed: %d",
                ret);
            return false;
        }
'''

count = text.count(old)

if count == 0:
    if "Radio resample failed: %d" in text and "ESP_AE_ERR_OK" in text:
        print("El ajuste ya parece estar aplicado. No se hicieron cambios.")
        raise SystemExit(0)

    raise SystemExit(
        "ERROR: no encontré exactamente el bloque esperado.\n"
        "No se modificó ningún archivo."
    )

if count != 1:
    raise SystemExit(
        f"ERROR: encontré {count} bloques candidatos; esperaba exactamente 1.\n"
        "No se modificó ningún archivo."
    )

stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
backup = TARGET.with_name(
    TARGET.name + f".before-dp042a-enum-fix-{stamp}.bak"
)
shutil.copy2(TARGET, backup)

text = text.replace(old, new, 1)
TARGET.write_text(text, encoding="utf-8")

# Verificación posterior
check = TARGET.read_text(encoding="utf-8")
if old in check:
    raise SystemExit(
        "ERROR: la verificación posterior detectó el bloque viejo."
    )
if new not in check:
    raise SystemExit(
        "ERROR: la verificación posterior no encontró el bloque corregido."
    )

print()
print("====================================================")
print(" DP-042A - FIX ENUM esp_ae_err_t APLICADO")
print("====================================================")
print()
print("Archivo modificado:")
print(TARGET)
print()
print("Backup:")
print(backup)
print()
print("Cambio:")
print("  ESP_AUDIO_ERR_OK  ->  ESP_AE_ERR_OK")
print()
print("Motivo:")
print("  esp_ae_rate_cvt_process() devuelve esp_ae_err_t,")
print("  por lo tanto debe compararse contra ESP_AE_ERR_OK.")
print()
print("Ahora ejecutá:")
print("  idf.py build")
