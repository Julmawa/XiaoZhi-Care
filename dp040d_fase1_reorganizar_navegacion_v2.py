from pathlib import Path
from datetime import datetime
import shutil

ROOT = Path(r"C:\xiaozhi-esp32_prueba3")
SRC = ROOT / "components" / "care-web" / "care_web_page.cc"

if not SRC.exists():
    raise SystemExit(f"ERROR: no existe {SRC}")

text = SRC.read_text(encoding="utf-8")
MARKER = "DP040D_FASE1_NAV_VISUAL_V2"

if MARKER in text:
    print("DP-040D Fase 1 V2 ya está aplicada. No se hicieron cambios.")
    raise SystemExit(0)

nav_start = text.find('<nav class="tabs"')
if nav_start < 0:
    raise SystemExit('ERROR: no encontré <nav class="tabs"> en care_web_page.cc')

nav_end = text.find('</nav>', nav_start)
if nav_end < 0:
    raise SystemExit("ERROR: encontré el inicio de navegación pero no </nav>")
nav_end += len('</nav>')

old_nav = text[nav_start:nav_end]

required_tabs = [
    'data-tab="people"',
    'data-tab="profile"',
    'data-tab="routines"',
    'data-tab="pressure"',
    'data-tab="pillbox"',
    'data-tab="reminders"',
    'data-tab="maintenance"',
]

missing = [x for x in required_tabs if x not in old_nav]
if missing:
    raise SystemExit(
        "ERROR: el bloque de navegación encontrado no contiene todas las pestañas esperadas:\n"
        + "\n".join(missing)
    )

new_nav = '''<!-- DP040D_FASE1_NAV_VISUAL_V2 -->
    <nav class="tabs careNav" aria-label="Secciones de XiaoZhi Care">

      <div class="careNavGroup careNavPeople">
        <div class="careNavHead">
          <span class="careNavKicker">PERSONA</span>
          <span class="careNavHint">Información y vínculos</span>
        </div>
        <div class="careNavItems">
          <button class="tab active" data-tab="people" type="button">
            <span class="careNavIcon">01</span><span>Personas</span>
          </button>
          <button class="tab" data-tab="profile" type="button">
            <span class="careNavIcon">02</span><span>Perfil principal</span>
          </button>
        </div>
      </div>

      <div class="careNavGroup careNavCare">
        <div class="careNavHead">
          <span class="careNavKicker">CUIDADOS</span>
          <span class="careNavHint">Rutinas, salud y avisos</span>
        </div>
        <div class="careNavItems">
          <button class="tab" data-tab="routines" type="button">
            <span class="careNavIcon">03</span><span>Rutinas</span>
          </button>
          <button class="tab" data-tab="pressure" type="button">
            <span class="careNavIcon">04</span><span>Presión arterial</span>
          </button>
          <button class="tab" data-tab="pillbox" type="button">
            <span class="careNavIcon">05</span><span>Pastillero</span>
          </button>
          <button class="tab" data-tab="reminders" type="button">
            <span class="careNavIcon">06</span><span>Recordatorios</span>
          </button>
        </div>
      </div>

      <div class="careNavGroup careNavSystem">
        <div class="careNavHead">
          <span class="careNavKicker">SISTEMA</span>
          <span class="careNavHint">Audio, memoria y mantenimiento</span>
        </div>
        <div class="careNavItems">
          <button class="tab" data-tab="maintenance" type="button">
            <span class="careNavIcon">07</span><span>Sistema</span>
          </button>
        </div>
      </div>

    </nav>'''

text = text[:nav_start] + new_nav + text[nav_end:]

