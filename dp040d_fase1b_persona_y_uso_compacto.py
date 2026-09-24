from pathlib import Path
from datetime import datetime
import shutil

ROOT = Path(r"C:\xiaozhi-esp32_prueba3")
SRC = ROOT / "components" / "care-web" / "care_web_page.cc"

if not SRC.exists():
    raise SystemExit(f"ERROR: no existe {SRC}")

text = SRC.read_text(encoding="utf-8")
MARKER = "DP040D_FASE1B_PERSONA_Y_USO_COMPACTO"

if MARKER in text:
    print("Este parche ya parece aplicado. No se hicieron cambios.")
    raise SystemExit(0)

if "DP040D_FASE1_NAV_VISUAL" not in text:
    raise SystemExit(
        "ERROR: no encuentro la navegación DP-040D Fase 1.\n"
        "No se modificó ningún archivo."
    )

# ---------------------------------------------------------------------------
# 1) PERSONA: Perfil principal primero / Familia y otros segundo
# ---------------------------------------------------------------------------
group_start = text.find('<div class="careNavGroup careNavPeople">')
next_group = text.find('<div class="careNavGroup careNavCare">', group_start)

if group_start < 0 or next_group < 0:
    raise SystemExit("ERROR: no pude localizar correctamente el grupo PERSONA.")

old_group = text[group_start:next_group]

new_group = '''<div class="careNavGroup careNavPeople">
        <!-- DP040D_FASE1B_PERSONA_Y_USO_COMPACTO -->
        <div class="careNavHead">
          <span class="careNavKicker">PERSONA</span>
          <span class="careNavHint">Información y vínculos</span>
        </div>
        <div class="careNavItems">
          <button class="tab active" data-tab="profile" type="button">
            <span class="careNavIcon">01</span><span>Perfil principal</span>
          </button>
          <button class="tab" data-tab="people" type="button">
            <span class="careNavIcon">02</span><span>Familia y otros</span>
          </button>
        </div>
      </div>

      '''

text = text[:group_start] + new_group + text[next_group:]

# La sección inicial también pasa a Perfil principal.
text = text.replace(
    '<section id="section-people" class="section active">',
    '<section id="section-people" class="section">',
    1,
)
text = text.replace(
    '<section id="section-profile" class="section">',
    '<section id="section-profile" class="section active">',
    1,
)

# Si ya estaba aplicado parcialmente, asegurar estado correcto.
text = text.replace(
    'class="tab active" data-tab="people"',
    'class="tab" data-tab="people"',
)
text = text.replace(
    'class="tab" data-tab="profile"',
    'class="tab active" data-tab="profile"',
)

# ---------------------------------------------------------------------------
# 2) "Uso de XiaoZhi Care": compactar visualmente la tarjeta
#    Sin moverla ni tocar IDs / JS / backend.
# ---------------------------------------------------------------------------
heading = '<h2>Uso de XiaoZhi Care</h2>'
pos = text.find(heading)
if pos < 0:
    raise SystemExit("ERROR: no encontré la tarjeta 'Uso de XiaoZhi Care'.")

card_start = text.rfind('<div class="card"', 0, pos)
if card_start < 0 or pos - card_start > 500:
    raise SystemExit("ERROR: no pude identificar la card de 'Uso de XiaoZhi Care'.")

card_open_end = text.find(">", card_start)
opening = text[card_start:card_open_end + 1]

if 'usageCompactCard' not in opening:
    if 'class="card"' not in opening:
        raise SystemExit(
            "ERROR: apertura inesperada de la tarjeta Uso de XiaoZhi Care:\n" + opening
        )
    opening_new = opening.replace(
        'class="card"',
        'class="card usageCompactCard"',
        1,
    )
    text = text[:card_start] + opening_new + text[card_open_end + 1:]

css = r'''
/* DP040D_FASE1B_PERSONA_Y_USO_COMPACTO */
#section-maintenance .usageCompactCard{
  padding:14px 16px;
}
#section-maintenance .usageCompactCard .panelTitle{
  margin-bottom:8px;
}
#section-maintenance .usageCompactCard .panelTitle h2{
  font-size:19px;
}
#section-maintenance .usageCompactCard .item{
  padding:10px 12px;
  border-radius:14px;
}
#section-maintenance .usageCompactCard .list{
  gap:8px;
}
#section-maintenance .usageCompactCard .itemName{
  font-size:14px;
}
#section-maintenance .usageCompactCard .itemMeta,
#section-maintenance .usageCompactCard .itemNotes{
  font-size:12px;
  line-height:1.3;
}
'''

style_end = text.find("</style>")
if style_end < 0:
    raise SystemExit("ERROR: no encontré </style>.")

text = text[:style_end] + css + "\n" + text[style_end:]

# ---------------------------------------------------------------------------
# Validaciones
# ---------------------------------------------------------------------------
checks = {
    "marcador": MARKER in text,
    "perfil primero": (
        text.find('data-tab="profile"', group_start)
        < text.find('data-tab="people"', group_start)
    ),
    "familia y otros": "Familia y otros" in text,
    "perfil activo": 'class="tab active" data-tab="profile"' in text,
    "people no activo": 'class="tab" data-tab="people"' in text,
    "section-profile activa": '<section id="section-profile" class="section active">' in text,
    "section-people inactiva": '<section id="section-people" class="section">' in text,
    "uso compacto": "usageCompactCard" in text,
    "uso de XiaoZhi Care": "Uso de XiaoZhi Care" in text,
    "maintenanceCount preservado": 'id="maintenanceCount"' in text,
    "maintenanceList preservado": 'id="maintenanceList"' in text,
    "preparedAudioSummary preservado": 'id="preparedAudioSummary"' in text,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit(
        "ERROR: validación final falló: "
        + ", ".join(failed)
        + "\nNo se modificó ningún archivo."
    )

stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
backup = SRC.with_name(SRC.name + f".before-dp040d-fase1b-v2-{stamp}.bak")
shutil.copy2(SRC, backup)
SRC.write_text(text, encoding="utf-8")

print()
print("DP-040D FASE 1B V2 APLICADA")
print("===========================")
print("Backup:")
print(" ", backup)
print()
print("Cambios:")
print("  PERSONA")
print("    01 Perfil principal")
print("    02 Familia y otros")
print()
print("  SISTEMA")
print("    - 'Uso de XiaoZhi Care' ahora es visualmente más compacto")
print("    - menos padding")
print("    - menor altura visual")
print("    - tipografía y listas más contenidas")
print()
print("Compatibilidad preservada:")
print("  - mismos data-tab")
print("  - mismos IDs")
print("  - mismo JavaScript")
print("  - mismo backend")
print()
print("Ahora ejecutá:")
print("  idf.py build")
print("  idf.py flash")
