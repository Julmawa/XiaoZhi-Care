from pathlib import Path
from datetime import datetime
import re

ROOT = Path(r"C:\xiaozhi-esp32_prueba3")
OUT = ROOT / f"DP042_INSPECCION_RADIO_{datetime.now().strftime('%Y%m%d-%H%M%S')}.txt"

if not ROOT.exists():
    raise SystemExit(f"ERROR: no existe {ROOT}")

lines = []

def add(title, content=""):
    lines.append("")
    lines.append("=" * 100)
    lines.append(title)
    lines.append("=" * 100)
    if content:
        lines.append(content)

def read_text(path):
    try:
        return path.read_text(encoding="utf-8", errors="ignore")
    except Exception as e:
        return f"[ERROR leyendo {path}: {e}]"

def grep_file(path, patterns, context=5, max_hits=40):
    text = read_text(path)
    src = text.splitlines()
    rx = [re.compile(p, re.I) for p in patterns]
    hits = []
    for i, line in enumerate(src):
        if any(r.search(line) for r in rx):
            start = max(0, i - context)
            end = min(len(src), i + context + 1)
            block = [f"{j+1:5d}: {src[j]}" for j in range(start, end)]
            hits.append("\n".join(block))
            if len(hits) >= max_hits:
                break
    return "\n\n---\n\n".join(hits) if hits else "[sin coincidencias]"

def find_files(base, names):
    found = []
    for name in names:
        found.extend(base.rglob(name))
    # quitar duplicados preservando orden
    out = []
    seen = set()
    for p in found:
        s = str(p).lower()
        if s not in seen:
            seen.add(s)
            out.append(p)
    return out

# -------------------------------------------------------------------------
# 1) Datos generales
# -------------------------------------------------------------------------
add("DP-042 - INSPECCIÓN PREVIA PARA RADIO DE INTERNET")
lines.append(f"Proyecto: {ROOT}")
lines.append("Este script es SOLO LECTURA. No modifica ningún archivo.")

# -------------------------------------------------------------------------
# 2) Dependencia de audio codec
# -------------------------------------------------------------------------
manifest_candidates = [
    ROOT / "main" / "idf_component.yml",
    ROOT / "idf_component.yml",
]
for p in manifest_candidates:
    if p.exists():
        add(f"MANIFEST: {p}")
        lines.append(grep_file(
            p,
            [
                r"esp_audio_codec",
                r"esp_audio_effects",
                r"esp_codec_dev",
                r"esp-sr",
            ],
            context=2,
            max_hits=20,
        ))

# -------------------------------------------------------------------------
# 3) sdkconfig: MP3/AAC/PSRAM
# -------------------------------------------------------------------------
sdkconfigs = [
    ROOT / "sdkconfig",
    ROOT / "build" / "default" / "sdkconfig",
]
for p in sdkconfigs:
    if p.exists():
        add(f"SDKCONFIG: {p}")
        lines.append(grep_file(
            p,
            [
                r"AUDIO_DECODER_MP3_SUPPORT",
                r"AUDIO_DECODER_AAC_SUPPORT",
                r"AUDIO_SIMPLE_DEC",
                r"SPIRAM",
                r"PSRAM",
            ],
            context=0,
            max_hits=80,
        ))

# -------------------------------------------------------------------------
# 4) Headers reales de esp_audio_codec
# -------------------------------------------------------------------------
managed = ROOT / "managed_components"
codec_dirs = []
if managed.exists():
    for p in managed.iterdir():
        if p.is_dir() and "esp_audio_codec" in p.name.lower():
            codec_dirs.append(p)

if codec_dirs:
    for d in codec_dirs:
        add(f"COMPONENTE AUDIO CODEC: {d}")
        headers = find_files(
            d,
            [
                "esp_audio_simple_dec.h",
                "esp_audio_dec.h",
                "esp_mp3_dec.h",
                "esp_aac_dec.h",
            ],
        )
        for h in headers:
            lines.append(f"\n--- {h} ---")
            lines.append(grep_file(
                h,
                [
                    r"ESP_AUDIO_SIMPLE_DEC_TYPE_MP3",
                    r"ESP_AUDIO_SIMPLE_DEC_TYPE_AAC",
                    r"esp_audio_simple_dec_open",
                    r"esp_audio_simple_dec_process",
                    r"esp_audio_simple_dec_get_info",
                    r"esp_audio_simple_dec_close",
                    r"esp_mp3_dec_",
                    r"esp_aac_dec_",
                ],
                context=2,
                max_hits=80,
            ))
else:
    add("COMPONENTE AUDIO CODEC")
    lines.append("[No se encontró managed_components/*esp_audio_codec*]")

# -------------------------------------------------------------------------
# 5) AudioService exacto
# -------------------------------------------------------------------------
audio_cc = ROOT / "main" / "audio" / "audio_service.cc"
audio_h = ROOT / "main" / "audio" / "audio_service.h"

for p in (audio_h, audio_cc):
    if p.exists():
        add(f"AUDIO SERVICE: {p}")
        lines.append(grep_file(
            p,
            [
                r"AudioOutputTask",
                r"PlaySound",
                r"PushPacketToDecodeQueue",
                r"audio_playback_queue_",
                r"codec_->",
                r"OutputData",
                r"SetDecodeSampleRate",
                r"output_sample_rate",
                r"output_channels",
                r"ResetDecoder",
                r"IsPlaybackIdle",
                r"on_playback_drained",
            ],
            context=8,
            max_hits=100,
        ))

