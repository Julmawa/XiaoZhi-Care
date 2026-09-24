from pathlib import Path
from datetime import datetime
import shutil
import re

ROOT = Path(r"C:\xiaozhi-esp32_prueba3")

RADIO_H = ROOT / "components" / "care-radio" / "include" / "care_radio" / "radio_service.h"
RADIO_CC = ROOT / "components" / "care-radio" / "radio_service.cc"
MCP_H = ROOT / "components" / "care-mcp" / "include" / "care_mcp_xiaozhi.h"
WEB_PAGE = ROOT / "components" / "care-web" / "care_web_page.cc"
SDKCONFIG = ROOT / "sdkconfig"

AAC_H = ROOT / "managed_components" / "espressif__esp_audio_codec" / "include" / "decoder" / "impl" / "esp_aac_dec.h"
SIMPLE_H = ROOT / "managed_components" / "espressif__esp_audio_codec" / "include" / "simple_dec" / "esp_audio_simple_dec.h"

FILES = [RADIO_H, RADIO_CC, MCP_H, WEB_PAGE]
for p in FILES + [SDKCONFIG, AAC_H, SIMPLE_H]:
    if not p.exists():
        raise SystemExit(f"ERROR: no existe {p}")

h = RADIO_H.read_text(encoding="utf-8")
cc = RADIO_CC.read_text(encoding="utf-8")
mcp = MCP_H.read_text(encoding="utf-8")
page = WEB_PAGE.read_text(encoding="utf-8")
sdk = SDKCONFIG.read_text(encoding="utf-8", errors="ignore")
aac_header = AAC_H.read_text(encoding="utf-8", errors="ignore")
simple_header = SIMPLE_H.read_text(encoding="utf-8", errors="ignore")

MARKER = "DP043B_RADIO_AAC_VOICE"
if MARKER in h or MARKER in cc or MARKER in mcp:
    print("DP-043B ya parece aplicado. No se hicieron cambios.")
    raise SystemExit(0)

required = {
    "DP043A header": "DP043A_RADIO_STATIONS_WEB" in h,
    "DP043A cc": "DP043A_RADIO_STATIONS_WEB" in cc,
    "DP042B buffer": "DP042B_RADIO_JITTER_BUFFER" in cc,
    "MP3 registrado": "esp_mp3_dec_register();" in cc,
    "MCP unificado": '"care.radio_control"' in mcp,
    "AAC habilitado": "CONFIG_AUDIO_DECODER_AAC_SUPPORT=y" in sdk,
    "AAC register API": "esp_aac_dec_register(void)" in aac_header,
    "AAC+ cfg": "aac_plus_enable" in aac_header,
    "AAC simple": "ESP_AUDIO_SIMPLE_DEC_TYPE_AAC" in simple_header,
    "radio web card": "<h2>Radios de Internet</h2>" in page,
}
bad = [name for name, ok in required.items() if not ok]
if bad:
    raise SystemExit(
        "ERROR: el proyecto no coincide con DP-043A esperado:\n  - "
        + "\n  - ".join(bad)
        + "\nNo se modificó ningún archivo."
    )


def replace_function(text: str, signature: str, replacement: str, label: str) -> str:
    start = text.find(signature)
    if start < 0:
        raise SystemExit(f"ERROR: no encontré {label}.")
    brace = text.find("{", start)
    if brace < 0:
        raise SystemExit(f"ERROR: no encontré apertura de {label}.")
    depth = 0
    in_string = False
    quote = ""
    escaped = False
    i = brace
    while i < len(text):
        ch = text[i]
        if in_string:
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == quote:
                in_string = False
        else:
            if ch in ('"', "'"):
                in_string = True
                quote = ch
            elif ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    return text[:start] + replacement.rstrip() + "\n" + text[i + 1:]
        i += 1
    raise SystemExit(f"ERROR: no pude cerrar {label}.")


