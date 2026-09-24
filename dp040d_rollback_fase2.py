from pathlib import Path
from datetime import datetime
import shutil

ROOT = Path(r"C:\xiaozhi-esp32_prueba3")
SRC = ROOT / "components" / "care-web" / "care_web_page.cc"

if not SRC.exists():
    raise SystemExit(f"ERROR: no existe {SRC}")

backups = sorted(
    SRC.parent.glob(SRC.name + ".before-dp040d-fase2-*.bak"),
    key=lambda p: p.stat().st_mtime,
    reverse=True,
)

if not backups:
    raise SystemExit(
        "ERROR: no encontré backups .before-dp040d-fase2-*.bak.\n"
        "No se modificó ningún archivo."
    )

backup = backups[0]
backup_text = backup.read_text(encoding="utf-8", errors="replace")

# Debe ser el estado posterior a Fase 1, anterior a Fase 2.
phase1_ok = (
    "DP040D_FASE1_NAV_VISUAL_V2" in backup_text
    or "DP040D_FASE1_NAV_VISUAL" in backup_text
)

if not phase1_ok:
    raise SystemExit(
        "ERROR: el backup más reciente de Fase 2 no contiene la marca de Fase 1.\n"
        f"Backup detectado: {backup}\n"
        "No se modificó ningún archivo."
    )

# Guardar por seguridad el estado roto actual antes de restaurar.
stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
broken_copy = SRC.with_name(SRC.name + f".dp040d-fase2-descartada-{stamp}.bak")
shutil.copy2(SRC, broken_copy)

# Restaurar exactamente el archivo anterior a Fase 2.
shutil.copy2(backup, SRC)

restored = SRC.read_text(encoding="utf-8", errors="replace")

checks = {
    "Fase 1 presente": (
        "DP040D_FASE1_NAV_VISUAL_V2" in restored
        or "DP040D_FASE1_NAV_VISUAL" in restored
    ),
    "Fase 2 removida": "DP040D_FASE2_SYSTEM_GROUPS" not in restored,
    "Personas": 'data-tab="people"' in restored,
    "Perfil principal": "Perfil principal" in restored,
    "Rutinas": 'data-tab="routines"' in restored,
    "Presión arterial": "Presión arterial" in restored,
    "Pastillero": 'data-tab="pillbox"' in restored,
    "Recordatorios": 'data-tab="reminders"' in restored,
    "Sistema": 'data-tab="maintenance"' in restored,
    "section-maintenance": 'id="section-maintenance"' in restored,
}

failed = [k for k, ok in checks.items() if not ok]
if failed:
    raise SystemExit(
        "ERROR: restauración realizada pero falló validación: "
        + ", ".join(failed)
    )

print()
print("ROLLBACK DP-040D FASE 2 COMPLETADO")
print("=================================")
print("Restaurado desde:")
print(" ", backup)
print()
print("Copia del estado descartado:")
print(" ", broken_copy)
print()
print("Validación:")
for k, ok in checks.items():
    print(f"  {k:22} -> {ok}")
print()
print("Resultado:")
print(" - DP-040D Fase 1 se conserva")
print(" - DP-040D Fase 2 fue retirada")
print(" - La navegación PERSONA / CUIDADOS / SISTEMA queda intacta")
print()
print("Ahora ejecutá:")
print("  idf.py build")
print("  idf.py flash")