# -------------------------------------------------------------------------
# 6) AudioCodec interface y clases derivadas
# -------------------------------------------------------------------------
codec_files = []
main_dir = ROOT / "main"
if main_dir.exists():
    for p in main_dir.rglob("*audio_codec*.h"):
        codec_files.append(p)
    for p in main_dir.rglob("*audio_codec*.cc"):
        codec_files.append(p)

if codec_files:
    for p in codec_files[:30]:
        add(f"AUDIO CODEC: {p}")
        lines.append(grep_file(
            p,
            [
                r"class .*AudioCodec",
                r"OutputData",
                r"InputData",
                r"EnableOutput",
                r"SetOutputVolume",
                r"output_sample_rate",
                r"output_channels",
                r"duplex",
            ],
            context=5,
            max_hits=60,
        ))
else:
    add("AUDIO CODEC")
    lines.append("[No se encontraron archivos *audio_codec*.h/.cc dentro de main]")

# -------------------------------------------------------------------------
# 7) Application state machine
# -------------------------------------------------------------------------
app_cc = ROOT / "main" / "application.cc"
app_h = ROOT / "main" / "application.h"

for p in (app_h, app_cc):
    if p.exists():
        add(f"APPLICATION: {p}")
        lines.append(grep_file(
            p,
            [
                r"kDeviceStateListening",
                r"kDeviceStateSpeaking",
                r"kDeviceStateIdle",
                r"StartListeningAudio",
                r"HandleStateChangedEvent",
                r"ResetDecoder",
                r"PlaySound",
                r"SetAudioVolumeCallbacks",
                r"MAIN_EVENT_PLAYBACK_DRAINED",
            ],
            context=8,
            max_hits=100,
        ))

# -------------------------------------------------------------------------
# 8) CMake actual
# -------------------------------------------------------------------------
for p in [
    ROOT / "main" / "CMakeLists.txt",
    ROOT / "components" / "care-mcp" / "CMakeLists.txt",
    ROOT / "components" / "care-web" / "CMakeLists.txt",
]:
    if p.exists():
        add(f"CMAKE: {p}")
        lines.append(grep_file(
            p,
            [
                r"esp_audio_codec",
                r"esp_audio_effects",
                r"esp_http_client",
                r"REQUIRES",
                r"PRIV_REQUIRES",
            ],
            context=5,
            max_hits=60,
        ))

# -------------------------------------------------------------------------
# 9) HTTP actual
# -------------------------------------------------------------------------
http_files = []
for base in [ROOT / "components", ROOT / "main"]:
    if base.exists():
        for p in base.rglob("*.cc"):
            txt = read_text(p)
            if "esp_http_client" in txt:
                http_files.append(p)

add("USOS ACTUALES DE esp_http_client")
if http_files:
    for p in http_files[:30]:
        lines.append(f"\n--- {p} ---")
        lines.append(grep_file(
            p,
            [
                r"esp_http_client_config_t",
                r"esp_http_client_open",
                r"esp_http_client_read",
                r"esp_http_client_perform",
                r"HTTP_EVENT_ON_DATA",
                r"crt_bundle_attach",
            ],
            context=5,
            max_hits=50,
        ))
else:
    lines.append("[sin usos encontrados]")

# -------------------------------------------------------------------------
# 10) Resumen automático
# -------------------------------------------------------------------------
all_text = "\n".join(lines)

add("RESUMEN AUTOMÁTICO")
checks = [
    ("esp_audio_codec presente", "esp_audio_codec" in all_text),
    ("Simple Decoder presente", "esp_audio_simple_dec_open" in all_text),
    ("MP3 presente", "MP3" in all_text or "mp3" in all_text),
    ("AAC presente", "AAC" in all_text or "aac" in all_text),
    ("AudioOutputTask encontrado", "AudioOutputTask" in all_text),
    ("AudioCodec OutputData encontrado", "OutputData" in all_text),
    ("HTTP client presente", "esp_http_client" in all_text),
    ("PSRAM detectada/configurada", "SPIRAM" in all_text or "PSRAM" in all_text),
]
for label, ok in checks:
    lines.append(f"[{'OK' if ok else 'REVISAR'}] {label}")

lines.append("")
lines.append("Objetivo siguiente si esta inspección confirma los hooks:")
lines.append(" - care-radio separado")
lines.append(" - streaming HTTP/HTTPS MP3 primero")
lines.append(" - decoder oficial esp_audio_codec")
lines.append(" - buffer en PSRAM")
lines.append(" - salida por el AudioCodec existente")
lines.append(" - pausa/mute automática mientras XiaoZhi escucha o habla")
lines.append(" - reanudación al volver a idle")
lines.append(" - control por MCP y por Sistema")

OUT.write_text("\n".join(lines), encoding="utf-8")

print()
print("====================================================")
print(" DP-042 - INSPECCIÓN RADIO COMPLETADA")
print("====================================================")
print()
print("No se modificó ningún archivo del proyecto.")
print()
print("Reporte:")
print(OUT)
print()
print("Pasame ese TXT y preparo la implementación sobre los hooks exactos.")