def replace_addtool_block(text: str, tool_name: str, replacement: str) -> str:
    pos = text.find(f'"{tool_name}"')
    if pos < 0:
        raise SystemExit(f"ERROR: no encontré tool {tool_name}.")
    start = text.rfind("    server.AddTool(", 0, pos)
    if start < 0:
        raise SystemExit(f"ERROR: no encontré inicio de {tool_name}.")
    paren = text.find("(", start)
    depth = 0
    in_string = False
    quote = ""
    escaped = False
    i = paren
    while i < len(text):
        ch = text[i]
        if in_string:
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == quote:
                in_string = False
        else:
            if ch in ('"', "'"):
                in_string = True
                quote = ch
            elif ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
                if depth == 0:
                    semi = text.find(";", i)
                    if semi < 0:
                        raise SystemExit(f"ERROR: no encontré ; de {tool_name}.")
                    return text[:start] + replacement.rstrip() + "\n" + text[semi + 1:]
        i += 1
    raise SystemExit(f"ERROR: no pude cerrar AddTool {tool_name}.")

# -----------------------------------------------------------------------------
# radio_service.h
# -----------------------------------------------------------------------------
old_api = '''    bool Play();
    void Stop();
    bool Pause();
    bool Resume();
'''
new_api = '''    bool Play();
    // DP043B_RADIO_AAC_VOICE
    bool PlayStation(const std::string& selector);
    bool NextStation();
    std::string StationsJson() const;

    void Stop();
    bool Pause();
    bool Resume();
'''
if old_api not in h:
    raise SystemExit("ERROR: no encontré API Play/Stop actual en radio_service.h.")
h = h.replace(old_api, new_api, 1)

old_stream_decl = "    bool StreamOnce(const std::string& url);\n"
new_stream_decl = "    bool StreamOnce(const std::string& url, uint32_t generation);\n"
if old_stream_decl not in h:
    raise SystemExit("ERROR: no encontré declaración StreamOnce actual.")
h = h.replace(old_stream_decl, new_stream_decl, 1)

old_atomic = '''    std::atomic<bool> streaming_{false};

    // DP042B_RADIO_JITTER_BUFFER
'''
new_atomic = '''    std::atomic<bool> streaming_{false};

    // DP043B_RADIO_AAC_VOICE
    std::atomic<uint32_t> stream_generation_{0};

    // DP042B_RADIO_JITTER_BUFFER
'''
if old_atomic not in h:
    raise SystemExit("ERROR: no encontré streaming_ para agregar generation.")
h = h.replace(old_atomic, new_atomic, 1)

# -----------------------------------------------------------------------------
# includes AAC/cctype/cstdlib
# -----------------------------------------------------------------------------
mp3_include = '#include "decoder/impl/esp_mp3_dec.h"\n'
if mp3_include not in cc:
    raise SystemExit("ERROR: no encontré include MP3.")
if '#include "decoder/impl/esp_aac_dec.h"\n' not in cc:
    cc = cc.replace(mp3_include, mp3_include + '#include "decoder/impl/esp_aac_dec.h"\n', 1)

if "#include <cctype>\n" not in cc:
    cc = cc.replace("#include <algorithm>\n", "#include <algorithm>\n#include <cctype>\n#include <cstdlib>\n", 1)

# -----------------------------------------------------------------------------
# registrar AAC
# -----------------------------------------------------------------------------
register_anchor = '''    ESP_LOGI(kTag, "MP3 decoder registered");

    if (task_handle_ == nullptr) {
'''
register_repl = '''    ESP_LOGI(kTag, "MP3 decoder registered");

    // DP043B_RADIO_AAC_VOICE
    const esp_audio_err_t aac_register_ret =
        esp_aac_dec_register();

    if (aac_register_ret != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(
            kTag,
            "Unable to register AAC decoder: %d",
            static_cast<int>(aac_register_ret));
        return false;
    }

    ESP_LOGI(kTag, "AAC decoder registered");

    if (task_handle_ == nullptr) {
'''
if register_anchor not in cc:
    raise SystemExit("ERROR: no encontré punto de registro MP3.")
