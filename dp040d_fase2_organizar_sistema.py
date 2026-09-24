from pathlib import Path
from datetime import datetime
from collections import Counter
import shutil
import re

ROOT = Path(r"C:\xiaozhi-esp32_prueba3")
SRC = ROOT / "components" / "care-web" / "care_web_page.cc"

if not SRC.exists():
    raise SystemExit(f"ERROR: no existe {SRC}")

text = SRC.read_text(encoding="utf-8")
MARKER = "DP040D_FASE2_SYSTEM_GROUPS"

if MARKER in text:
    print("DP-040D Fase 2 ya parece aplicada. No se hicieron cambios.")
    raise SystemExit(0)

phase1_ok = (
    "DP040D_FASE1_NAV_VISUAL_V2" in text
    or "DP040D_FASE1_NAV_VISUAL" in text
)
if not phase1_ok:
    raise SystemExit(
        "ERROR: no encuentro la marca de DP-040D Fase 1. "
        "No voy a aplicar Fase 2 sobre una versión inesperada."
    )

section_start = text.find('<section id="section-maintenance"')
if section_start < 0:
    raise SystemExit("ERROR: no encontré section-maintenance.")

section_end = text.find("</section>", section_start)
if section_end < 0:
    raise SystemExit("ERROR: no encontré el cierre de section-maintenance.")
section_end += len("</section>")

before = text[section_start:section_end]

def inventory(html):
    return {
        "ids": Counter(re.findall(r'\bid="([^"]+)"', html)),
        "buttons": len(re.findall(r'<button\b', html)),
        "forms": len(re.findall(r'<form\b', html)),
        "inputs": len(re.findall(r'<input\b', html)),
        "selects": len(re.findall(r'<select\b', html)),
        "textareas": len(re.findall(r'<textarea\b', html)),
        "data_profiles": Counter(re.findall(r'\bdata-profile="([^"]+)"', html)),
    }

inv_before = inventory(before)

# ---------------------------------------------------------------------------
# Helpers: work only inside section-maintenance.
# ---------------------------------------------------------------------------
section = before

def add_class_to_card(section_text, heading, extra_class, prefix=""):
    token = f"<h2>{heading}</h2>"
    pos = section_text.find(token)
    if pos < 0:
        raise SystemExit(f"ERROR: no encontré el bloque '{heading}'.")

    card_start = section_text.rfind('<div class="card"', 0, pos)
    if card_start < 0 or pos - card_start > 500:
        raise SystemExit(f"ERROR: no pude ubicar la card de '{heading}'.")

    open_end = section_text.find(">", card_start)
    if open_end < 0:
        raise SystemExit(f"ERROR: card inválida en '{heading}'.")

    opening = section_text[card_start:open_end + 1]
    if 'systemCard' not in opening:
        if 'class="card"' not in opening:
            raise SystemExit(f"ERROR: apertura de card inesperada en '{heading}': {opening}")
        new_opening = opening.replace(
            'class="card"',
            f'class="card systemCard {extra_class}"',
            1,
        )
        section_text = (
            section_text[:card_start]
            + prefix
            + new_opening
            + section_text[open_end + 1:]
        )
    elif prefix:
        section_text = section_text[:card_start] + prefix + section_text[card_start:]

    return section_text

def group_header(title, subtitle, tone):
    return f'''<div class="systemGroupHeader systemGroup{tone}">
          <div class="systemGroupBadge">{tone[:1]}</div>
          <div>
            <div class="systemGroupTitle">{title}</div>
            <div class="systemGroupHint">{subtitle}</div>
          </div>
        </div>
        '''

# ---------------------------------------------------------------------------
# 1) Cabecera de Sistema + aviso más claro.
# ---------------------------------------------------------------------------
section_open_end = section.find(">")
if section_open_end < 0:
    raise SystemExit("ERROR: section-maintenance inválida.")

intro = '''
      <!-- DP040D_FASE2_SYSTEM_GROUPS -->
      <div class="systemIntro">
        <div>
          <div class="systemIntroKicker">CONFIGURACIÓN DEL DISPOSITIVO</div>
          <div class="systemIntroTitle">Sistema</div>
          <div class="systemIntroText">
            Ajustes de voz y audio, estado interno, copias de seguridad y herramientas administrativas.
          </div>
        </div>
        <div class="systemIntroBadge">LOCAL</div>
      </div>
'''

section = section[:section_open_end + 1] + intro + section[section_open_end + 1:]

old_notice = (
    '<div class="notice">Herramientas de mantenimiento para pruebas. '
    'No borra WiFi ni configuración de XiaoZhi. Usar con cuidado.</div>'
)
new_notice = (
    '<div class="notice systemNotice">Los cambios de esta sección se aplican localmente en XiaoZhi Care. '
    'Las acciones que eliminan o restauran datos están identificadas por separado.</div>'
)
if old_notice not in section:
    raise SystemExit("ERROR: no encontré el aviso actual de Mantenimiento.")
section = section.replace(old_notice, new_notice, 1)

