from pathlib import Path
from datetime import datetime
import shutil
import re

ROOT = Path(r"C:\xiaozhi-esp32_prueba3")
APP = ROOT / "main" / "application.cc"
AUDIO = ROOT / "main" / "audio" / "audio_service.cc"
SETTINGS = ROOT / "components" / "care-listening" / "listening_settings.cc"

for p in (APP, AUDIO, SETTINGS):
    if not p.exists():
        raise SystemExit(f"ERROR: no existe {p}")

app = APP.read_text(encoding="utf-8")
audio = AUDIO.read_text(encoding="utf-8")
settings = SETTINGS.read_text(encoding="utf-8")

MARKER = "DP039_DIAGNOSTICO_LIMPIO"

if MARKER in app:
    print("La limpieza de diagnóstico DP-039 ya parece aplicada.")
    raise SystemExit(0)

required_app = [
    "HandleSlowSpeechInputActivity",
    "HandleSlowSpeechTimeout",
    "kCareSlowSpeechInitialWaitUs",
    "kCareSlowSpeechHardLimitUs",
    "SendStopListening()",
]
required_audio = [
    "UpdateInputActivity",
    "input_activity_detected_",
    "callbacks_.on_input_activity",
]
expected_times = [
    "case ListeningProfile::Fast: return 1000;",
    "case ListeningProfile::Medium: return 1500;",
    "case ListeningProfile::Slow: return 2500;",
]

missing = [x for x in required_app if x not in app]
missing += [x for x in required_audio if x not in audio]
missing += [x for x in expected_times if x not in settings]

if missing:
    raise SystemExit(
        "ERROR: el proyecto no coincide con el estado esperado.\nFalta:\n  - "
        + "\n  - ".join(missing)
        + "\nNo se modificó ningún archivo."
    )

changes = []

def remove_regex(text, pattern, label, flags=0):
    new_text, count = re.subn(pattern, "", text, count=1, flags=flags)
    if count:
        changes.append(label)
    return new_text

# AUDIO: quitar sólo trazas de medición/validación.
audio = remove_regex(
    audio,
    r'\n\s*// DP-039 Fase 1B: temporary validation log; one line per VAD transition\.\n'
    r'\s*ESP_LOGI\(TAG,\s*"DP-039 VAD=%s",\s*speaking \? "speech" : "silence"\);',
    "AudioService: log temporal VAD"
)

audio = remove_regex(
    audio,
    r'\n\s*ESP_LOGI\(TAG,\s*"DP-039 PCM=speech level=%lu floor=%lu threshold=%lu",\s*\n'
    r'\s*static_cast<unsigned long>\(level\),\s*\n'
    r'\s*static_cast<unsigned long>\(floor\),\s*\n'
    r'\s*static_cast<unsigned long>\(threshold_on\)\);',
    "AudioService: medición PCM speech"
)

audio = remove_regex(
    audio,
    r'\n\s*ESP_LOGI\(TAG,\s*"DP-039 PCM=silence level=%lu floor=%lu threshold=%lu",\s*\n'
    r'\s*static_cast<unsigned long>\(level\),\s*\n'
    r'\s*static_cast<unsigned long>\(floor\),\s*\n'
    r'\s*static_cast<unsigned long>\(threshold_off\)\);',
    "AudioService: medición PCM silence"
)

# APPLICATION: quitar resumen de perfil/tiempos al iniciar escucha.
app = remove_regex(
    app,
    r'\n\s*auto& settings = xiaozhi_care::listening::ListeningSettings::GetInstance\(\);\n'
    r'\s*settings\.Init\(\);\n'
    r'\s*ESP_LOGI\(TAG,\s*\n'
    r'\s*"DP-039 CARE listening: profile=%s silence_ms=%lu initial_wait_ms=%llu hard_limit_ms=%llu",\s*\n'
    r'\s*settings\.GetProfileName\(\),\s*\n'
    r'\s*static_cast<unsigned long>\(settings\.GetSilenceMs\(\)\),\s*\n'
    r'\s*static_cast<unsigned long long>\(kCareSlowSpeechInitialWaitUs / 1000ULL\),\s*\n'
    r'\s*static_cast<unsigned long long>\(kCareSlowSpeechHardLimitUs / 1000ULL\)\);',
    "Application: resumen de tiempos al iniciar escucha"
)

# Logs de inicio/reanudación PCM.
app = remove_regex(
    app,
    r'\n\s*if \(first_activity\) \{\n'
    r'\s*ESP_LOGI\(TAG,\s*"DP-039 PCM activity detected; sentence started"\);\n'
    r'\s*\} else \{\n'
    r'\s*ESP_LOGI\(TAG,\s*"DP-039 PCM activity resumed; silence timer cancelled"\);\n'
    r'\s*\}',
    "Application: logs inicio/reanudación PCM"
)

