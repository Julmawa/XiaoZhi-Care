from pathlib import Path
from datetime import datetime
from collections import Counter, defaultdict
import hashlib
import re

ROOT = Path(r"C:\xiaozhi-esp32_prueba3")
WEB = ROOT / "components" / "care-web"

FILES = {
    "page": WEB / "care_web_page.cc",
    "data": WEB / "care_web_data.cc",
    "server": WEB / "care_web_server.cc",
    "header": WEB / "include" / "care_web_server.h",
    "cmake": WEB / "CMakeLists.txt",
}

missing = [str(p) for p in FILES.values() if not p.exists()]
if missing:
    raise SystemExit("ERROR: faltan archivos:\n" + "\n".join(missing))

stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
OUT = ROOT / f"DP040D_INSPECCION_WEB_UI_{stamp}.txt"

texts = {k: p.read_text(encoding="utf-8", errors="replace") for k, p in FILES.items()}
page = texts["page"]
server = texts["server"]

def line_of(text, pos):
    return text.count("\n", 0, pos) + 1

def snippet(text, start_line, end_line, pad=3):
    lines = text.splitlines()
    a = max(0, start_line - 1 - pad)
    b = min(len(lines), end_line + pad)
    return "\n".join(f"{i+1:6}: {lines[i]}" for i in range(a, b))

def extract_function_blocks(js_text):
    out = []
    pat = re.compile(r'\b(?:async\s+)?function\s+([A-Za-z_$][\w$]*)\s*\([^)]*\)\s*\{')
    for m in pat.finditer(js_text):
        name = m.group(1)
        brace = js_text.find("{", m.start())
        depth = 0
        quote = None
        esc = False
        i = brace
        while i < len(js_text):
            ch = js_text[i]
            if quote:
                if esc:
                    esc = False
                elif ch == "\\":
                    esc = True
                elif ch == quote:
                    quote = None
            else:
                if ch in ("'", '"', "`"):
                    quote = ch
                elif ch == "{":
                    depth += 1
                elif ch == "}":
                    depth -= 1
                    if depth == 0:
                        out.append((name, m.start(), i + 1, js_text[m.start():i + 1]))
                        break
            i += 1
    return out

def normalize_code(s):
    s = re.sub(r'/\*.*?\*/', '', s, flags=re.S)
    s = re.sub(r'//[^\n]*', '', s)
    s = re.sub(r'\s+', ' ', s).strip()
    return s

def strip_html(s):
    s = re.sub(r'<[^>]+>', ' ', s)
    s = re.sub(r'\s+', ' ', s)
    return s.strip()

def add_section(f, title):
    f.write("\n" + "=" * 100 + "\n")
    f.write(title + "\n")
    f.write("=" * 100 + "\n")

ids = re.findall(r'\bid\s*=\s*["\']([^"\']+)["\']', page)
id_counts = Counter(ids)

func_blocks = extract_function_blocks(page)
func_names = [x[0] for x in func_blocks]
arrow_names = re.findall(
    r'\b(?:const|let|var)\s+([A-Za-z_$][\w$]*)\s*=\s*(?:async\s*)?\([^)]*\)\s*=>',
    page
)
all_declared = func_names + arrow_names
decl_counts = Counter(all_declared)

body_groups = defaultdict(list)
for name, start, end, block in func_blocks:
    norm = normalize_code(block)
    digest = hashlib.sha1(norm.encode("utf-8")).hexdigest()
    body_groups[digest].append((name, start, end, block))
duplicate_body_groups = [v for v in body_groups.values() if len(v) > 1]

usage_candidates = []
for name in sorted(set(all_declared)):
    occurrences = len(re.findall(rf'\b{re.escape(name)}\b', page))
    if occurrences == 1:
        usage_candidates.append(name)

fetches = re.findall(r"fetch\s*\(\s*([`'\"])(.*?)\1", page, flags=re.S)
fetch_urls = [u for _, u in fetches]
fetch_counts = Counter(fetch_urls)

server_uris = re.findall(r'\.uri\s*=\s*"([^"]+)"', server)
server_uri_counts = Counter(server_uris)

tab_tokens = []
for pat in (
    r'data-tab\s*=\s*["\']([^"\']+)["\']',
    r'data-page\s*=\s*["\']([^"\']+)["\']',
    r'data-target\s*=\s*["\']#?([^"\']+)["\']',
):
    tab_tokens.extend(re.findall(pat, page))

nav_calls = re.findall(
    r'onclick\s*=\s*["\'][^"\']*(?:showTab|openTab|setTab|switchTab)\s*\(\s*["\']([^"\']+)["\']',
    page
)

