from pathlib import Path
from datetime import datetime
import shutil
import re

ROOT = Path(r"C:\xiaozhi-esp32_prueba3")
SRC = ROOT / "components" / "care-web" / "care_web_page.cc"

if not SRC.exists():
    raise SystemExit(f"ERROR: no existe {SRC}")

text = SRC.read_text(encoding="utf-8")
MARKER = "DP040D_FASE1_NAV_VISUAL"

if MARKER in text:
    print("DP-040D Fase 1 ya parece aplicada. No se hicieron cambios.")
    raise SystemExit(0)

nav_pattern = re.compile(
    r'<nav\s+class="tabs"\s+aria-label="Secciones de memoria">\s*'
    r'<button\s+class="tab active"\s+data-tab="people"\s+type="button">Personas</button>\s*'
    r'<button\s+class="tab"\s+data-tab="profile"\s+type="button">Perfil</button>\s*'
    r'<button\s+class="tab"\s+data-tab="routines"\s+type="button">Cuidados</button>\s*'
    r'<button\s+class="tab"\s+data-tab="pressure"\s+type="button">Presión</button>\s*'
    r'<button\s+class="tab"\s+data-tab="pillbox"\s+type="button">Pastillero</button>\s*'
    r'<button\s+class="tab"\s+data-tab="reminders"\s+type="button">Recordatorios</button>\s*'
    r'<button\s+class="tab"\s+data-tab="maintenance"\s+type="button">Mantenimiento</button>\s*'
    r'</nav>',
    re.S
)

matches = list(nav_pattern.finditer(text))
if len(matches) != 1:
    raise SystemExit(
        f"ERROR: esperaba encontrar exactamente 1 navegación actual y encontré {len(matches)}. "
        "No se modificó ningún archivo."
    )

new_nav = '''<!-- DP040D_FASE1_NAV_VISUAL -->
    <nav class="tabs careNav" aria-label="Secciones de XiaoZhi Care">
      <div class="careNavGroup careNavPeople">
        <div class="careNavHead">
          <span class="careNavKicker">PERSONA</span>
          <span class="careNavHint">Información y vínculos</span>
        </div>
        <div class="careNavItems">
          <button class="tab active" data-tab="people" type="button">
            <span class="careNavIcon" aria-hidden="true">01</span>
            <span>Personas</span>
          </button>
          <button class="tab" data-tab="profile" type="button">
            <span class="careNavIcon" aria-hidden="true">02</span>
            <span>Perfil principal</span>
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
            <span class="careNavIcon" aria-hidden="true">03</span>
            <span>Rutinas</span>
          </button>
          <button class="tab" data-tab="pressure" type="button">
            <span class="careNavIcon" aria-hidden="true">04</span>
            <span>Presión arterial</span>
          </button>
          <button class="tab" data-tab="pillbox" type="button">
            <span class="careNavIcon" aria-hidden="true">05</span>
            <span>Pastillero</span>
          </button>
          <button class="tab" data-tab="reminders" type="button">
            <span class="careNavIcon" aria-hidden="true">06</span>
            <span>Recordatorios</span>
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
            <span class="careNavIcon" aria-hidden="true">07</span>
            <span>Sistema</span>
          </button>
        </div>
      </div>
    </nav>'''

text = nav_pattern.sub(new_nav, text, count=1)