# ---------------------------------------------------------------------------
# 2) Primera grilla: Audio y voz / Estado / Administración.
# ---------------------------------------------------------------------------
section = add_class_to_card(
    section,
    "Velocidad de escucha",
    "systemCardAudio",
    group_header(
        "Audio y voz",
        "Cómo escucha XiaoZhi y cómo se reproduce el sonido.",
        "Audio",
    ),
)
section = add_class_to_card(section, "Volumen del parlante", "systemCardAudio")

section = add_class_to_card(
    section,
    "Estado de memoria",
    "systemCardState",
    group_header(
        "Estado",
        "Información del almacenamiento y uso actual de XiaoZhi Care.",
        "State",
    ),
)
section = add_class_to_card(section, "Uso de XiaoZhi Care", "systemCardState")

section = add_class_to_card(
    section,
    "Backup completo",
    "systemCardAdmin",
    group_header(
        "Administración",
        "Respaldo y recuperación de la configuración de Care.",
        "Admin",
    ),
)

# ---------------------------------------------------------------------------
# 3) Audios personalizados: mantener toda la lógica existente, sólo agrupar.
# ---------------------------------------------------------------------------
audio_area_token = '<div id="maintenanceAudioArea"'
audio_area_pos = section.find(audio_area_token)
if audio_area_pos < 0:
    raise SystemExit("ERROR: no encontré maintenanceAudioArea.")

audio_header = '''<div class="systemStandaloneHeader systemStandaloneAudio">
        <div class="systemStandaloneIcon">A</div>
        <div>
          <div class="systemGroupTitle">Audios personalizados</div>
          <div class="systemGroupHint">Archivos OGG asignados a recordatorios y avisos del pastillero.</div>
        </div>
      </div>
      '''
section = section[:audio_area_pos] + audio_header + section[audio_area_pos:]

section = add_class_to_card(section, "Agregar audio", "systemCardAudio")
section = add_class_to_card(section, "Audios asignados", "systemCardAudio")

# Cambiar únicamente la etiqueta visual "Mantenimiento" dentro de Audios asignados.
section = section.replace(
    '<div class="panelTitle"><h2>Audios asignados</h2><span class="counter">Mantenimiento</span></div>',
    '<div class="panelTitle"><h2>Audios asignados</h2><span class="counter">Audio</span></div>',
    1,
)

# ---------------------------------------------------------------------------
# 4) Limpieza segura: bloque administrativo separado.
# ---------------------------------------------------------------------------
lower_token = '<div id="maintenanceLowerArea"'
lower_pos = section.find(lower_token)
if lower_pos < 0:
    raise SystemExit("ERROR: no encontré maintenanceLowerArea.")

lower_header = '''<div class="systemStandaloneHeader systemStandaloneDanger">
        <div class="systemStandaloneIcon">!</div>
        <div>
          <div class="systemGroupTitle">Limpieza y seguridad</div>
          <div class="systemGroupHint">Acciones administrativas para pruebas y mantenimiento del almacenamiento.</div>
        </div>
      </div>
      '''
section = section[:lower_pos] + lower_header + section[lower_pos:]
section = add_class_to_card(section, "Limpieza segura", "systemCardDanger")

