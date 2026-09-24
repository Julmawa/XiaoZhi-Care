from pathlib import Path
import re
import sys
import csv
from statistics import mean, median

def usage():
    print("Uso:")
    print(r'  python analizar_latencia_xiaozhi_v3.py ".\latencia_test.txt"')
    raise SystemExit(1)

if len(sys.argv) < 2:
    usage()

log_path = Path(sys.argv[1])
if not log_path.exists():
    raise SystemExit(f"ERROR: no existe {log_path}")

data = log_path.read_bytes()

def decode_log(raw: bytes):
    # BOM explícito
    if raw.startswith(b"\xff\xfe"):
        return raw.decode("utf-16-le", errors="replace"), "UTF-16 LE"
    if raw.startswith(b"\xfe\xff"):
        return raw.decode("utf-16-be", errors="replace"), "UTF-16 BE"
    if raw.startswith(b"\xef\xbb\xbf"):
        return raw.decode("utf-8-sig", errors="replace"), "UTF-8 BOM"

    # PowerShell clásico puede generar UTF-16 sin que el uso posterior lo note.
    sample = raw[:4096]
    if sample:
        null_ratio = sample.count(b"\x00") / len(sample)
        if null_ratio > 0.15:
            try:
                return raw.decode("utf-16-le"), "UTF-16 LE detectado"
            except UnicodeDecodeError:
                pass

    try:
        return raw.decode("utf-8"), "UTF-8"
    except UnicodeDecodeError:
        return raw.decode("cp1252", errors="replace"), "Windows-1252"

text, encoding_used = decode_log(data)

# Eliminar ANSI de idf.py monitor.
ansi_re = re.compile(r'\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])')
text = ansi_re.sub("", text)
lines = text.splitlines()

ts_re = re.compile(r'\b[DIWEV] \((\d+)\) ')

events = []
for lineno, line in enumerate(lines, 1):
    mt = ts_re.search(line)
    if not mt:
        continue

    t = int(mt.group(1))
    kind = None
    extra = {}

    if "DP-039 CARE listening:" in line:
        m = re.search(r'profile=(\w+)\s+silence_ms=(\d+)', line)
        kind = "listen_start"
        if m:
            extra = {"profile": m.group(1), "silence_ms": int(m.group(2))}

    elif "DP-039 end of turn after" in line:
        m = re.search(r'after\s+(\d+)\s+ms.*profile=(\w+)', line)
        kind = "end_turn"
        if m:
            extra = {"wait_ms": int(m.group(1)), "profile": m.group(2)}

    elif "Application: >>" in line:
        kind = "transcript"
        extra = {"text": line.split("Application: >>", 1)[1].strip()}

    elif "Application: <<" in line:
        out = line.split("Application: <<", 1)[1].strip()
        kind = "tool_marker" if out.startswith("%") else "assistant"
        extra = {"text": out}

    elif "CARE_MCP: Tool call:" in line:
        kind = "care_tool"
        extra = {"tool": line.split("CARE_MCP: Tool call:", 1)[1].strip()}

    elif "DP-039 no microphone activity after" in line:
        kind = "no_activity"

    elif "DP-039 PCM activity resumed" in line:
        kind = "resume"

    if kind:
        events.append({"line": lineno, "t": t, "kind": kind, **extra})

transcripts = [e for e in events if e["kind"] == "transcript"]
end_turns = [e for e in events if e["kind"] == "end_turn"]