cc = cc.replace(register_anchor, register_repl, 1)

# -----------------------------------------------------------------------------
# migrar las dos URLs de página ya usadas en pruebas
# -----------------------------------------------------------------------------
migration_anchor = '''    station_name_ =
        stations_[static_cast<size_t>(default_station_index_)].name;
    station_url_ =
        stations_[static_cast<size_t>(default_station_index_)].url;

    nvs_close(handle);
'''
migration_repl = '''    // DP043B_RADIO_AAC_VOICE
    // Migración puntual de páginas web ya cargadas a streams directos.
    for (auto& station : stations_) {
        if (station.url.find(
                "radios-argentinas.org/fm-aspen-1023") !=
            std::string::npos) {
            station.url =
                "https://playerservices.streamtheworld.com/api/"
                "livestream-redirect/ASPEN.mp3";
            migrated = true;
            ESP_LOGI(kTag, "Migrated Aspen page URL to direct MP3 stream");
        } else if (station.url.find(
                       "radios-argentinas.org/radio-la-red") !=
                   std::string::npos) {
            station.url =
                "https://playerservices.streamtheworld.com/api/"
                "livestream-redirect/LA_RED_AM910AAC.aac";
            migrated = true;
            ESP_LOGI(kTag, "Migrated La Red page URL to direct AAC stream");
        }
    }

    station_name_ =
        stations_[static_cast<size_t>(default_station_index_)].name;
    station_url_ =
        stations_[static_cast<size_t>(default_station_index_)].url;

    nvs_close(handle);
'''
if migration_anchor not in cc:
    raise SystemExit("ERROR: no encontré final de LoadSettings DP043A.")
cc = cc.replace(migration_anchor, migration_repl, 1)

# -----------------------------------------------------------------------------
# Play / selector / next / list / stop
# -----------------------------------------------------------------------------
play_impl = r'''bool RadioService::Play() {
    if (!initialized_.load()) {
        ESP_LOGW(kTag, "Play requested before initialization");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stations_.empty() ||
            default_station_index_ < 0 ||
            default_station_index_ >=
                static_cast<int>(stations_.size())) {
            ESP_LOGW(kTag, "No default radio station configured");
            return false;
        }

        const auto& def =
            stations_[static_cast<size_t>(default_station_index_)];
        station_name_ = def.name;
        station_url_ = def.url;
    }

    ClearPcmBuffer();
    stream_generation_.fetch_add(1);
    user_paused_.store(false);
    wants_playing_.store(true);

    ESP_LOGI(kTag, "Play requested: %s", GetStationName().c_str());
    return true;
}'''
cc = replace_function(cc, "bool RadioService::Play()", play_impl, "RadioService::Play")

stop_anchor = "void RadioService::Stop() {"
if stop_anchor not in cc:
    raise SystemExit("ERROR: no encontré RadioService::Stop.")

