from pathlib import Path
from datetime import datetime
import shutil

ROOT = Path(r"C:\xiaozhi-esp32_prueba3")
OLD = "Anotado por CARA desde la voz."
NEW = "Anotado por CARE desde la voz."

if not ROOT.exists():
    raise SystemExit(f"ERROR: no existe {ROOT}")

skip_dirs = {
    "build", ".git", "__pycache__", ".idea", ".vscode",
    "managed_components"
}
allowed_ext = {
    ".cc", ".cpp", ".c", ".h", ".hpp",
    ".html", ".htm", ".js", ".ts", ".css",
    ".json", ".txt"
}

matches = []

for p in ROOT.rglob("*"):
    if not p.is_file():
        continue

    if any(part in skip_dirs for part in p.parts):
        continue

    name_low = p.name.lower()

    if ".before-" in name_low or name_low.endswith(".bak"):
        continue

    if p.suffix.lower() not in allowed_ext:
        continue

    try:
        text = p.read_text(encoding="utf-8")
    except Exception:
        continue

    count = text.count(OLD)
    if count:
        matches.append((p, text, count))

if not matches:
    print("No se encontró el texto exacto:")
    print(f'  "{OLD}"')
    print()
    print("No se modificó ningún archivo.")
    raise SystemExit(0)

print("Coincidencias encontradas:")
for p, _, count in matches:
    print(f"  {p.relative_to(ROOT)} -> {count}")

stamp = datetime.now().strftime("%Y%m%d-%H%M%S")

for p, text, count in matches:
    backup = p.with_name(p.name + f".before-care-typo-{stamp}.bak")
    shutil.copy2(p, backup)

    new_text = text.replace(OLD, NEW)
    p.write_text(new_text, encoding="utf-8")

    verify = p.read_text(encoding="utf-8")
    if OLD in verify or NEW not in verify:
        raise SystemExit(
            f"ERROR: falló la validación de {p}\n"
            f"Backup disponible en: {backup}"
        )

    print()
    print("Corregido:")
    print(" ", p.relative_to(ROOT))
    print("Backup:")
    print(" ", backup)

print()
print("CORRECCIÓN COMPLETADA")
print("=====================")
print(f'Antes: "{OLD}"')
print(f'Ahora: "{NEW}"')
print()
print("Siguiente paso:")
print("  idf.py build")
print("  idf.py flash")
