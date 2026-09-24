from pathlib import Path
from datetime import datetime
import shutil

ROOT = Path(r"C:\xiaozhi-esp32_prueba3")
SRC = ROOT / "components" / "care-web" / "care_web_page.cc"

if not SRC.exists():
    raise SystemExit(f"ERROR: no existe {SRC}")

text = SRC.read_text(encoding="utf-8")
MARKER = "DP040D_SYSTEM_ORDER_1_TO_7"

if MARKER in text:
    print("El orden 1..7 de Sistema ya parece aplicado. No se hicieron cambios.")
    raise SystemExit(0)

if "DP040D_FASE1_NAV_VISUAL" not in text:
    raise SystemExit(
        "ERROR: no encuentro DP-040D Fase 1 en care_web_page.cc.\n"
        "No se modificó ningún archivo."
    )

# Confirmar que están todos los bloques funcionales antes de tocar nada.
required_headings = [
    "Velocidad de escucha",
    "Volumen del parlante",
    "Agregar audio",
    "Audios asignados",
    "Limpieza segura",
    "Backup completo",
    "Uso de XiaoZhi Care",
    "Estado de memoria",
]
missing = [h for h in required_headings if f"<h2>{h}</h2>" not in text]
if missing:
    raise SystemExit(
        "ERROR: faltan bloques esperados en Sistema:\n  - "
        + "\n  - ".join(missing)
        + "\nNo se modificó ningún archivo."
    )

css = r'''
/* DP040D_SYSTEM_ORDER_1_TO_7 */
#section-maintenance .systemOrderedGrid{
  display:grid;
  grid-template-columns:minmax(320px,.82fr) minmax(0,1.18fr);
  gap:16px;
  align-items:start;
  margin-top:18px;
}
#section-maintenance .systemOrderedGrid > .card{
  position:relative;
  margin:0!important;
  min-width:0;
}
#section-maintenance .systemOrderedGrid > .card::after{
  content:attr(data-system-order);
  position:absolute;
  top:14px;
  right:16px;
  min-width:27px;
  height:27px;
  padding:0 7px;
  display:flex;
  align-items:center;
  justify-content:center;
  border-radius:9px;
  background:#edf5ff;
  border:1px solid #d6e7fa;
  color:#245b9b;
  font-size:11px;
  font-weight:950;
}
#section-maintenance .systemOrderedGrid > .card .panelTitle{
  padding-right:42px;
}
#section-maintenance .systemUsageCompact{
  grid-column:1/-1;
  padding:14px 16px!important;
}
#section-maintenance .systemUsageCompact .panelTitle{
  margin-bottom:7px;
}
#section-maintenance .systemUsageCompact .panelTitle h2{
  font-size:19px;
}
#section-maintenance .systemUsageCompact #maintenanceList{
  display:grid;
  grid-template-columns:repeat(4,minmax(0,1fr));
  gap:8px;
}
#section-maintenance .systemUsageCompact #maintenanceList .item{
  padding:9px 11px;
  min-height:0;
  border-radius:13px;
}
#section-maintenance .systemUsageCompact #maintenanceList .itemName{
  font-size:13px;
}
#section-maintenance .systemUsageCompact #maintenanceList .itemMeta,
#section-maintenance .systemUsageCompact #maintenanceList .itemNotes{
  font-size:11px;
  line-height:1.25;
}
#section-maintenance .systemNvsCompact{
  margin-top:9px;
  padding-top:9px;
  border-top:1px solid #e4edf7;
}
#section-maintenance .systemNvsCompact summary{
  cursor:pointer;
  color:#4f6485;
  font-size:12px;
  font-weight:850;
}
#section-maintenance .systemNvsCompact .actions{
  margin-top:9px;
}
#section-maintenance .systemNvsCompact #maintenanceNvs{
  margin-top:8px!important;
}
#section-maintenance .systemAudioNotice{
  margin-top:14px;
  margin-bottom:0;
}
@media(max-width:900px){
  #section-maintenance .systemOrderedGrid{
    grid-template-columns:1fr;
  }
  #section-maintenance .systemUsageCompact{
    grid-column:auto;
  }
  #section-maintenance .systemUsageCompact #maintenanceList{
    grid-template-columns:repeat(2,minmax(0,1fr));
  }
}
@media(max-width:520px){
  #section-maintenance .systemUsageCompact #maintenanceList{
    grid-template-columns:1fr;
  }
}
'''

style_end = text.find("</style>")
if style_end < 0:
    raise SystemExit("ERROR: no encontré </style>.")
text = text[:style_end] + css + "\n" + text[style_end:]