selection_impl = r'''// DP043B_RADIO_AAC_VOICE
bool RadioService::PlayStation(const std::string& selector) {
    if (!initialized_.load()) {
        return false;
    }

    auto normalize = [](std::string value) -> std::string {
        std::transform(
            value.begin(), value.end(), value.begin(),
            [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });

        while (!value.empty() &&
               std::isspace(static_cast<unsigned char>(value.front()))) {
            value.erase(value.begin());
        }
        while (!value.empty() &&
               std::isspace(static_cast<unsigned char>(value.back()))) {
            value.pop_back();
        }

        const std::string prefix = "radio ";
        if (value.rfind(prefix, 0) == 0) {
            value.erase(0, prefix.size());
        }
        return value;
    };

    const std::string wanted = normalize(selector);
    if (wanted.empty()) {
        return Play();
    }

    int selected = -1;
    bool numeric = true;
    for (char c : wanted) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            numeric = false;
            break;
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (numeric) {
            const int one_based = std::atoi(wanted.c_str());
            if (one_based >= 1 &&
                one_based <= static_cast<int>(stations_.size())) {
                selected = one_based - 1;
            }
        }

        if (selected < 0) {
            for (size_t i = 0; i < stations_.size(); ++i) {
                if (normalize(stations_[i].name) == wanted) {
                    selected = static_cast<int>(i);
                    break;
                }
            }
        }

        if (selected < 0) {
            for (size_t i = 0; i < stations_.size(); ++i) {
                const std::string candidate = normalize(stations_[i].name);
                if (candidate.find(wanted) != std::string::npos ||
                    wanted.find(candidate) != std::string::npos) {
                    selected = static_cast<int>(i);
                    break;
                }
            }
        }

        if (selected < 0 ||
            selected >= static_cast<int>(stations_.size())) {
            ESP_LOGW(kTag, "Station selector not found: %s", selector.c_str());
            return false;
        }

        station_name_ = stations_[static_cast<size_t>(selected)].name;
        station_url_ = stations_[static_cast<size_t>(selected)].url;
    }

    ClearPcmBuffer();
    stream_generation_.fetch_add(1);
    user_paused_.store(false);
    wants_playing_.store(true);

    ESP_LOGI(
        kTag,
        "Play station selector='%s' -> %s",
        selector.c_str(),
        GetStationName().c_str());
    return true;
}

bool RadioService::NextStation() {
    if (!initialized_.load()) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stations_.empty()) {
            return false;
        }

        int current = -1;
        for (size_t i = 0; i < stations_.size(); ++i) {
            if (stations_[i].name == station_name_ &&
                stations_[i].url == station_url_) {
                current = static_cast<int>(i);
                break;
            }
        }

        const int next =
            (current < 0)
                ? default_station_index_
                : (current + 1) % static_cast<int>(stations_.size());

        station_name_ = stations_[static_cast<size_t>(next)].name;
        station_url_ = stations_[static_cast<size_t>(next)].url;
    }

    ClearPcmBuffer();
    stream_generation_.fetch_add(1);
    user_paused_.store(false);
    wants_playing_.store(true);

    ESP_LOGI(kTag, "Next station: %s", GetStationName().c_str());
    return true;
}

std::string RadioService::StationsJson() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string out =
        "{\"ok\":true,\"count\":" +
        std::to_string(stations_.size()) +
        ",\"default_index\":" +
        std::to_string(default_station_index_ + 1) +
        ",\"stations\":[";

    for (size_t i = 0; i < stations_.size(); ++i) {
        if (i > 0) out += ",";
        out +=
            "{\"index\":" +
            std::to_string(i + 1) +
            ",\"name\":\"" +
            JsonEscape(stations_[i].name) +
            "\",\"is_default\":" +
            std::string(
                static_cast<int>(i) == default_station_index_
                    ? "true" : "false") +
            "}";
    }

    out += "]}";
    return out;
}

'''
cc = cc.replace(stop_anchor, selection_impl + stop_anchor, 1)

stop_impl = r'''void RadioService::Stop() {
    wants_playing_.store(false);
    user_paused_.store(false);
    stream_generation_.fetch_add(1);
    streaming_.store(false);
    ClearPcmBuffer();
    ESP_LOGI(kTag, "Stop requested");
}'''
cc = replace_function(cc, "void RadioService::Stop()", stop_impl, "RadioService::Stop")

# -----------------------------------------------------------------------------
# WorkerTask generation-aware
# -----------------------------------------------------------------------------
worker_impl = r'''void RadioService::WorkerTask() {
    while (true) {
        if (!wants_playing_.load()) {
            streaming_.store(false);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        const uint32_t generation = stream_generation_.load();
        const std::string url = GetStationUrl();

        if (url.empty()) {
            wants_playing_.store(false);
            streaming_.store(false);
            continue;
        }

        const bool ok = StreamOnce(url, generation);
        streaming_.store(false);

        if (wants_playing_.load() &&
            generation == stream_generation_.load()) {
            ESP_LOGW(
                kTag,
                "Stream ended (%s); reconnecting in %d ms",
                ok ? "eof" : "error",
                kReconnectDelayMs);
            vTaskDelay(pdMS_TO_TICKS(kReconnectDelayMs));
        }
    }
}'''
cc = replace_function(cc, "void RadioService::WorkerTask()", worker_impl, "WorkerTask")

