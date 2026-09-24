from pathlib import Path
from datetime import datetime
import argparse
import shutil
import sys

ROOT = Path(r"C:\xiaozhi-esp32_prueba3")
BACKUP_DIR = Path(r"C:\XiaoZhi_Care_Backups")

parser = argparse.ArgumentParser(
    description="Limpieza segura del proyecto XiaoZhi Care después del checkpoint estable DP-040C."
)
parser.add_argument(
    "--apply",
    action="store_true",
    help="Ejecuta realmente el borrado. Sin esta opción sólo muestra el plan."
)
args = parser.parse_args()

if not ROOT.exists() or not ROOT.is_dir():
    raise SystemExit(f"ERROR: no existe el proyecto: {ROOT}")

BACKUP_DIR.mkdir(parents=True, exist_ok=True)
stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
log_path = BACKUP_DIR / f"XiaoZhi_Care_LIMPIEZA_DP040C_{stamp}.txt"

# ---------------------------------------------------------------------------
# REGLA CONSERVADORA
# ---------------------------------------------------------------------------
# Se borran solamente:
# 1. backups locales de parches (*.before-*.bak y *.bak)
# 2. caches Python
# 3. build (100 % regenerable)
# 4. informes y scripts históricos de parche/inspección en la RAIZ
#
# NO se toca:
# - main/
# - components/
# - managed_components/
# - extras/
# - sdkconfig / sdkconfig.defaults*
# - dependencies.lock
# - CMakeLists.txt / CMakePresets.json
# - archivos README/LICENSE/config oficiales
#
# managed_components ocupa mucho, pero se mantiene deliberadamente:
# contiene dependencias externas necesarias para compilar y en este proyecto
# hubo trabajo previo alrededor de esp_video. Se evaluará por separado.

ROOT_SCRIPT_PREFIXES = (
    "dp0",
    "fix_",
    "patch_",
    "parche_",
    "crear_backup_",
    "inspeccionar_",
)

ROOT_SCRIPT_EXACT = {
    "limpieza_y_backup_xiaozhi_care.py",
    "check_elf_tools.py",
}

ROOT_REPORT_PREFIXES = (
    "dp0",
    "backups_eliminados_",
)

def human_size(n):
    units = ["B", "KB", "MB", "GB"]
    x = float(n)
    for u in units:
        if x < 1024.0 or u == units[-1]:
            return f"{x:.1f} {u}"
        x /= 1024.0

def tree_size(path):
    if path.is_file():
        try:
            return path.stat().st_size
        except OSError:
            return 0
    total = 0
    for p in path.rglob("*"):
        if p.is_file():
            try:
                total += p.stat().st_size
            except OSError:
                pass
    return total

def is_historical_root_script(p):
    if p.parent != ROOT or not p.is_file():
        return False
    name = p.name.lower()
    if p.suffix.lower() not in {".py", ".ps1"}:
        return False
    return name.startswith(ROOT_SCRIPT_PREFIXES) or name in ROOT_SCRIPT_EXACT

def is_historical_root_report(p):
    if p.parent != ROOT or not p.is_file():
        return False
    name = p.name.lower()
    if p.suffix.lower() != ".txt":
        return False
    return (
        name.startswith(ROOT_REPORT_PREFIXES)
        or "inspeccion" in name
        or "limpieza" in name
    )

candidates = []
seen = set()

def add_candidate(path, reason):
    try:
        key = str(path.resolve()).lower()
    except Exception:
        key = str(path).lower()
    if key in seen or not path.exists():
        return
    seen.add(key)
    candidates.append((path, reason, tree_size(path)))

# 1) Build completo.
add_candidate(ROOT / "build", "Directorio de build regenerable")

# 2) Caches.
for name in ("__pycache__", ".pytest_cache", ".cache"):
    for p in ROOT.rglob(name):
        if p.is_dir():
            add_candidate(p, "Cache regenerable")

# 3) Backups locales históricos.
for p in ROOT.rglob("*"):
    if not p.is_file():
        continue
    lname = p.name.lower()
    if ".before-" in lname and lname.endswith(".bak"):
        add_candidate(p, "Backup histórico before-*")
    elif p.suffix.lower() == ".bak":
        add_candidate(p, "Backup histórico .bak")

# 4) Scripts e informes históricos en raíz.
for p in ROOT.iterdir():
    if is_historical_root_script(p):
        add_candidate(p, "Script histórico de parche/inspección")
    elif is_historical_root_report(p):
        add_candidate(p, "Informe histórico de inspección/limpieza")