js = r'''
// DP040D_SYSTEM_ORDER_1_TO_7
function dp040dOrganizeSystemOrder(){
  const section=document.getElementById('section-maintenance');
  if(!section || section.querySelector('.systemOrderedGrid')) return;

  const cards=[...section.querySelectorAll('.card')];
  const cardByTitle=(title)=>cards.find(card=>{
    const h=card.querySelector('h2');
    return h && h.textContent.trim()===title;
  });

  const orderedTitles=[
    'Velocidad de escucha',
    'Volumen del parlante',
    'Agregar audio',
    'Audios asignados',
    'Limpieza segura',
    'Backup completo',
    'Uso de XiaoZhi Care'
  ];

  const orderedCards=orderedTitles.map(cardByTitle);
  const memoryCard=cardByTitle('Estado de memoria');

  if(orderedCards.some(x=>!x) || !memoryCard){
    console.warn('DP-040D: no se pudo organizar Sistema; falta algún bloque esperado.');
    return;
  }

  const firstGrid=section.querySelector('.grid');
  if(!firstGrid) return;

  const grid=document.createElement('div');
  grid.className='systemOrderedGrid';

  orderedCards.forEach((card,index)=>{
    card.dataset.systemOrder=String(index+1);
    if(orderedTitles[index]==='Uso de XiaoZhi Care'){
      card.classList.add('systemUsageCompact');
    }
    grid.appendChild(card);
  });

  section.insertBefore(grid,firstGrid);

  // "Estado de memoria" deja de ocupar una tarjeta propia.
  // Sus controles se conservan dentro de Uso de XiaoZhi Care.
  const usage=orderedCards[6];
  const nvsDetails=document.createElement('details');
  nvsDetails.className='systemNvsCompact';
  const summary=document.createElement('summary');
  summary.textContent='Estado interno y memoria';
  nvsDetails.appendChild(summary);

  const refresh=memoryCard.querySelector('#maintenanceRefresh');
  const nvs=memoryCard.querySelector('#maintenanceNvs');
  const actions=refresh ? refresh.closest('.actions') : null;

  if(actions) nvsDetails.appendChild(actions);
  else if(refresh) nvsDetails.appendChild(refresh);

  if(nvs) nvsDetails.appendChild(nvs);
  usage.appendChild(nvsDetails);
  memoryCard.remove();

  // Mantener el aviso de audio, pero integrado visualmente con Sistema.
  const notices=[...section.querySelectorAll('.notice')];
  const audioNotice=notices.find(n=>
    (n.textContent||'').includes('Audios de recordatorios y pastillero')
  );
  if(audioNotice){
    audioNotice.classList.add('systemAudioNotice');
    grid.parentNode.insertBefore(audioNotice,grid);
  }

  // Ocultar contenedores viejos que quedaron sin tarjetas.
  [...section.querySelectorAll('.grid')].forEach(oldGrid=>{
    if(oldGrid!==grid && !oldGrid.querySelector('.card')){
      oldGrid.style.display='none';
    }
  });

  ['maintenanceAudioArea','maintenanceLowerArea'].forEach(id=>{
    const area=document.getElementById(id);
    if(area && !area.querySelector('.card')) area.style.display='none';
  });
}

if(document.readyState==='loading'){
  document.addEventListener('DOMContentLoaded',dp040dOrganizeSystemOrder);
}else{
  dp040dOrganizeSystemOrder();
}
'''

script_end = text.rfind("</script>")
if script_end < 0:
    raise SystemExit("ERROR: no encontré </script>.")
text = text[:script_end] + js + "\n" + text[script_end:]

# Validaciones estáticas.
required_ids = [
    'saveListeningProfileBtn',
    'saveAudioVolumeBtn',
    'recordingForm',
    'recordingsList',
    'clearExecutionsBtn',
    'clearLegacyPillboxBtn',
    'fullBackupDownload',
    'maintenanceCount',
    'maintenanceList',
    'maintenanceRefresh',
    'maintenanceNvs',
]
missing_ids = [x for x in required_ids if f'id="{x}"' not in text]
if missing_ids:
    raise SystemExit(
        "ERROR: faltan IDs funcionales después del parche:\n  - "
        + "\n  - ".join(missing_ids)
    )

checks = {
    "marcador": MARKER in text,
    "orden exacto": "const orderedTitles=[" in text,
    "uso compacto": "systemUsageCompact" in text,
    "estado memoria preservado": "maintenanceNvs" in text and "maintenanceRefresh" in text,
    "persona preservada": "Familia y otros" in text and "Perfil principal" in text,
}
failed = [k for k,v in checks.items() if not v]
if failed:
    raise SystemExit(
        "ERROR: validación final falló: " + ", ".join(failed)
    )

stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
backup = SRC.with_name(SRC.name + f".before-dp040d-system-order-{stamp}.bak")
shutil.copy2(SRC, backup)
SRC.write_text(text, encoding="utf-8")

print()
print("DP-040D - ORDEN DE SISTEMA APLICADO")
print("==================================")
print("Backup:")
print(" ", backup)
print()
print("Orden visual:")
print("  1. Velocidad de escucha")
print("  2. Volumen del parlante")
print("  3. Agregar audio")
print("  4. Audios asignados")
print("  5. Limpieza segura")
print("  6. Backup completo")
print("  7. Uso de XiaoZhi Care")
print()
print("Estado de memoria:")
print("  - deja de ser una tarjeta separada")
print("  - se conserva dentro de 'Uso de XiaoZhi Care'")
print("  - aparece como desplegable 'Estado interno y memoria'")
print()
print("No se tocaron:")
print("  - endpoints")
print("  - IDs funcionales")
print("  - backend")
print("  - datos guardados")
print()
print("Ahora ejecutá:")
print("  idf.py build")
print("  idf.py flash")