# -----------------------------------------------------------------------------
# StreamOnce MP3/AAC + redirects + cancelación
# -----------------------------------------------------------------------------
stream_impl = r'''bool RadioService::StreamOnce(const std::string& url,
                              uint32_t generation) {
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.timeout_ms = kHttpTimeoutMs;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.keep_alive_enable = true;
    config.disable_auto_redirect = false;
    config.max_redirection_count = 4;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        ESP_LOGE(kTag, "esp_http_client_init failed");
        return false;
    }

    esp_http_client_set_header(client, "User-Agent", "XiaoZhi-Care-Radio/0.2");
    esp_http_client_set_header(client, "Accept", "audio/mpeg,audio/aac,audio/aacp,*/*");
    esp_http_client_set_header(client, "Icy-MetaData", "0");

    int status = -1;
    bool opened = false;

    for (int redirect = 0; redirect <= 4; ++redirect) {
        const esp_err_t open_err = esp_http_client_open(client, 0);
        if (open_err != ESP_OK) {
            ESP_LOGW(kTag, "HTTP open failed: %s", esp_err_to_name(open_err));
            esp_http_client_cleanup(client);
            return false;
        }

        opened = true;
        (void)esp_http_client_fetch_headers(client);
        status = esp_http_client_get_status_code(client);

        if (status >= 300 && status < 400) {
            if (redirect >= 4) {
                ESP_LOGW(kTag, "Radio redirect limit reached status=%d", status);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return false;
            }

            const esp_err_t redirect_err = esp_http_client_set_redirection(client);
            esp_http_client_close(client);
            opened = false;

            if (redirect_err != ESP_OK) {
                ESP_LOGW(kTag, "Radio redirect failed: %s", esp_err_to_name(redirect_err));
                esp_http_client_cleanup(client);
                return false;
            }

            ESP_LOGI(kTag, "Following radio HTTP redirect status=%d", status);
            continue;
        }

        break;
    }

    if (status != 200) {
        ESP_LOGW(kTag, "Radio HTTP status=%d", status);
        if (opened) esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    if (!wants_playing_.load() ||
        generation != stream_generation_.load()) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return true;
    }

    std::string lower_url = url;
    std::transform(
        lower_url.begin(), lower_url.end(), lower_url.begin(),
        [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

    const bool is_aac =
        lower_url.find(".aac") != std::string::npos ||
        lower_url.find("aacp") != std::string::npos;

    esp_audio_simple_dec_cfg_t dec_cfg = {};
    esp_aac_dec_cfg_t aac_cfg = ESP_AAC_DEC_CONFIG_DEFAULT();

    if (is_aac) {
        aac_cfg.aac_plus_enable = true;
        aac_cfg.no_adts_header = false;
        dec_cfg.dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_AAC;
        dec_cfg.dec_cfg = &aac_cfg;
        dec_cfg.cfg_size = sizeof(aac_cfg);
    } else {
        dec_cfg.dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
        dec_cfg.dec_cfg = nullptr;
        dec_cfg.cfg_size = 0;
    }
    dec_cfg.use_frame_dec = false;

    esp_audio_simple_dec_handle_t decoder = nullptr;
    const esp_audio_err_t open_ret = esp_audio_simple_dec_open(&dec_cfg, &decoder);

    if (open_ret != ESP_AUDIO_ERR_OK || decoder == nullptr) {
        ESP_LOGE(
            kTag,
            "%s decoder open failed: %d",
            is_aac ? "AAC" : "MP3",
            static_cast<int>(open_ret));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    if (!wants_playing_.load() ||
        generation != stream_generation_.load()) {
        esp_audio_simple_dec_close(decoder);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return true;
    }

    std::vector<uint8_t> http_buffer(kHttpBufferBytes);
    std::vector<uint8_t> pcm_buffer(kPcmBufferBytes);

    streaming_.store(true);
    ESP_LOGI(
        kTag,
        "Streaming %s: %s",
        is_aac ? "AAC/AAC+" : "MP3",
        GetStationName().c_str());

    bool result = true;

    while (wants_playing_.load() &&
           generation == stream_generation_.load()) {
        const int received = esp_http_client_read(
            client,
            reinterpret_cast<char*>(http_buffer.data()),
            static_cast<int>(http_buffer.size()));

        if (received < 0) {
            ESP_LOGW(kTag, "HTTP read error=%d", received);
            result = false;
            break;
        }
        if (received == 0) {
            result = true;
            break;
        }

        size_t offset = 0;
        while (offset < static_cast<size_t>(received) &&
               wants_playing_.load() &&
               generation == stream_generation_.load()) {
            esp_audio_simple_dec_raw_t raw = {};
            raw.buffer = http_buffer.data() + offset;
            raw.len = static_cast<uint32_t>(
                static_cast<size_t>(received) - offset);
            raw.eos = false;
            raw.consumed = 0;

            esp_audio_simple_dec_out_t out = {};
            out.buffer = pcm_buffer.data();
            out.len = static_cast<uint32_t>(pcm_buffer.size());
            out.needed_size = 0;
            out.decoded_size = 0;

            esp_audio_err_t ret = esp_audio_simple_dec_process(decoder, &raw, &out);

            if (ret != ESP_AUDIO_ERR_OK &&
                out.needed_size > pcm_buffer.size()) {
                pcm_buffer.resize(out.needed_size);

                raw.buffer = http_buffer.data() + offset;
                raw.len = static_cast<uint32_t>(
                    static_cast<size_t>(received) - offset);
                raw.eos = false;
                raw.consumed = 0;

                out.buffer = pcm_buffer.data();
                out.len = static_cast<uint32_t>(pcm_buffer.size());
                out.needed_size = 0;
                out.decoded_size = 0;

                ret = esp_audio_simple_dec_process(decoder, &raw, &out);
            }

            if (raw.consumed > 0) offset += raw.consumed;

            if (ret != ESP_AUDIO_ERR_OK) {
                if (raw.consumed == 0) break;
                continue;
            }

            if (out.decoded_size > 0) {
                esp_audio_simple_dec_info_t info = {};
                const esp_audio_err_t info_ret =
                    esp_audio_simple_dec_get_info(decoder, &info);

                if (info_ret == ESP_AUDIO_ERR_OK &&
                    info.bits_per_sample == 16 &&
                    out.decoded_size >= sizeof(int16_t)) {
                    if (!system_paused_.load() &&
                        !user_paused_.load()) {
                        const size_t samples =
                            out.decoded_size / sizeof(int16_t);

                        std::vector<int16_t> pcm(samples);
                        std::memcpy(
                            pcm.data(),
                            out.buffer,
                            samples * sizeof(int16_t));

                        EnqueuePcm(
                            std::move(pcm),
                            static_cast<int>(info.sample_rate),
                            static_cast<int>(info.channel));
                    }
                }
            }

            if (raw.consumed == 0 && out.decoded_size == 0) break;
        }
    }

    streaming_.store(false);
    esp_audio_simple_dec_close(decoder);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return result;
}'''
cc = replace_function(cc, "bool RadioService::StreamOnce(", stream_impl, "StreamOnce")