css = '''
/* DP040D_FASE1_NAV_VISUAL_V2 */
.tabs.careNav{
  display:grid;
  grid-template-columns:minmax(220px,.9fr) minmax(460px,1.75fr) minmax(230px,.95fr);
  gap:14px;
  overflow:visible;
  padding:0;
  margin:0 0 18px;
  align-items:stretch;
}
.careNavGroup{
  min-width:0;
  border:1px solid #dce8f7;
  border-radius:20px;
  padding:13px;
  background:rgba(255,255,255,.88);
  box-shadow:0 8px 24px rgba(17,44,95,.055);
}
.careNavPeople{background:linear-gradient(145deg,#fff,#f2f8ff)}
.careNavCare{background:linear-gradient(145deg,#fff,#f7f3ff)}
.careNavSystem{background:linear-gradient(145deg,#fff,#f0faf5)}
.careNavHead{display:flex;flex-direction:column;gap:2px;padding:0 3px 10px}
.careNavKicker{font-size:11px;font-weight:950;letter-spacing:.12em;color:#214d86}
.careNavHint{font-size:12px;color:var(--muted);line-height:1.3}
.careNavItems{display:flex;flex-wrap:wrap;gap:7px}
.tabs.careNav .tab{
  display:flex;
  align-items:center;
  justify-content:flex-start;
  gap:7px;
  min-height:42px;
  padding:8px 11px;
  border-radius:12px;
  box-shadow:none;
  font-size:13px;
  flex:1 1 auto;
  background:rgba(255,255,255,.95);
}
.tabs.careNav .tab:hover{
  transform:translateY(-1px);
  border-color:#a9c9ee;
  box-shadow:0 5px 12px rgba(17,44,95,.07);
}
.tabs.careNav .tab.active{
  background:linear-gradient(135deg,var(--navy),#214d86);
  color:#fff;
  border-color:var(--navy);
  box-shadow:0 7px 16px rgba(17,44,95,.18);
}
.careNavIcon{
  display:inline-flex;
  align-items:center;
  justify-content:center;
  width:24px;
  height:24px;
  flex:0 0 24px;
  border-radius:8px;
  font-size:10px;
  font-weight:950;
  background:#eaf3ff;
  color:#1f579c;
}
.tabs.careNav .tab.active .careNavIcon{
  background:rgba(255,255,255,.17);
  color:#fff;
}
@media(max-width:1050px){
  .tabs.careNav{grid-template-columns:1fr 1fr}
  .careNavCare{grid-column:1/-1;grid-row:2}
}
@media(max-width:680px){
  .tabs.careNav{grid-template-columns:1fr;gap:9px}
  .careNavCare{grid-column:auto;grid-row:auto}
  .careNavGroup{padding:11px;border-radius:17px}
  .careNavItems{display:grid;grid-template-columns:1fr 1fr}
  .careNavSystem .careNavItems{grid-template-columns:1fr}
  .tabs.careNav .tab{width:100%}
}
@media(max-width:430px){
  .careNavItems{grid-template-columns:1fr}
}
'''

style_end = text.find("</style>")
if style_end < 0:
    raise SystemExit("ERROR: no encontré </style>")

text = text[:style_end] + css + "\n" + text[style_end:]

for token in required_tabs:
    if token not in text:
        raise SystemExit(f"ERROR: falta token crítico luego del parche: {token}")

for sec in (
    'id="section-people"',
    'id="section-profile"',
    'id="section-routines"',
    'id="section-pressure"',
    'id="section-pillbox"',
    'id="section-reminders"',
    'id="section-maintenance"',
):
    if sec not in text:
        raise SystemExit(f"ERROR: falta sección crítica: {sec}")

if MARKER not in text or "careNavGroup" not in text or "Perfil principal" not in text:
    raise SystemExit("ERROR: validación final del contenido nuevo falló.")

stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
backup = SRC.with_name(SRC.name + f".before-dp040d-fase1-v2-{stamp}.bak")
shutil.copy2(SRC, backup)
SRC.write_text(text, encoding="utf-8")

print()
print("DP-040D FASE 1 V2 APLICADA CORRECTAMENTE")
print("========================================")
print("Backup:", backup)
print("Archivo:", SRC)
print()
print("VALIDACION:")
check = SRC.read_text(encoding="utf-8")
print("  marcador nuevo      :", MARKER in check)
print("  careNavGroup        :", "careNavGroup" in check)
print("  Perfil principal    :", "Perfil principal" in check)
print("  Presión arterial    :", "Presión arterial" in check)
print()
print("Ahora ejecutá:")
print("  idf.py build")
print("  idf.py flash")
