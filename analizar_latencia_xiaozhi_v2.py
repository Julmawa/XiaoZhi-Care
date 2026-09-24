from pathlib import Path
import re
import sys
import csv
from statistics import mean, median

def usage():
    print("Uso:")
    print(r'  python analizar_latencia_xiaozhi_v2.py ".\latencia_test.txt"')
    raise SystemExit(1)

if len(sys.argv) < 2:
    usage()

log_path = Path(sys.argv[1])

if not log_path.exists():
    raise SystemExit(f"ERROR: no existe {log_path}")

raw = log_path.read_text(encoding="utf-8", errors="ignore")

# Quitar secuencias ANSI comunes para que el parser funcione aunque el log venga de idf.py monitor.
ansi_re = re.compile(r'\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])')
text = ansi_re.sub("", raw)
lines = text.splitlines()

# Busca el timestamp en cualquier parte de la línea, no sólo al inicio.
ts_re = re.compile(r'\b[DIWEV] \((\d+)\) ')

def ts(line):
    m = ts_re.search(line)
    return int(m.group(1)) if m else None

events = []

for i, line in enumerate(lines):
    t = ts(line)
    if t is None:
        continue

    kind = None
    data = {}

    if "DP-039 CARE listening:" in line:
        m = re.search(r'profile=(\w+)\s+silence_ms=(\d+)', line)
        kind = "listen_start"
        if m:
            data = {"profile": m.group(1), "silence_ms": int(m.group(2))}

    elif "DP-039 end of turn after" in line:
        m = re.search(r'after\s+(\d+)\s+ms.*profile=(\w+)', line)
        kind = "end_turn"
        if m:
            data = {"wait_ms": int(m.group(1)), "profile": m.group(2)}

    elif "Application: >>" in line:
        kind = "transcript"
        data = {"text": line.split("Application: >>", 1)[1].strip()}

    elif "Application: <<" in line:
        out = line.split("Application: <<", 1)[1].strip()
        kind = "assistant_tool_marker" if out.startswith("%") else "assistant_spoken"
        data = {"text": out}

    elif "CARE_MCP: Tool call:" in line:
        kind = "care_tool_call"
        data = {"tool": line.split("CARE_MCP: Tool call:", 1)[1].strip()}

    elif re.search(r'\bTool call:', line):
        kind = "tool_call"
        data = {"tool": line.split("Tool call:", 1)[1].strip()}

    elif "DP-039 no microphone activity after" in line:
        kind = "no_activity"

    elif "DP-039 PCM activity resumed" in line:
        kind = "resume"

    elif "DP-039 PCM activity detected; sentence started" in line:
        kind = "sentence_start"

    elif "DP-039 PCM=speech" in line:
        m = re.search(r'level=(\d+)\s+floor=(\d+)\s+threshold=(\d+)', line)
        kind = "pcm_speech"
        if m:
            data = {
                "level": int(m.group(1)),
                "floor": int(m.group(2)),
                "threshold": int(m.group(3)),
            }

    events.append({"line": i + 1, "t": t, "kind": kind, **data})

# ------------------------------------------------------------
# Construir turnos a partir de cada transcripción de usuario.
# ------------------------------------------------------------
transcripts = [e for e in events if e["kind"] == "transcript"]
rows = []

for idx, tr in enumerate(transcripts):
    next_tr_t = transcripts[idx + 1]["t"] if idx + 1 < len(transcripts) else 10**15

    # Fin de turno inmediatamente anterior, si está dentro de 20 s.
    end_turn = None
    for e in reversed(events):
        if e["t"] > tr["t"]:
            continue
        if e["kind"] == "end_turn":
            if tr["t"] - e["t"] <= 20000:
                end_turn = e
            break

    # Inicio de escucha correspondiente.
    listen_start = None
    if end_turn:
        for e in reversed(events):
            if e["t"] > end_turn["t"]:
                continue
            if e["kind"] == "listen_start":
                listen_start = e
                break

    # Tool call después de la transcripción y antes de la siguiente transcripción.
    tool = next(
        (e for e in events
         if e["kind"] in ("care_tool_call", "tool_call")
         and tr["t"] <= e["t"] < next_tr_t
         and e["t"] - tr["t"] <= 30000),
        None
    )

    # Marcador "% tool..." del stream, si aparece.
    tool_marker = next(
        (e for e in events
         if e["kind"] == "assistant_tool_marker"
         and tr["t"] <= e["t"] < next_tr_t
         and e["t"] - tr["t"] <= 30000),
        None
    )

    # Primera salida hablada normal después de la transcripción.
    spoken = next(
        (e for e in events
         if e["kind"] == "assistant_spoken"
         and tr["t"] <= e["t"] < next_tr_t
         and e["t"] - tr["t"] <= 30000),
        None
    )

    end_to_text = (tr["t"] - end_turn["t"]) if end_turn else None
    text_to_tool = (tool["t"] - tr["t"]) if tool else None
    text_to_spoken = (spoken["t"] - tr["t"]) if spoken else None
    end_to_spoken = (spoken["t"] - end_turn["t"]) if (spoken and end_turn) else None
    tool_to_spoken = (spoken["t"] - tool["t"]) if (spoken and tool) else None

    category = "conversación"
    if tool:
        name = tool.get("tool", "")
        if name.startswith("care."):
            category = "CARE"
        else:
            category = "herramienta"
    elif tool_marker:
        category = "herramienta"

    rows.append({
        "turno": idx + 1,
        "categoria": category,
        "perfil": (end_turn or {}).get("profile", ""),
        "silencio_ms": (listen_start or {}).get("silence_ms", ""),
        "texto_usuario": tr.get("text", ""),
        "fin_a_transcripcion_ms": end_to_text if end_to_text is not None else "",
        "transcripcion_a_tool_ms": text_to_tool if text_to_tool is not None else "",
        "tool": (tool or {}).get("tool", ""),
        "tool_a_voz_ms": tool_to_spoken if tool_to_spoken is not None else "",
        "transcripcion_a_voz_ms": text_to_spoken if text_to_spoken is not None else "",
        "fin_a_voz_ms": end_to_spoken if end_to_spoken is not None else "",
        "primera_respuesta": (spoken or {}).get("text", ""),
    })