headings = re.findall(r'<h([1-4])[^>]*>(.*?)</h\1>', page, flags=re.I | re.S)
summaries = re.findall(r'<summary[^>]*>(.*?)</summary>', page, flags=re.I | re.S)

css_blocks = re.findall(r'([^{}]+)\{([^{}]*)\}', page, flags=re.S)
selector_counts = Counter()
for sel, body in css_blocks:
    sel_clean = re.sub(r'\s+', ' ', sel).strip()
    if (
        sel_clean
        and not sel_clean.startswith("@")
        and len(sel_clean) < 180
        and any(ch in sel_clean for ch in ".#[]")
    ):
        selector_counts[sel_clean] += 1
duplicate_selectors = [(s, c) for s, c in selector_counts.items() if c > 1]

counts = {
    "buttons": len(re.findall(r'<button\b', page, flags=re.I)),
    "forms": len(re.findall(r'<form\b', page, flags=re.I)),
    "inputs": len(re.findall(r'<input\b', page, flags=re.I)),
    "selects": len(re.findall(r'<select\b', page, flags=re.I)),
    "textareas": len(re.findall(r'<textarea\b', page, flags=re.I)),
    "details": len(re.findall(r'<details\b', page, flags=re.I)),
    "sections": len(re.findall(r'<section\b', page, flags=re.I)),
}

listeners = re.findall(
    r'getElementById\(\s*["\']([^"\']+)["\']\s*\)\.addEventListener\(\s*["\']([^"\']+)["\']',
    page
)
listener_counts = Counter(listeners)

method_blocks = []
for m in re.finditer(r'httpd_uri_t\s+([A-Za-z_]\w*)\s*=\s*\{', server):
    start = m.start()
    end = server.find("};", start)
    if end == -1:
        continue
    block = server[start:end + 2]
    uri = re.search(r'\.uri\s*=\s*"([^"]+)"', block)
    method = re.search(r'\.method\s*=\s*(HTTP_[A-Z]+)', block)
    handler = re.search(r'\.handler\s*=\s*&?([A-Za-z_:]\w*)', block)
    if uri:
        method_blocks.append((
            m.group(1),
            uri.group(1),
            method.group(1) if method else "?",
            handler.group(1) if handler else "?",
            line_of(server, start)
        ))

