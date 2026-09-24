from pathlib import Path
from datetime import datetime
import shutil

ROOT = Path(r"C:\xiaozhi-esp32_prueba3")

SETTINGS = ROOT / "components" / "care-listening" / "listening_settings.cc"
PAGE = ROOT / "components" / "care-web" / "care_web_page.cc"
APP = ROOT / "main" / "application.cc"

for p in (SETTINGS, PAGE):
    if not p.exists():
        raise SystemExit(f"ERROR: no existe {p}")

settings = SETTINGS.read_text(encoding="utf-8")
page = PAGE.read_text(encoding="utf-8")
app = APP.read_text(encoding="utf-8") if APP.exists() else None

MARKER = "DP039_TIEMPOS_MENOS_500MS"

if MARKER in settings or MARKER in page:
    print("Este ajuste de -0,5 s ya parece aplicado. No se hicieron cambios.")
    raise SystemExit(0)

# Estado esperado actual:
#   Rápida = 1500 ms
#   Media  = 2000 ms
#   Lenta  = 3000 ms
checks = [
    ("settings fast",   "case ListeningProfile::Fast: return 1500;" in settings),
    ("settings medium", "case ListeningProfile::Medium: return 2000;" in settings),
    ("settings slow",   "case ListeningProfile::Slow: return 3000;" in settings),

    ("page fast button",   'data-profile="fast"' in page and "<span>1,5 s</span>" in page),
    ("page medium button", 'data-profile="medium"' in page and "<span>2,0 s</span>" in page),
    ("page slow button",   'data-profile="slow"' in page and "<span>3,0 s</span>" in page),

    ("page js fast",   "fast:{label:'Rápida',ms:1500" in page),
    ("page js medium", "medium:{label:'Media',ms:2000" in page),
    ("page js slow",   "slow:{label:'Lenta',ms:3000" in page),
]

failed = [name for name, ok in checks if not ok]
if failed:
    raise SystemExit(
        "ERROR: el proyecto no coincide con el estado esperado de DP-039.\n"
        "Faltan:\n  - " + "\n  - ".join(failed) +
        "\n\nNo se modificó ningún archivo."
    )

stamp = datetime.now().strftime("%Y%m%d-%H%M%S")

for p in (SETTINGS, PAGE):
    backup = p.with_name(p.name + f".before-dp039-minus500-{stamp}.bak")
    shutil.copy2(p, backup)
    print("Backup:", backup)

if APP.exists():
    backup = APP.with_name(APP.name + f".before-dp039-minus500-{stamp}.bak")
    shutil.copy2(APP, backup)
    print("Backup:", backup)

# ---------------------------------------------------------------------------
# 1) Runtime real: tiempos de silencio
# ---------------------------------------------------------------------------
settings = settings.replace(
    "case ListeningProfile::Fast: return 1500;",
    "case ListeningProfile::Fast: return 1000;  // DP039_TIEMPOS_MENOS_500MS",
    1
)
settings = settings.replace(
    "case ListeningProfile::Medium: return 2000;",
    "case ListeningProfile::Medium: return 1500;",
    1
)
settings = settings.replace(
    "case ListeningProfile::Slow: return 3000;",
    "case ListeningProfile::Slow: return 2500;",
    1
)

# ---------------------------------------------------------------------------
# 2) Panel web: botones visibles
# ---------------------------------------------------------------------------
page = page.replace("<span>1,5 s</span>", "<span>1,0 s</span>", 1)
page = page.replace("<span>2,0 s</span>", "<span>1,5 s</span>", 1)
page = page.replace("<span>3,0 s</span>", "<span>2,5 s</span>", 1)

