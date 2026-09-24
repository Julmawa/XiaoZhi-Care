from pathlib import Path
import re
import sys
import csv

def usage():
    print("Uso:")
    print(r'  python analizar_dp039_latencia.py "C:\ruta\monitor.txt"')
    raise SystemExit(1)

if len(sys.argv) < 2:
    usage()

log_path = Path(sys.argv[1])
if not log_path.exists():
    raise SystemExit(f"ERROR: no existe {log_path}")

text = log_path.read_text(encoding="utf-8", errors="ignore")
lines = text.splitlines()

ts_re = re.compile(r'^[IWE] \((\d+)\) ')
profile_re = re.compile(r'DP-039 CARE listening: profile=(\w+) silence_ms=(\d+)')
end_re = re.compile(r'DP-039 end of turn after (\d+) ms of PCM silence profile=(\w+)')
pcm_speech_re = re.compile(r'DP-039 PCM=speech level=(\d+) floor=(\d+) threshold=(\d+)')
pcm_silence_re = re.compile(r'DP-039 PCM=silence level=(\d+) floor=(\d+) threshold=(\d+)')

events = []

for idx, line in enumerate(lines):
    m_ts = ts_re.match(line)
    if not m_ts:
        continue
    t = int(m_ts.group(1))

    kind = None
    data = {}

    m = profile_re.search(line)
    if m:
        kind = "listen_start"
        data = {"profile": m.group(1), "silence_ms": int(m.group(2))}
    else:
        m = end_re.search(line)
        if m:
            kind = "end_turn"
            data = {"wait_ms": int(m.group(1)), "profile": m.group(2)}
        elif "Application: >>" in line:
            kind = "transcript"
            data = {"text": line.split("Application: >>", 1)[1].strip()}
        elif "Application: <<" in line:
            kind = "assistant"
            data = {"text": line.split("Application: <<", 1)[1].strip()}
        elif "DP-039 no microphone activity after" in line:
            kind = "no_activity"
        else:
            m = pcm_speech_re.search(line)
            if m:
                kind = "pcm_speech"
                data = {"level": int(m.group(1)), "floor": int(m.group(2)), "threshold": int(m.group(3))}
            else:
                m = pcm_silence_re.search(line)
                if m:
                    kind = "pcm_silence"
                    data = {"level": int(m.group(1)), "floor": int(m.group(2)), "threshold": int(m.group(3))}
                elif "PCM activity resumed; silence timer cancelled" in line:
                    kind = "resume"

    if kind:
        events.append({"idx": idx, "t": t, "kind": kind, **data})

# Emparejar cada fin de turno con la siguiente transcripción (máximo 20 s)
turns = []
end_events = [e for e in events if e["kind"] == "end_turn"]

for n, end in enumerate(end_events, 1):
    next_end_t = end_events[n]["t"] if n < len(end_events) else 10**12

    transcript = next(
        (e for e in events
         if e["kind"] == "transcript"
         and e["t"] >= end["t"]
         and e["t"] < next_end_t
         and e["t"] - end["t"] <= 20000),
        None
    )

    assistant = None
    if transcript:
        assistant = next(
            (e for e in events
             if e["kind"] == "assistant"
             and e["t"] >= transcript["t"]
             and e["t"] - transcript["t"] <= 20000),
            None
        )

    # Buscar último inicio de escucha antes del end
    listen_start = None
    for e in events:
        if e["kind"] == "listen_start" and e["t"] <= end["t"]:
            listen_start = e
        elif e["t"] > end["t"]:
            break

    turns.append({
        "turno": n,
        "perfil": end.get("profile", ""),
        "silencio_config_ms": listen_start.get("silence_ms") if listen_start else "",
        "fin_escucha_ms": end["t"],
        "transcripcion_ms": transcript["t"] if transcript else "",
        "lat_fin_a_trans_ms": (transcript["t"] - end["t"]) if transcript else "",
        "primera_respuesta_ms": assistant["t"] if assistant else "",
        "lat_trans_a_resp_ms": (assistant["t"] - transcript["t"]) if transcript and assistant else "",
        "texto": transcript.get("text", "") if transcript else "",
    })

no_activity = [e for e in events if e["kind"] == "no_activity"]
resumes = [e for e in events if e["kind"] == "resume"]
pcm_speech = [e for e in events if e["kind"] == "pcm_speech"]

lat_remote = [r["lat_fin_a_trans_ms"] for r in turns if isinstance(r["lat_fin_a_trans_ms"], int)]
lat_reply = [r["lat_trans_a_resp_ms"] for r in turns if isinstance(r["lat_trans_a_resp_ms"], int)]

def avg(xs):
    return round(sum(xs)/len(xs), 1) if xs else None

print()
print("=== DIAGNÓSTICO DP-039 ===")
print()
print(f"Finales de turno analizados : {len(turns)}")
print(f"Salidas por falta de voz    : {len(no_activity)}")
print(f"Reanudaciones dentro pausa  : {len(resumes)}")
print(f"Detecciones PCM de voz      : {len(pcm_speech)}")

if lat_remote:
    print()
    print("FIN DE ESCUCHA -> TRANSCRIPCIÓN")
    print(f"  promedio : {avg(lat_remote)/1000:.2f} s")
    print(f"  mínimo   : {min(lat_remote)/1000:.2f} s")
    print(f"  máximo   : {max(lat_remote)/1000:.2f} s")

if lat_reply:
    print()
    print("TRANSCRIPCIÓN -> PRIMERA RESPUESTA")
    print(f"  promedio : {avg(lat_reply)/1000:.2f} s")
    print(f"  mínimo   : {min(lat_reply)/1000:.2f} s")
    print(f"  máximo   : {max(lat_reply)/1000:.2f} s")

print()
print("TURNOS")
print("-" * 100)
for r in turns:
    lat1 = f'{r["lat_fin_a_trans_ms"]/1000:.2f}s' if isinstance(r["lat_fin_a_trans_ms"], int) else "-"
    lat2 = f'{r["lat_trans_a_resp_ms"]/1000:.2f}s' if isinstance(r["lat_trans_a_resp_ms"], int) else "-"
    txt = r["texto"][:65]
    print(f'{r["turno"]:02d}  {r["perfil"]:<7}  fin→texto {lat1:>7}  texto→resp {lat2:>7}  {txt}')

csv_path = log_path.with_name(log_path.stem + "_dp039_analisis.csv")
with csv_path.open("w", newline="", encoding="utf-8-sig") as f:
    writer = csv.DictWriter(f, fieldnames=turns[0].keys() if turns else [
        "turno","perfil","silencio_config_ms","fin_escucha_ms","transcripcion_ms",
        "lat_fin_a_trans_ms","primera_respuesta_ms","lat_trans_a_resp_ms","texto"
    ])
    writer.writeheader()
    writer.writerows(turns)

print()
print("CSV generado:")
print(" ", csv_path)
print()
print("Interpretación rápida:")
print(" - Si fin→texto sigue en varios segundos, la demora principal está después del temporizador local.")
print(" - Si aparecen muchas salidas por falta de voz con TV encendida, el filtro PCM está rechazando bien el fondo.")
print(" - Si aparecen muchas transcripciones ajenas, el ruido/TV todavía está entrando como voz útil.")