# -----------------------------------------------------------------------------
# MCP radio_control extendido, manteniendo UNA sola tool
# -----------------------------------------------------------------------------
new_radio_tool = r'''    // DP042A_RADIO_MP3_BASE
    // DP042A_TOOL_LIMIT_FIX
    // DP043B_RADIO_AAC_VOICE
    server.AddTool(
        "care.radio_control",
        "RADIO POR INTERNET. Usa esta UNICA herramienta para TODA orden o "
        "consulta sobre radio. action='play' para poner/prender/escuchar radio. "
        "Si el usuario nombra una emisora, pasa station con ese nombre: "
        "'pone Radio Aspen' -> action='play', station='Radio Aspen'; "
        "'pone La Red' -> action='play', station='La Red'. "
        "Para 'pone la segunda radio', action='play', station='2'. "
        "Si no nombra emisora, station='' y se usa la predeterminada. "
        "action='next' para 'cambia de emisora', 'pone otra radio' o "
        "'siguiente emisora'. action='list' para 'que radios tengo', "
        "'que emisoras hay' o 'decime las radios guardadas'. "
        "action='stop' para apagar/parar/sacar la radio. "
        "action='pause' para pausar. action='resume' para continuar. "
        "action='status' para saber que radio esta sonando. "
        "IMPORTANTE: una orden de prender, apagar, cambiar o escuchar radio "
        "NUNCA es un recordatorio y NO debe usar care.add_todo_reminder. "
        "Despues de play/next/status usa el campo station devuelto; "
        "nunca inventes el nombre de la emisora.",
        PropertyList({
            Property("action", kPropertyTypeString, std::string("play")),
            Property("station", kPropertyTypeString, std::string("")),
        }),
        [](const PropertyList& properties) -> ReturnValue {
            auto& radio =
                xiaozhi_care::radio::RadioService::GetInstance();

            const std::string action =
                properties["action"].value<std::string>();
            const std::string station =
                properties["station"].value<std::string>();

            ESP_LOGI(
                "CARE_MCP",
                "Tool call: care.radio_control action=%s station=%s",
                action.c_str(),
                station.c_str());

            if (action == "play") {
                const bool ok = station.empty()
                    ? radio.Play()
                    : radio.PlayStation(station);

                if (!ok) {
                    return std::string(
                        "{\"ok\":false,"
                        "\"message\":\"No encontre esa emisora. Usa action=list para consultar las radios guardadas.\"}");
                }
                return radio.StatusJson();
            }

            if (action == "next") {
                if (!radio.NextStation()) {
                    return std::string(
                        "{\"ok\":false,\"message\":\"No pude cambiar de emisora.\"}");
                }
                return radio.StatusJson();
            }

            if (action == "list") {
                return radio.StationsJson();
            }

            if (action == "stop") {
                radio.Stop();
                return std::string(
                    "{\"ok\":true,\"state\":\"stopped\",\"message\":\"Radio apagada.\"}");
            }

            if (action == "pause") {
                const bool ok = radio.Pause();
                return ok
                    ? std::string("{\"ok\":true,\"state\":\"paused\",\"message\":\"Radio pausada.\"}")
                    : std::string("{\"ok\":false,\"message\":\"La radio no estaba reproduciendo.\"}");
            }

            if (action == "resume") {
                const bool ok = radio.Resume();
                return ok
                    ? radio.StatusJson()
                    : std::string("{\"ok\":false,\"message\":\"No pude reanudar la radio.\"}");
            }

            if (action == "status") {
                return radio.StatusJson();
            }

            return std::string(
                "{\"ok\":false,\"message\":\"Accion invalida. Usa play, next, list, stop, pause, resume o status.\"}");
        });'''