css = '''
/* DP040D_FASE1_NAV_VISUAL
   Reorganización puramente visual: no cambia IDs, data-tab ni endpoints. */
.tabs.careNav{
  display:grid;
  grid-template-columns:minmax(210px,.9fr) minmax(420px,1.65fr) minmax(220px,.95fr);
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
  background:rgba(255,255,255,.84);
  box-shadow:0 8px 24px rgba(17,44,95,.055);
}
.careNavPeople{background:linear-gradient(145deg,#ffffff,#f3f9ff)}
.careNavCare{background:linear-gradient(145deg,#ffffff,#f7f4ff)}
.careNavSystem{background:linear-gradient(145deg,#ffffff,#f3fbf7)}
.careNavHead{
  display:flex;
  flex-direction:column;
  gap:2px;
  padding:0 3px 10px;
}
.careNavKicker{
  font-size:11px;
  font-weight:950;
  letter-spacing:.12em;
  color:#214d86;
}
.careNavHint{
  font-size:12px;
  color:var(--muted);
  line-height:1.3;
}
.careNavItems{
  display:flex;
  flex-wrap:wrap;
  gap:7px;
}
.tabs.careNav .tab{
  display:flex;
  align-items:center;
  gap:7px;
  min-height:42px;
  padding:8px 11px;
  border-radius:12px;
  box-shadow:none;
  font-size:13px;
  flex:1 1 auto;
  justify-content:flex-start;
  background:rgba(255,255,255,.92);
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
  letter-spacing:-.02em;
  background:#eaf3ff;
  color:#1f579c;
}
.tabs.careNav .tab.active .careNavIcon{
  background:rgba(255,255,255,.17);
  color:#fff;
}
@media(max-width:1050px){
  .tabs.careNav{
    grid-template-columns:1fr 1fr;
  }
  .careNavCare{
    grid-column:1/-1;
    grid-row:2;
  }
}
@media(max-width:680px){
  .tabs.careNav{
    grid-template-columns:1fr;
    gap:9px;
  }
  .careNavCare{
    grid-column:auto;
    grid-row:auto;
  }
  .careNavGroup{
    padding:11px;
    border-radius:17px;
  }
  .careNavItems{
    display:grid;
    grid-template-columns:1fr 1fr;
  }
  .careNavSystem .careNavItems{
    grid-template-columns:1fr;
  }
  .tabs.careNav .tab{
    width:100%;
  }
}
@media(max-width:430px){
  .careNavItems{
    grid-template-columns:1fr;
  }
}
'''

style_close = "</style>"
if text.count(style_close) != 1:
    raise SystemExit(
        f"ERROR: esperaba un único </style> y encontré {text.count(style_close)}. "
        "No se modificó ningún archivo."
    )

text = text.replace(style_close, css + "\n</style>", 1)

required_tokens = [
    'data-tab="people"',
    'data-tab="profile"',
    'data-tab="routines"',
    'data-tab="pressure"',
    'data-tab="pillbox"',
    'data-tab="reminders"',
    'data-tab="maintenance"',
    'id="section-people"',
    'id="section-profile"',
    'id="section-routines"',
    'id="section-pressure"',
    'id="section-pillbox"',
    'id="section-reminders"',
    'id="section-maintenance"',
]

missing_required = [x for x in required_tokens if x not in text]
if missing_required:
    raise SystemExit(
        "ERROR: validación final falló. Faltan tokens críticos:\n" +
        "\n".join(missing_required) +
        "\nNo se modificó ningún archivo."
    )

if text.count(MARKER) < 2:
    raise SystemExit("ERROR: no se insertaron correctamente los marcadores DP040D.")

if len(re.findall(r'class="tab(?: active)?"\s+data-tab=', text)) != 7:
    raise SystemExit("ERROR: la cantidad de pestañas funcionales dejó de ser 7.")

stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
backup = SRC.with_name(SRC.name + f".before-dp040d-fase1-{stamp}.bak")
shutil.copy2(SRC, backup)
SRC.write_text(text, encoding="utf-8")

print()
print("DP-040D FASE 1 APLICADA")
print("=======================")
print("Backup:")
print(" ", backup)
print()
print("Archivo modificado:")
print(r"  components\care-web\care_web_page.cc")
print()
print("Nueva organización visual:")
print("  PERSONA   -> Personas | Perfil principal")
print("  CUIDADOS  -> Rutinas | Presión arterial | Pastillero | Recordatorios")
print("  SISTEMA   -> Sistema")
print()
print("Compatibilidad preservada:")
print("  - mismos data-tab")
print("  - mismos section-*")
print("  - mismos IDs de formularios")
print("  - mismo JavaScript funcional")
print("  - mismos endpoints/payloads")
print()
print("Siguiente comando:")
print("  idf.py build")