# Protección explícita: jamás borrar estos aunque algún nombre coincida.
PROTECTED = {
    ROOT / "CMakeLists.txt",
    ROOT / "CMakePresets.json",
    ROOT / "dependencies.lock",
    ROOT / "sdkconfig",
    ROOT / "sdkconfig.defaults",
    ROOT / "README.md",
    ROOT / "README_ja.md",
    ROOT / "README_zh.md",
    ROOT / "LICENSE",
    ROOT / "AGENTS.md",
    ROOT / "MANIFEST-U2.json",
    ROOT / "README-U2.md",
}

filtered = []
for path, reason, size in candidates:
    if path in PROTECTED:
        continue
    # Nunca tocar estas carpetas en esta fase.
    try:
        rel = path.relative_to(ROOT)
        top = rel.parts[0] if rel.parts else ""
    except ValueError:
        continue
    if top in {"main", "components", "managed_components", "extras"} and path.is_dir():
        continue
    # Los .bak dentro de main/components SÍ se pueden borrar; sólo impedimos
    # borrar los directorios principales completos.
    filtered.append((path, reason, size))

candidates = sorted(filtered, key=lambda x: str(x[0]).lower())
total = sum(size for _, _, size in candidates)

lines = []
lines.append("XIAOZHI CARE - LIMPIEZA SEGURA POST DP-040C")
lines.append("=" * 82)
lines.append(f"Fecha: {datetime.now().isoformat(timespec='seconds')}")
lines.append(f"Proyecto: {ROOT}")
lines.append(f"Modo: {'APLICAR BORRADO' if args.apply else 'SIMULACION / DRY-RUN'}")
lines.append("")
lines.append(f"Elementos candidatos: {len(candidates)}")
lines.append(f"Espacio estimado a liberar: {human_size(total)}")
lines.append("")
lines.append("SE CONSERVA EXPLICITAMENTE:")
lines.append("- main/")
lines.append("- components/")
lines.append("- managed_components/")
lines.append("- extras/")
lines.append("- sdkconfig y sdkconfig.defaults*")
lines.append("- dependencies.lock")
lines.append("- CMakeLists.txt / CMakePresets.json")
lines.append("- documentación/configuración oficial")
lines.append("")
lines.append("CANDIDATOS:")
for path, reason, size in candidates:
    try:
        rel = path.relative_to(ROOT)
    except ValueError:
        rel = path
    lines.append(f"{human_size(size):>12}  {str(rel):<90}  [{reason}]")

deleted = []
errors = []

if args.apply:
    # Borrar archivos primero y directorios después.
    file_items = [(p, r, s) for p, r, s in candidates if p.is_file()]
    dir_items = [(p, r, s) for p, r, s in candidates if p.is_dir()]
    dir_items.sort(key=lambda x: len(x[0].parts), reverse=True)

    for path, reason, size in file_items + dir_items:
        try:
            if not path.exists():
                continue
            if path.is_dir():
                shutil.rmtree(path)
            else:
                path.unlink()
            deleted.append((path, reason, size))
        except Exception as e:
            errors.append((path, str(e)))

    freed = sum(size for _, _, size in deleted)
    lines.append("")
    lines.append("RESULTADO")
    lines.append("-" * 82)
    lines.append(f"Eliminados: {len(deleted)}")
    lines.append(f"Espacio liberado estimado: {human_size(freed)}")
    lines.append(f"Errores: {len(errors)}")
    if errors:
        for p, e in errors:
            lines.append(f"ERROR  {p}: {e}")
else:
    lines.append("")
    lines.append("No se borró nada. Para ejecutar:")
    lines.append(r"python .\limpiar_proyecto_dp040c.py --apply")

log_path.write_text("\n".join(lines) + "\n", encoding="utf-8")

print()
print("\n".join(lines[:22]))
print()
if args.apply:
    print("LIMPIEZA TERMINADA")
    print("==================")
    print(f"Eliminados: {len(deleted)}")
    print(f"Espacio liberado estimado: {human_size(sum(s for _, _, s in deleted))}")
    print(f"Errores: {len(errors)}")
else:
    print("SIMULACION TERMINADA - NO SE BORRO NADA")
    print("=======================================")
    print(r"Para borrar realmente: python .\limpiar_proyecto_dp040c.py --apply")

print()
print("Registro externo:")
print(log_path)
print()
print("managed_components y extras NO fueron tocados.")