mcp = replace_addtool_block(mcp, "care.radio_control", new_radio_tool)

# -----------------------------------------------------------------------------
# blindaje add_todo_reminder: órdenes de radio no son recordatorios
# -----------------------------------------------------------------------------
todo_pos = mcp.find('"care.add_todo_reminder"')
if todo_pos < 0:
    raise SystemExit("ERROR: no encontré care.add_todo_reminder.")

todo_start = mcp.rfind("    server.AddTool(", 0, todo_pos)
todo_prop = mcp.find("        PropertyList({", todo_pos)
if todo_start < 0 or todo_prop < 0:
    raise SystemExit("ERROR: no pude delimitar care.add_todo_reminder.")

todo_prefix = mcp[todo_start:todo_prop]
last_quote_comma = todo_prefix.rfind('",')
if last_quote_comma < 0:
    raise SystemExit("ERROR: no encontré final de descripción de add_todo_reminder.")

radio_exclusion = (
    " EXCEPCION IMPORTANTE: no uses esta herramienta para ordenes de radio "
    "como prender, poner, escuchar, cambiar, pausar, continuar o apagar la "
    "radio, aunque la frase contenga 'quiero', 'tenes que' o 'prender'. "
    "Esas ordenes usan exclusivamente care.radio_control."
)