rows = []
for i, tr in enumerate(transcripts):
    next_tr_t = transcripts[i+1]["t"] if i+1 < len(transcripts) else 10**15

    end = None
    for e in reversed(end_turns):
        if e["t"] <= tr["t"] and tr["t"] - e["t"] <= 20000:
            end = e
            break

    listen = None
    if end:
        for e in reversed(events):
            if e["kind"] == "listen_start" and e["t"] <= end["t"]:
                listen = e
                break

    care_tool = next(
        (e for e in events
         if e["kind"] == "care_tool"
         and tr["t"] <= e["t"] < next_tr_t
         and e["t"] - tr["t"] <= 30000),
        None
    )

    tool_marker = next(
        (e for e in events
         if e["kind"] == "tool_marker"
         and tr["t"] <= e["t"] < next_tr_t
         and e["t"] - tr["t"] <= 30000),
        None
    )

    first_voice = next(
        (e for e in events
         if e["kind"] == "assistant"
         and tr["t"] <= e["t"] < next_tr_t
         and e["t"] - tr["t"] <= 30000),
        None
    )

    tool_event = care_tool or tool_marker
    tool_name = ""
    category = "conversación"
    if care_tool:
        tool_name = care_tool.get("tool", "")
        category = "CARE"
    elif tool_marker:
        tool_name = tool_marker.get("text", "").lstrip("%").strip()
        category = "herramienta"

    def delta(a, b):
        return (b["t"] - a["t"]) if a and b else ""

    rows.append({
        "turno": i + 1,
        "categoria": category,
        "perfil": end.get("profile", "") if end else "",
        "silencio_ms": listen.get("silence_ms", "") if listen else "",
        "texto_usuario": tr.get("text", ""),
        "fin_a_transcripcion_ms": delta(end, tr),
        "transcripcion_a_tool_ms": delta(tr, tool_event),
        "tool": tool_name,
        "tool_a_voz_ms": delta(tool_event, first_voice),
        "transcripcion_a_voz_ms": delta(tr, first_voice),
        "fin_a_voz_ms": delta(end, first_voice),
        "primera_respuesta": first_voice.get("text", "") if first_voice else "",
    })

def numeric(key, pred=None):
    vals = []
    for row in rows:
        if pred and not pred(row):
            continue
        v = row[key]
        if isinstance(v, int):
            vals.append(v)
    return vals

def s(ms):
    return f"{ms/1000:.2f} s"

def show_stats(label, values):
    if not values:
        print(f"{label}: sin datos")
        return
    print(
        f"{label}: promedio {s(mean(values))} | "
        f"mediana {s(median(values))} | "
        f"mín {s(min(values))} | máx {s(max(values))}"
    )

print()
print("=== ANALIZADOR DE LATENCIA XIAOZHI CARE V3 ===")
print("Encoding detectado:", encoding_used)
print("Líneas:", len(lines))
print("Transcripciones:", len(transcripts))
print("Finales DP-039:", len(end_turns))
print("Salidas sin actividad:", sum(e["kind"] == "no_activity" for e in events))
print("Reanudaciones:", sum(e["kind"] == "resume" for e in events))
print()

show_stats("Fin escucha -> transcripción", numeric("fin_a_transcripcion_ms"))
show_stats("Transcripción -> primera voz", numeric("transcripcion_a_voz_ms"))
show_stats("Fin escucha -> primera voz", numeric("fin_a_voz_ms"))
show_stats(
    "CARE: transcripción -> tool",
    numeric("transcripcion_a_tool_ms", lambda r: r["categoria"] == "CARE")
)
show_stats(
    "CARE: tool -> primera voz",
    numeric("tool_a_voz_ms", lambda r: r["categoria"] == "CARE")
)

print()
print("DETALLE")
print("-" * 118)
for r in rows:
    def fs(v):
        return s(v) if isinstance(v, int) else "-"
    print(
        f'{r["turno"]:02d} {r["categoria"]:<12} '
        f'fin→txt {fs(r["fin_a_transcripcion_ms"]):>7} '
        f'txt→tool {fs(r["transcripcion_a_tool_ms"]):>7} '
        f'tool→voz {fs(r["tool_a_voz_ms"]):>7} '
        f'txt→voz {fs(r["transcripcion_a_voz_ms"]):>7} | '
        f'{r["texto_usuario"][:60]}'
    )

csv_path = log_path.with_name(log_path.stem + "_latencia_v3.csv")
fieldnames = [
    "turno","categoria","perfil","silencio_ms","texto_usuario",
    "fin_a_transcripcion_ms","transcripcion_a_tool_ms","tool",
    "tool_a_voz_ms","transcripcion_a_voz_ms","fin_a_voz_ms",
    "primera_respuesta"
]

with csv_path.open("w", newline="", encoding="utf-8-sig") as f:
    w = csv.DictWriter(f, fieldnames=fieldnames)
    w.writeheader()
    w.writerows(rows)

print()
print("CSV generado:")
print(csv_path)