with OUT.open("w", encoding="utf-8") as f:
    f.write("XIAOZHI CARE - DP-040D INSPECCION WEB / UI\n")
    f.write("=" * 100 + "\n")
    f.write(f"Fecha: {datetime.now().isoformat(timespec='seconds')}\n")
    f.write(f"Proyecto: {ROOT}\n")
    f.write("MODO: SOLO LECTURA - NO MODIFICA NINGUN ARCHIVO\n")

    add_section(f, "1. ARCHIVOS ANALIZADOS")
    for key, p in FILES.items():
        f.write(f"{key:8} {p.stat().st_size/1024:8.1f} KB  {p.relative_to(ROOT)}\n")

    add_section(f, "2. INVENTARIO VISUAL RAPIDO")
    for k, v in counts.items():
        f.write(f"{k:12}: {v}\n")
    f.write(f"IDs HTML    : {len(ids)} ({len(set(ids))} unicos)\n")
    f.write(f"Funciones JS: {len(all_declared)} declaraciones ({len(set(all_declared))} nombres unicos)\n")
    f.write(f"fetch()     : {len(fetch_urls)} llamadas ({len(set(fetch_urls))} URLs literales unicas)\n")
    f.write(f"Rutas server: {len(server_uris)} declaraciones ({len(set(server_uris))} URIs unicas)\n")

    add_section(f, "3. TITULOS, SUBTITULOS Y MENUS DESPLEGABLES")
    f.write("HEADINGS:\n")
    for level, text in headings:
        clean = strip_html(text)
        if clean:
            f.write(f"  H{level}: {clean}\n")
    f.write("\nDETAILS / SUMMARY:\n")
    for text in summaries:
        clean = strip_html(text)
        if clean:
            f.write(f"  - {clean}\n")

    add_section(f, "4. PESTANAS / NAVEGACION DETECTADA")
    tokens = tab_tokens + nav_calls
    if tokens:
        for x, c in Counter(tokens).items():
            f.write(f"{c:3}  {x}\n")
    else:
        f.write("No se detectaron tokens estándar data-tab/showTab.\n")

    nav_kw = re.compile(r'(nav|tab|sidebar|menu|showTab|openTab|switchTab|setTab)', re.I)
    shown = 0
    for m in nav_kw.finditer(page):
        if shown >= 20:
            break
        ln = line_of(page, m.start())
        f.write(f"\n--- navegación cerca de línea {ln} ---\n")
        f.write(snippet(page, ln, ln, pad=5) + "\n")
        shown += 1

    add_section(f, "5. IDs HTML DUPLICADOS")
    dup_ids = [(k, c) for k, c in id_counts.items() if c > 1]
    if not dup_ids:
        f.write("No se detectaron IDs HTML duplicados.\n")
    else:
        for k, c in sorted(dup_ids):
            f.write(f"{c:3}  id={k}\n")

    add_section(f, "6. FUNCIONES JS DUPLICADAS POR NOMBRE")
    dup_names = [(k, c) for k, c in decl_counts.items() if c > 1]
    if not dup_names:
        f.write("No se detectaron nombres de función declarados más de una vez.\n")
    else:
        for k, c in sorted(dup_names):
            f.write(f"{c:3}  {k}\n")
            for name, start, end, block in func_blocks:
                if name == k:
                    f.write(f"      líneas {line_of(page,start)}-{line_of(page,end)}\n")

    add_section(f, "7. FUNCIONES CON CUERPO EXACTAMENTE REPETIDO")
    if not duplicate_body_groups:
        f.write("No se detectaron cuerpos de función exactamente repetidos.\n")
    else:
        for group in duplicate_body_groups:
            f.write("\nGrupo duplicado:\n")
            for name, start, end, block in group:
                f.write(f"  {name}: líneas {line_of(page,start)}-{line_of(page,end)}\n")

    add_section(f, "8. FUNCIONES POSIBLEMENTE NO USADAS (HEURISTICA)")
    f.write("Candidatas cuyo nombre aparece una sola vez en care_web_page.cc.\n")
    f.write("NO implica que sea seguro borrarlas sin revisar.\n\n")
    for name in usage_candidates:
        f.write(f"  {name}\n")

    add_section(f, "9. EVENT LISTENERS DUPLICADOS")
    dups = [(k, c) for k, c in listener_counts.items() if c > 1]
    if not dups:
        f.write("No se detectaron addEventListener duplicados sobre mismo id/evento.\n")
    else:
        for (element_id, event), c in sorted(dups):
            f.write(f"{c:3}  id={element_id} event={event}\n")

    add_section(f, "10. FETCH / ENDPOINTS USADOS POR LA UI")
    for url, c in sorted(fetch_counts.items()):
        f.write(f"{c:3}  {url}\n")

    add_section(f, "11. RUTAS HTTP DEL SERVIDOR")
    for var, uri, method, handler, ln in method_blocks:
        f.write(f"línea {ln:5}: {method:10} {uri:45} -> {handler}\n")
    dup_uri = [(u, c) for u, c in server_uri_counts.items() if c > 1]
    if dup_uri:
        f.write("\nURIs declaradas más de una vez:\n")
        for u, c in dup_uri:
            f.write(f"{c:3}  {u}\n")

    add_section(f, "12. SELECTORES CSS REPETIDOS")
    if not duplicate_selectors:
        f.write("No se detectaron selectores CSS repetidos por texto exacto.\n")
    else:
        for sel, c in sorted(duplicate_selectors):
            f.write(f"{c:3}  {sel}\n")

    add_section(f, "13. MAPA DE FUNCIONES load/render/save/delete")
    for prefix in ("load", "render", "save", "delete", "create", "update", "refresh", "show", "open", "toggle"):
        matches = sorted({n for n in all_declared if n.lower().startswith(prefix)})
        f.write(f"\n{prefix.upper()} ({len(matches)}):\n")
        for n in matches:
            f.write(f"  {n}\n")

    add_section(f, "14. POSIBLES BLOQUES GRANDES DE UI")
    panel_ids = [
        x for x in ids
        if any(k in x.lower() for k in ("tab", "panel", "section", "page", "view", "content"))
    ]
    for x in panel_ids:
        f.write(f"  {x}\n")

    add_section(f, "15. RESUMEN PARA SIGUIENTE PASO")
    f.write(
        "Este informe permite decidir qué consolidar y cómo reorganizar las pestañas.\n"
        "La siguiente etapa debe ser una propuesta de arquitectura visual + un parche único,\n"
        "manteniendo IDs/endpoints/payloads compatibles salvo que se documente lo contrario.\n"
    )

print()
print("DP-040D - INSPECCION WEB/UI GENERADA")
print("====================================")
print(OUT)
print()
print("No se modificó ningún archivo del proyecto.")
print("Subime ese TXT y preparo:")
print("  1) diagnóstico de duplicaciones y funciones sobrantes,")
print("  2) propuesta de pestañas/jerarquía visual,")
print("  3) parche único de reorganización con backup.")