todo_prefix = (
    todo_prefix[:last_quote_comma]
    + radio_exclusion
    + todo_prefix[last_quote_comma:]
)
mcp = mcp[:todo_start] + todo_prefix + mcp[todo_prop:]

# -----------------------------------------------------------------------------
# UI: aclarar URL directa y AAC
# -----------------------------------------------------------------------------
page = page.replace(
    "Usá una URL directa de streaming MP3.",
    "Usá una URL directa de streaming MP3 o AAC/AAC+. No pegues la dirección de una página web de la radio.",
    1,
)
page = page.replace(
    'placeholder="https://servidor/radio.mp3"',
    'placeholder="https://servidor/stream.mp3 o .aac"',
    1,
)

checks = {
    "AAC include": '#include "decoder/impl/esp_aac_dec.h"' in cc,
    "AAC register": "esp_aac_dec_register();" in cc,
    "AAC decoder": "ESP_AUDIO_SIMPLE_DEC_TYPE_AAC" in cc,
    "AAC+": "aac_plus_enable = true" in cc,
    "redirect": "esp_http_client_set_redirection" in cc,
    "generation header": "stream_generation_" in h,
    "generation cc": "stream_generation_" in cc,
    "play named": "PlayStation(const std::string& selector)" in h,
    "next": "bool NextStation()" in h,
    "list": "StationsJson() const" in h,
    "MCP station": 'Property("station", kPropertyTypeString' in mcp,
    "MCP next": 'action == "next"' in mcp,
    "MCP list": 'action == "list"' in mcp,
    "todo protected": "Esas ordenes usan exclusivamente care.radio_control" in mcp,
    "UI AAC": "MP3 o AAC/AAC+" in page,
    "Aspen migration": "ASPEN.mp3" in cc,
    "La Red migration": "LA_RED_AM910AAC.aac" in cc,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit(
        "ERROR: validación final falló:\n  - "
        + "\n  - ".join(failed)
        + "\nNo se modificó ningún archivo."
    )

stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
for p in FILES:
    backup = p.with_name(p.name + f".before-dp043b-radio-aac-voice-{stamp}.bak")
    shutil.copy2(p, backup)
    print("Backup:", backup)

RADIO_H.write_text(h, encoding="utf-8")
RADIO_CC.write_text(cc, encoding="utf-8")
MCP_H.write_text(mcp, encoding="utf-8")
WEB_PAGE.write_text(page, encoding="utf-8")

print()
print("====================================================")
print(" DP-043B - AAC + SELECCION DE RADIO POR VOZ")
print("====================================================")
print()
print("Agregado:")
print(" - MP3 + AAC/AAC+ automático según URL")
print(" - redirects HTTP de streams")
print(" - selección por nombre")
print(" - selección por número")
print(" - siguiente emisora")
print(" - listado de emisoras")
print(" - cancelación del stream anterior al cambiar/stop")
print(" - radio excluida de care.add_todo_reminder")
print(" - migración Aspen/La Red desde páginas web a streams directos")
print()
print("Frases para probar:")
print(" - Poné Radio Aspen")
print(" - Poné La Red")
print(" - Cambiá de emisora")
print(" - Poné la segunda radio")
print(" - ¿Qué radios tengo guardadas?")
print(" - Apagá la radio")
print()
print("Ahora ejecutá:")
print("  idf.py build")