# Log de espera de silencio.
app = remove_regex(
    app,
    r'\n\s*ESP_LOGI\(TAG,\s*\n'
    r'\s*"DP-039 PCM silence; profile=%s waiting_ms=%llu",\s*\n'
    r'\s*settings\.GetProfileName\(\),\s*\n'
    r'\s*static_cast<unsigned long long>\(endpoint_us / 1000ULL\)\);',
    "Application: log de espera de silencio"
)

old_timeout = '''    if (!slow_speech_heard_voice_) {
        ESP_LOGI(TAG,
                 "DP-039 no microphone activity after %llu ms; leaving listening state",
                 static_cast<unsigned long long>(kCareSlowSpeechInitialWaitUs / 1000ULL));
    } else if (activity_active) {
        if (slow_speech_hard_deadline_us_ > now_us) {
            const uint64_t remaining_us =
                static_cast<uint64_t>(slow_speech_hard_deadline_us_ - now_us);
            ArmSlowSpeechTimer(remaining_us);
            return;
        }

        ESP_LOGW(TAG,
                 "DP-039 absolute safety limit reached after %llu ms; forcing end of turn",
                 static_cast<unsigned long long>(kCareSlowSpeechHardLimitUs / 1000ULL));
    } else {
        auto& settings = xiaozhi_care::listening::ListeningSettings::GetInstance();
        settings.Init();
        ESP_LOGI(TAG,
                 "DP-039 end of turn after %lu ms of PCM silence profile=%s",
                 static_cast<unsigned long>(settings.GetSilenceMs()),
                 settings.GetProfileName());
    }
'''

new_timeout = '''    if (slow_speech_heard_voice_ && activity_active) {
        if (slow_speech_hard_deadline_us_ > now_us) {
            const uint64_t remaining_us =
                static_cast<uint64_t>(slow_speech_hard_deadline_us_ - now_us);
            ArmSlowSpeechTimer(remaining_us);
            return;
        }

        ESP_LOGW(TAG,
                 "DP-039 absolute safety limit reached after %llu ms; forcing end of turn",
                 static_cast<unsigned long long>(kCareSlowSpeechHardLimitUs / 1000ULL));
    }
'''

if old_timeout in app:
    app = app.replace(old_timeout, new_timeout, 1)
    changes.append("Application: logs de cierre de turno")
else:
    raise SystemExit(
        "ERROR: no encontré el bloque actual de HandleSlowSpeechTimeout esperado.\n"
        "No se modificó ningún archivo."
    )

if "// DP039_CARE_SLOW_SPEECH" in app:
    app = app.replace(
        "// DP039_CARE_SLOW_SPEECH",
        "// DP039_DIAGNOSTICO_LIMPIO: sólo se retiraron trazas de medición.\n"
        "// DP039_CARE_SLOW_SPEECH",
        1
    )

# Validaciones funcionales posteriores.
for token in required_app:
    if token not in app:
        raise SystemExit(f"ERROR: validación falló; desapareció {token}")
for token in required_audio:
    if token not in audio:
        raise SystemExit(f"ERROR: validación falló; desapareció {token}")
for token in expected_times:
    if token not in settings:
        raise SystemExit(f"ERROR: validación falló; cambió un tiempo: {token}")

diagnostic_strings = [
    "DP-039 VAD=%s",
    "DP-039 PCM=speech level=",
    "DP-039 PCM=silence level=",
    "DP-039 CARE listening: profile=",
    "DP-039 PCM activity detected; sentence started",
    "DP-039 PCM activity resumed; silence timer cancelled",
    "DP-039 PCM silence; profile=",
    "DP-039 no microphone activity after",
    "DP-039 end of turn after",
]

remaining = [s for s in diagnostic_strings if s in app or s in audio]
if remaining:
    raise SystemExit(
        "ERROR: quedaron trazas de medición:\n  - "
        + "\n  - ".join(remaining)
        + "\nNo se modificó ningún archivo."
    )

if not changes:
    raise SystemExit(
        "ERROR: no encontré las trazas esperadas. No se modificó ningún archivo."
    )

stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
for p in (APP, AUDIO):
    backup = p.with_name(p.name + f".before-dp039-clean-{stamp}.bak")
    shutil.copy2(p, backup)
    print("Backup:", backup)

APP.write_text(app, encoding="utf-8")
AUDIO.write_text(audio, encoding="utf-8")

print()
print("=== DP-039: DIAGNÓSTICO RETIRADO ===")
print()
for c in changes:
    print(" -", c)

print()
print("SE CONSERVA:")
print(" - detector PCM")
print(" - VAD MODE_1")
print(" - callbacks de actividad")
print(" - temporizador local")
print(" - hard limit de seguridad")
print(" - Rápida 1,0 s / Media 1,5 s / Lenta 2,5 s")
print(" - MCP, alarmas, OLED, LEDs y audio")
print()
print("Los analizadores .py / CSV / logs no se borran; no forman parte del firmware.")
print()
print("Ahora ejecutá:")
print("  idf.py build")
print("  idf.py flash")
