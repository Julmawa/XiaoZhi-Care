from pathlib import Path
from datetime import datetime
import shutil

ROOT = Path(r"C:\xiaozhi-esp32_prueba3")
SRC = ROOT / "components" / "care-mcp" / "care_mcp_service.cc"

if not SRC.exists():
    raise SystemExit(f"ERROR: no existe {SRC}")

text = SRC.read_text(encoding="utf-8")

old_struct = '''    struct NewsHttpBuffer {
        std::string data;
        size_t complete_items = 0;
        static constexpr size_t kMaxBytes = 32768;
    };
'''

new_struct = '''    struct NewsHttpBuffer {
        std::string data;
        size_t complete_items = 0;
    };
'''

if old_struct not in text:
    raise SystemExit(
        "ERROR: no encontré el bloque NewsHttpBuffer esperado.\n"
        "No se modificó ningún archivo."
    )

old_ref = "NewsHttpBuffer::kMaxBytes"
ref_count = text.count(old_ref)

if ref_count != 2:
    raise SystemExit(
        f"ERROR: esperaba 2 referencias a {old_ref} y encontré {ref_count}.\n"
        "No se modificó ningún archivo."
    )

text = text.replace(old_struct, new_struct, 1)
text = text.replace(old_ref, "32768U")

if "static constexpr size_t kMaxBytes = 32768;" in text:
    raise SystemExit(
        "ERROR: la constante local problemática todavía está presente.\n"
        "No se modificó ningún archivo."
    )

if text.count("32768U") < 2:
    raise SystemExit(
        "ERROR: la sustitución de límite de buffer no quedó completa.\n"
        "No se modificó ningún archivo."
    )

if "CareMcpService::GetLatestNews" not in text:
    raise SystemExit(
        "ERROR: no encuentro GetLatestNews después del cambio.\n"
        "No se modificó ningún archivo."
    )

stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
backup = SRC.with_name(
    SRC.name + f".before-dp041-fix-local-constexpr-{stamp}.bak"
)
shutil.copy2(SRC, backup)

SRC.write_text(text, encoding="utf-8")

print("Backup:", backup)
print()
print("DP-041 FIX 1 aplicado")
print("====================")
print("Se corrigió:")
print(" - static constexpr dentro de clase local NewsHttpBuffer")
print(" - se mantuvo el mismo límite de 32768 bytes")
print(" - no se modificó la lógica RSS, MCP, cache ni categorías")
print()
print("Ahora ejecutá:")
print("  idf.py build")