def vals(key, filt=None):
    out = []
    for r in rows:
        if filt and not filt(r):
            continue
        v = r[key]
        if isinstance(v, int):
            out.append(v)
    return out

def sec(ms):
    return f"{ms/1000:.2f} s"

def summary(label, values):
    if not values:
        print(f"{label}: sin datos")
        return
    print(
        f"{label}: promedio {sec(mean(values))} | "
        f"mediana {sec(median(values))} | "
        f"mín {sec(min(values))} | máx {sec(max(values))}"
    )

print()
print("=== PRUEBA DE LATENCIA XIAOZHI CARE V2 ===")
print(f"Archivo: {log_path}")
print(f"Turnos con transcripción: {len(rows)}")
print(f"Salidas sin actividad PCM: {sum(1 for e in events if e['kind']=='no_activity')}")
print(f"Reanudaciones de frase: {sum(1 for e in events if e['kind']=='resume')}")
print()

summary("Fin de escucha -> transcripción", vals("fin_a_transcripcion_ms"))
summary("Transcripción -> primera voz", vals("transcripcion_a_voz_ms"))
summary("Fin de escucha -> primera voz", vals("fin_a_voz_ms"))

print()
summary(
    "Conversación: transcripción -> voz",
    vals("transcripcion_a_voz_ms", lambda r: r["categoria"] == "conversación")
)
summary(
    "CARE: transcripción -> voz",
    vals("transcripcion_a_voz_ms", lambda r: r["categoria"] == "CARE")
)

care_tool_vals = vals("transcripcion_a_tool_ms", lambda r: r["categoria"] == "CARE")
care_after_tool = vals("tool_a_voz_ms", lambda r: r["categoria"] == "CARE")

if care_tool_vals:
    summary("CARE: transcripción -> llamada MCP", care_tool_vals)
if care_after_tool:
    summary("CARE: llamada MCP -> primera voz", care_after_tool)

print()
print("DETALLE")
print("-" * 118)

for r in rows:
    a = sec(r["fin_a_transcripcion_ms"]) if isinstance(r["fin_a_transcripcion_ms"], int) else "-"
    b = sec(r["transcripcion_a_tool_ms"]) if isinstance(r["transcripcion_a_tool_ms"], int) else "-"
    c = sec(r["tool_a_voz_ms"]) if isinstance(r["tool_a_voz_ms"], int) else "-"
    d = sec(r["transcripcion_a_voz_ms"]) if isinstance(r["transcripcion_a_voz_ms"], int) else "-"
    user_text = r["texto_usuario"].replace("\n", " ")[:50]
    tool_name = r["tool"][:28]

    print(
        f'{r["turno"]:02d} {r["categoria"]:<12} '
        f'fin→txt {a:>7} txt→tool {b:>7} tool→voz {c:>7} txt→voz {d:>7} '
        f'{tool_name:<28} | {user_text}'
    )

csv_path = log_path.with_name(log_path.stem + "_latencia_v2.csv")
fieldnames = [
    "turno","categoria","perfil","silencio_ms","texto_usuario",
    "fin_a_transcripcion_ms","transcripcion_a_tool_ms","tool",
    "tool_a_voz_ms","transcripcion_a_voz_ms","fin_a_voz_ms","primera_respuesta"
]

with csv_path.open("w", newline="", encoding="utf-8-sig") as f:
    writer = csv.DictWriter(f, fieldnames=fieldnames)
    writer.writeheader()
    writer.writerows(rows)

print()
print("CSV generado correctamente:")
print(csv_path)
print()
print("Lectura:")
print(" - fin→transcripción mide red/reconocimiento después de cerrar el turno.")
print(" - transcripción→tool mide cuánto tarda la IA en decidir llamar una herramienta.")
print(" - tool→voz incluye ejecución de herramienta + generación de la respuesta.")
print(" - conversación sin tool sirve como referencia del modelo/servidor.")