# ---------------------------------------------------------------------------
# 3) Panel web: datos JS y textos de ayuda
# ---------------------------------------------------------------------------
page = page.replace(
    "fast:{label:'Rápida',ms:1500,text:'Rápida: espera 1,5 segundos de silencio.'}",
    "fast:{label:'Rápida',ms:1000,text:'Rápida: espera 1,0 segundo de silencio.'}",
    1
)
page = page.replace(
    "medium:{label:'Media',ms:2000,text:'Media: espera 2,0 segundos de silencio.'}",
    "medium:{label:'Media',ms:1500,text:'Media: espera 1,5 segundos de silencio.'}",
    1
)
page = page.replace(
    "slow:{label:'Lenta',ms:3000,text:'Lenta: espera 3,0 segundos de silencio.'}",
    "slow:{label:'Lenta',ms:2500,text:'Lenta: espera 2,5 segundos de silencio.'}",
    1
)
page = page.replace(
    "}[profile]||{label:'Media',ms:2000,text:'Media: espera 2,0 segundos de silencio.'};",
    "}[profile]||{label:'Media',ms:1500,text:'Media: espera 1,5 segundos de silencio.'};",
    1
)

# Por si el HTML tiene todavía un texto inicial hardcodeado.
page = page.replace(
    'id="listeningProfileHelp" class="itemNotes">Media: espera 2,0 segundos de silencio.</div>',
    'id="listeningProfileHelp" class="itemNotes">Media: espera 1,5 segundos de silencio.</div>',
    1
)

# ---------------------------------------------------------------------------
# 4) Comentarios de application.cc, sólo si existen.
#    No cambia lógica allí.
# ---------------------------------------------------------------------------
if app is not None:
    app = app.replace(
        "// fast=1500 ms, medium=2000 ms, slow=3000 ms.",
        "// fast=1000 ms, medium=1500 ms, slow=2500 ms.",
        1
    )
    app = app.replace(
        "// fast=1500 ms, medium=2500 ms, slow=3500 ms.",
        "// fast=1000 ms, medium=1500 ms, slow=2500 ms.",
        1
    )

# ---------------------------------------------------------------------------
# 5) Validación antes de escribir
# ---------------------------------------------------------------------------
final_checks = [
    ("runtime fast 1000",   "case ListeningProfile::Fast: return 1000;" in settings),
    ("runtime medium 1500", "case ListeningProfile::Medium: return 1500;" in settings),
    ("runtime slow 2500",   "case ListeningProfile::Slow: return 2500;" in settings),

    ("ui fast 1,0", "<span>1,0 s</span>" in page),
    ("ui medium 1,5", "<span>1,5 s</span>" in page),
    ("ui slow 2,5", "<span>2,5 s</span>" in page),

    ("js fast 1000", "fast:{label:'Rápida',ms:1000" in page),
    ("js medium 1500", "medium:{label:'Media',ms:1500" in page),
    ("js slow 2500", "slow:{label:'Lenta',ms:2500" in page),
]

failed_final = [name for name, ok in final_checks if not ok]
if failed_final:
    raise SystemExit(
        "ERROR: falló la validación final.\n"
        "Faltan:\n  - " + "\n  - ".join(failed_final) +
        "\n\nLos backups ya fueron creados, pero NO se escribieron los cambios."
    )

SETTINGS.write_text(settings, encoding="utf-8")
PAGE.write_text(page, encoding="utf-8")
if APP.exists() and app is not None:
    APP.write_text(app, encoding="utf-8")

print()
print("=== DP-039: -0,5 SEGUNDOS EN TODOS LOS PERFILES ===")
print()
print("Rápida : 1,0 s")
print("Media  : 1,5 s  <- predeterminada")
print("Lenta  : 2,5 s")
print()
print("Se mantiene sin cambios:")
print(" - detector PCM")
print(" - umbrales de actividad")
print(" - espera inicial")
print(" - límite máximo de escucha")
print(" - backend de recordatorios")
print(" - DP-036 / DP-037 / DP-038 / DP-040")
print()
print("Ahora ejecutá:")
print("  idf.py build")
print("  idf.py flash")
