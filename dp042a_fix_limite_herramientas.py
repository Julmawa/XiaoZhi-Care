from pathlib import Path
from datetime import datetime
import shutil
import re

ROOT = Path(r"C:\xiaozhi-esp32_prueba3")
TARGET = ROOT / "components" / "care-mcp" / "include" / "care_mcp_xiaozhi.h"

if not TARGET.exists():
    raise SystemExit(f"ERROR: no existe {TARGET}")

text = TARGET.read_text(encoding="utf-8")

required_tools = [
    '"care.radio_play"',
    '"care.radio_stop"',
    '"care.radio_pause"',
    '"care.radio_resume"',
    '"care.radio_status"',
]

missing = [name for name in required_tools if name not in text]
if missing:
    if '"care.radio_control"' in text:
        print("El ajuste de herramienta única ya parece aplicado. No se hicieron cambios.")
        raise SystemExit(0)
    raise SystemExit(
        "ERROR: faltan herramientas de radio esperadas:\n  - "
        + "\n  - ".join(missing)
        + "\nNo se modificó ningún archivo."
    )

play_pos = text.find('"care.radio_play"')
start = text.rfind("    // DP042A_RADIO_MP3_BASE", 0, play_pos)
end = text.find("    registered = true;", play_pos)

if start < 0 or end < 0 or start >= end:
    raise SystemExit(
        "ERROR: no pude delimitar de forma segura el bloque DP-042A de radio.\n"
        "No se modificó ningún archivo."
    )

old_block = text[start:end]

if old_block.count("server.AddTool(") != 5:
    raise SystemExit(
        "ERROR: el bloque de radio no contiene exactamente 5 herramientas.\n"
        f"Encontradas: {old_block.count('server.AddTool(')}\n"
        "No se modificó ningún archivo."
    )

new_block = r'''    // DP042A_RADIO_MP3_BASE
    // DP042A_TOOL_LIMIT_FIX
    // Una sola herramienta MCP controla todas las acciones de radio.
    // Esto evita superar el límite global de 32 herramientas del servidor.
    server.AddTool(
        "care.radio_control",
        "RADIO POR INTERNET. Usa esta UNICA herramienta para controlar la radio. "
        "action='play' cuando el usuario diga 'pone la radio', 'prende la radio', "
        "'quiero escuchar la radio' o equivalente; NO repreguntes. "
        "action='stop' para 'para la radio', 'apaga la radio', 'saca la radio'. "
        "action='pause' para 'pausa la radio'. "
        "action='resume' para 'segui con la radio', 'reanuda la radio'. "
        "action='status' para 'que radio esta sonando' o consultar su estado. "
        "La radio se silencia automaticamente mientras XiaoZhi escucha o habla "
        "y continua en vivo al volver a reposo.",
        PropertyList({
            Property("action", kPropertyTypeString, std::string("play")),
        }),
        [](const PropertyList& properties) -> ReturnValue {
            auto& radio =
                xiaozhi_care::radio::RadioService::GetInstance();

            const std::string action =
                properties["action"].value<std::string>();

            ESP_LOGI(
                "CARE_MCP",
                "Tool call: care.radio_control action=%s",
                action.c_str());

            if (action == "play") {
                if (!radio.Play()) {
                    return std::string(
                        "{\"ok\":false,\"action\":\"play\","
                        "\"message\":\"No pude iniciar la radio.\"}");
                }
                return std::string(
                    "{\"ok\":true,\"action\":\"play\","
                    "\"message\":\"Radio iniciada. Responde solo: Listo.\"}");
            }

            if (action == "stop") {
                radio.Stop();
                return std::string(
                    "{\"ok\":true,\"action\":\"stop\","
                    "\"message\":\"Radio detenida. Responde solo: Listo.\"}");
            }

            if (action == "pause") {
                const bool ok = radio.Pause();
                return ok
                    ? std::string(
                        "{\"ok\":true,\"action\":\"pause\","
                        "\"message\":\"Radio pausada.\"}")
                    : std::string(
                        "{\"ok\":false,\"action\":\"pause\","
                        "\"message\":\"La radio no estaba reproduciendo.\"}");
            }

            if (action == "resume") {
                const bool ok = radio.Resume();
                return ok
                    ? std::string(
                        "{\"ok\":true,\"action\":\"resume\","
                        "\"message\":\"Radio reanudada.\"}")
                    : std::string(
                        "{\"ok\":false,\"action\":\"resume\","
                        "\"message\":\"No pude reanudar la radio.\"}");
            }

            if (action == "status") {
                return radio.StatusJson();
            }

            return std::string(
                "{\"ok\":false,\"message\":\"Accion de radio invalida. "
                "Usa play, stop, pause, resume o status.\"}");
        });

'''

text = text[:start] + new_block + text[end:]

# El contador del log debe reflejar la cantidad REAL de AddTool del archivo.
tool_count = text.count("server.AddTool(")
count_pattern = re.compile(r'Registered \d+ XiaoZhi Care MCP tools')

matches = list(count_pattern.finditer(text))
if len(matches) == 1:
    text = count_pattern.sub(
        f"Registered {tool_count} XiaoZhi Care MCP tools",
        text,
        count=1
    )
elif len(matches) > 1:
    raise SystemExit(
        "ERROR: encontré más de un contador de herramientas MCP.\n"
        "No se modificó ningún archivo."
    )

# Validaciones finales antes de escribir.
checks = {
    "herramienta unificada": '"care.radio_control"' in text,
    "acción play": 'action == "play"' in text,
    "acción stop": 'action == "stop"' in text,
    "acción pause": 'action == "pause"' in text,
    "acción resume": 'action == "resume"' in text,
    "acción status": 'action == "status"' in text,
    "sin radio_play": '"care.radio_play"' not in text,
    "sin radio_stop": '"care.radio_stop"' not in text,
    "sin radio_pause": '"care.radio_pause"' not in text,
    "sin radio_resume": '"care.radio_resume"' not in text,
    "sin radio_status": '"care.radio_status"' not in text,
}
bad = [name for name, ok in checks.items() if not ok]
if bad:
    raise SystemExit(
        "ERROR: validación final falló:\n  - "
        + "\n  - ".join(bad)
        + "\nNo se modificó ningún archivo."
    )

stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
backup = TARGET.with_name(
    TARGET.name + f".before-dp042a-tool-limit-fix-{stamp}.bak"
)
shutil.copy2(TARGET, backup)

TARGET.write_text(text, encoding="utf-8")

print()
print("====================================================")
print(" DP-042A - FIX LIMITE DE HERRAMIENTAS MCP")
print("====================================================")
print()
print("Backup:")
print(backup)
print()
print("Antes:")
print("  care.radio_play")
print("  care.radio_stop")
print("  care.radio_pause")
print("  care.radio_resume")
print("  care.radio_status")
print("  = 5 herramientas MCP")
print()
print("Ahora:")
print("  care.radio_control")
print("  action = play | stop | pause | resume | status")
print("  = 1 herramienta MCP")
print()
print(f"Herramientas XiaoZhi Care declaradas ahora: {tool_count}")
print("Se liberaron 4 slots MCP.")
print()
print("No se modificó el motor de radio, MP3, I2S, PSRAM ni AudioService.")
print()
print("Ahora ejecutá:")
print("  idf.py build")
print("  idf.py flash")