# ---------------------------------------------------------------------------
# 5) CSS: añadido al final del style para que prevalezca sobre estilos previos.
# ---------------------------------------------------------------------------
css = r'''
/* DP040D_FASE2_SYSTEM_GROUPS */
.systemIntro{
  display:flex;
  align-items:center;
  justify-content:space-between;
  gap:18px;
  margin:0 0 14px;
  padding:20px 22px;
  border:1px solid #d8e6f6;
  border-radius:22px;
  background:
    radial-gradient(circle at 95% 0,rgba(97,197,255,.20),transparent 32%),
    linear-gradient(135deg,#ffffff,#f4f9ff);
  box-shadow:0 10px 28px rgba(17,44,95,.06);
}
.systemIntroKicker{
  color:#2f6cb3;
  font-size:11px;
  font-weight:950;
  letter-spacing:.12em;
}
.systemIntroTitle{
  margin-top:3px;
  color:var(--navy);
  font-size:25px;
  font-weight:950;
  letter-spacing:-.035em;
}
.systemIntroText{
  margin-top:5px;
  max-width:760px;
  color:var(--muted);
  font-size:13px;
  line-height:1.45;
}
.systemIntroBadge{
  flex:0 0 auto;
  padding:7px 11px;
  border-radius:999px;
  background:#eafaf3;
  border:1px solid #c8f0df;
  color:#08744c;
  font-size:11px;
  font-weight:950;
  letter-spacing:.08em;
}
.systemNotice{
  margin-bottom:18px;
}
#section-maintenance > .grid{
  align-items:stretch;
}
.systemGroupHeader{
  grid-column:1/-1;
  display:flex;
  align-items:center;
  gap:11px;
  margin:6px 0 -2px;
  padding:8px 5px;
}
.systemGroupBadge,
.systemStandaloneIcon{
  display:flex;
  align-items:center;
  justify-content:center;
  width:32px;
  height:32px;
  flex:0 0 32px;
  border-radius:11px;
  font-size:12px;
  font-weight:950;
}
.systemGroupTitle{
  color:var(--navy);
  font-size:17px;
  font-weight:950;
  letter-spacing:-.02em;
}
.systemGroupHint{
  margin-top:2px;
  color:var(--muted);
  font-size:12px;
  line-height:1.35;
}
.systemGroupAudio .systemGroupBadge{
  background:#e8f3ff;
  color:#2368b4;
}
.systemGroupState .systemGroupBadge{
  background:#edf9f4;
  color:#137655;
}
.systemGroupAdmin .systemGroupBadge{
  background:#f4efff;
  color:#6542a5;
}
.systemCard{
  position:relative;
  overflow:hidden;
}
.systemCard:before{
  content:'';
  position:absolute;
  left:0;
  top:18px;
  bottom:18px;
  width:4px;
  border-radius:0 8px 8px 0;
}
.systemCardAudio:before{background:#5aa8ff}
.systemCardState:before{background:#43b98b}
.systemCardAdmin:before{background:#9471d8}
.systemCardDanger:before{background:#e46b78}
.systemStandaloneHeader{
  display:flex;
  align-items:center;
  gap:11px;
  max-width:1340px;
  margin:22px auto 10px;
  padding:0 5px;
}
.systemStandaloneAudio .systemStandaloneIcon{
  background:#e8f3ff;
  color:#2368b4;
}
.systemStandaloneDanger .systemStandaloneIcon{
  background:#fff0f2;
  color:#b42336;
}
#maintenanceAudioArea{
  margin-top:0!important;
}
#maintenanceLowerArea{
  margin-top:0!important;
}
#maintenanceAudioArea .grid{
  align-items:stretch;
}
#maintenanceLowerArea .systemCardDanger{
  border-color:#f2cfd5;
  background:linear-gradient(180deg,#fff,#fffafb);
}
@media(max-width:900px){
  .systemIntro{
    align-items:flex-start;
    padding:17px;
  }
  .systemIntroTitle{font-size:22px}
}
@media(max-width:620px){
  .systemIntro{flex-direction:column}
  .systemGroupHeader,
  .systemStandaloneHeader{align-items:flex-start}
}
'''

style_end = text.find("</style>")
if style_end < 0:
    raise SystemExit("ERROR: no encontré </style> en care_web_page.cc.")
text = text[:style_end] + css + "\n" + text[style_end:]

# Reemplazar únicamente la sección ya modificada.
text = text[:section_start] + section + text[section_end:]

# ---------------------------------------------------------------------------
# 6) Validación: ningún control funcional de maintenance puede desaparecer.
# ---------------------------------------------------------------------------
new_section_start = text.find('<section id="section-maintenance"')
new_section_end = text.find("</section>", new_section_start) + len("</section>")
after = text[new_section_start:new_section_end]
inv_after = inventory(after)

if inv_before != inv_after:
    print("Inventario ANTES:", inv_before)
    print("Inventario DESPUÉS:", inv_after)
    raise SystemExit(
        "ERROR: cambió el inventario funcional de la sección Sistema. "
        "No se escribió ningún archivo."
    )

required = [
    MARKER,
    "systemIntro",
    "Audio y voz",
    "Estado",
    "Administración",
    "Audios personalizados",
    "Limpieza y seguridad",
    'id="saveListeningProfileBtn"',
    'id="saveAudioVolumeBtn"',
    'id="maintenanceRefresh"',
    'id="fullBackupDownload"',
    'id="recordingForm"',
    'id="recordingsList"',
    'id="clearExecutionsBtn"',
    'id="clearLegacyPillboxBtn"',
]
missing_required = [x for x in required if x not in text]
if missing_required:
    raise SystemExit(
        "ERROR: validación final falló. Faltan:\n" + "\n".join(missing_required)
    )

stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
backup = SRC.with_name(SRC.name + f".before-dp040d-fase2-{stamp}.bak")
shutil.copy2(SRC, backup)
SRC.write_text(text, encoding="utf-8")

print()
print("DP-040D FASE 2 APLICADA CORRECTAMENTE")
print("====================================")
print("Backup:")
print(" ", backup)
print()
print("Archivo modificado:")
print(r"  components\care-web\care_web_page.cc")
print()
print("Organización interna de SISTEMA:")
print("  AUDIO Y VOZ")
print("    - Velocidad de escucha")
print("    - Volumen del parlante")
print("    - Audios personalizados")
print("  ESTADO")
print("    - Estado de memoria")
print("    - Uso de XiaoZhi Care")
print("  ADMINISTRACIÓN")
print("    - Backup completo")
print("    - Limpieza y seguridad")
print()
print("Seguridad de compatibilidad:")
print("  - IDs preservados:", len(inv_before["ids"]))
print("  - botones preservados:", inv_before["buttons"])
print("  - forms preservados:", inv_before["forms"])
print("  - inputs preservados:", inv_before["inputs"])
print("  - selects preservados:", inv_before["selects"])
print("  - textareas preservados:", inv_before["textareas"])
print("  - no se modifica JavaScript ni backend")
print()
print("Siguiente comando:")
print("  idf.py build")
