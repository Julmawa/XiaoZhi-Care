from pathlib import Path
from datetime import datetime
import shutil
import re

ROOT = Path(r"C:\xiaozhi-esp32_prueba3")

MAIN_CMAKE = ROOT / "main" / "CMakeLists.txt"
APP_CC = ROOT / "main" / "application.cc"
AUDIO_H = ROOT / "main" / "audio" / "audio_service.h"
AUDIO_CC = ROOT / "main" / "audio" / "audio_service.cc"
SDKCONFIG = ROOT / "sdkconfig"

MCP_DIR = ROOT / "components" / "care-mcp"
MCP_HOOK = MCP_DIR / "include" / "care_mcp_xiaozhi.h"
MCP_CMAKE = MCP_DIR / "CMakeLists.txt"

RADIO_DIR = ROOT / "components" / "care-radio"

FILES = [MAIN_CMAKE, APP_CC, AUDIO_H, AUDIO_CC, SDKCONFIG, MCP_HOOK, MCP_CMAKE]
for p in FILES:
    if not p.exists():
        raise SystemExit(f"ERROR: no existe {p}")

if RADIO_DIR.exists():
    raise SystemExit(
        f"ERROR: ya existe {RADIO_DIR}\n"
        "No se modificó ningún archivo."
    )

main_cmake = MAIN_CMAKE.read_text(encoding="utf-8")
app = APP_CC.read_text(encoding="utf-8")
audio_h = AUDIO_H.read_text(encoding="utf-8")
audio_cc = AUDIO_CC.read_text(encoding="utf-8")
sdkconfig = SDKCONFIG.read_text(encoding="utf-8", errors="ignore")
mcp_hook = MCP_HOOK.read_text(encoding="utf-8")
mcp_cmake = MCP_CMAKE.read_text(encoding="utf-8")

MARKER = "DP042A_RADIO_MP3_BASE"
if any(MARKER in t for t in (app, audio_h, audio_cc, mcp_hook)):
    raise SystemExit("DP-042A ya parece aplicada. No se hicieron cambios.")

# Validar las APIs reales instaladas antes de preparar ningún cambio.
SIMPLE_DEC_H = (
    ROOT / "managed_components" / "espressif__esp_audio_codec" /
    "include" / "simple_dec" / "esp_audio_simple_dec.h"
)
if not SIMPLE_DEC_H.exists():
    raise SystemExit(
        f"ERROR: no existe {SIMPLE_DEC_H}\n"
        "No se modificó ningún archivo."
    )

simple_dec = SIMPLE_DEC_H.read_text(encoding="utf-8", errors="ignore")
simple_required = [
    "ESP_AUDIO_SIMPLE_DEC_TYPE_MP3",
    "esp_audio_simple_dec_open",
    "esp_audio_simple_dec_process",
    "esp_audio_simple_dec_get_info",
    "esp_audio_simple_dec_close",
    "dec_type",
    "dec_cfg",
    "cfg_size",
    "use_frame_dec",
    "consumed",
    "needed_size",
    "decoded_size",
    "sample_rate",
    "bits_per_sample",
    "channel",
]
missing_simple = [x for x in simple_required if x not in simple_dec]
if missing_simple:
    raise SystemExit(
        "ERROR: la API Simple Decoder instalada no coincide con lo esperado:\n  - "
        + "\n  - ".join(missing_simple)
        + "\nNo se modificó ningún archivo."
    )

required = {
    "MP3 habilitado": "CONFIG_AUDIO_DECODER_MP3_SUPPORT=y" in sdkconfig,
    "PSRAM malloc": "CONFIG_SPIRAM_USE_MALLOC=y" in sdkconfig,
    "Audio OutputData": "codec_->OutputData(task.pcm);" in audio_cc,
    "RATE_CVT_CFG": "RATE_CVT_CFG(" in audio_cc,
    "AudioService PlaySound": "void PlaySound(const std::string_view& sound);" in audio_h,
    "Application audio init":
        "audio_service_.Initialize(codec);" in app and "audio_service_.Start();" in app,
    "Application state handler": "void Application::HandleStateChangedEvent()" in app,
    "MCP registration": "server.AddTool(" in mcp_hook and "registered = true;" in mcp_hook,
}
bad = [name for name, ok in required.items() if not ok]
if bad:
    raise SystemExit(
        "ERROR: el proyecto no coincide con el estado esperado:\n  - "
        + "\n  - ".join(bad)
        + "\nNo se modificó ningún archivo."
    )

# =============================================================================
# COMPONENTE care-radio
# =============================================================================

radio_h = r'''#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace xiaozhi_care::radio {

class RadioService {
public:
    using OutputCallback =
        std::function<bool(std::vector<int16_t>& pcm,
                           int sample_rate,
                           int channels)>;

    static RadioService& GetInstance();

    bool Init(OutputCallback output_cb);

    bool Play();
    void Stop();
    bool Pause();
    bool Resume();

    // Pausa automática impuesta por XiaoZhi.
    // Se usa mientras escucha, habla, conecta o notifica.
    void SetSystemPaused(bool paused);

    bool SetStation(const std::string& name, const std::string& url);
    std::string GetStationName() const;
    std::string GetStationUrl() const;

    bool WantsPlaying() const { return wants_playing_.load(); }
    bool IsUserPaused() const { return user_paused_.load(); }
    bool IsSystemPaused() const { return system_paused_.load(); }
    bool IsStreaming() const { return streaming_.load(); }

    std::string StatusJson() const;

private:
    RadioService() = default;
    ~RadioService() = default;
    RadioService(const RadioService&) = delete;
    RadioService& operator=(const RadioService&) = delete;

    static void TaskEntry(void* arg);
    void WorkerTask();
    bool StreamOnce(const std::string& url);

    bool LoadSettings();
    bool SaveSettingsLocked();

    mutable std::mutex mutex_;
    std::string station_name_;
    std::string station_url_;
    OutputCallback output_cb_;

    std::atomic<bool> initialized_{false};
    std::atomic<bool> wants_playing_{false};
    std::atomic<bool> user_paused_{false};
    std::atomic<bool> system_paused_{true};
    std::atomic<bool> streaming_{false};

    TaskHandle_t task_handle_ = nullptr;
};

}  // namespace xiaozhi_care::radio
'''

radio_cc = r'''#include "care_radio/radio_service.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include <esp_crt_bundle.h>
#include <esp_err.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <nvs.h>

#include "esp_audio_simple_dec.h"

namespace xiaozhi_care::radio {
namespace {

constexpr char kTag[] = "CARE_RADIO";
constexpr char kNvsNamespace[] = "care_radio";
constexpr char kNvsName[] = "name";
constexpr char kNvsUrl[] = "url";

// DP-042A: una única emisora de prueba.
// En la fase siguiente se editará desde Sistema.
constexpr char kDefaultStationName[] = "SomaFM Groove Salad";
constexpr char kDefaultStationUrl[] =
    "https://ice5.somafm.com/groovesalad-128-mp3";

constexpr size_t kHttpBufferBytes = 8192;
constexpr size_t kPcmBufferBytes = 16384;
constexpr int kHttpTimeoutMs = 5000;
constexpr int kReconnectDelayMs = 1500;

std::string JsonEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 16);
    for (char c : text) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

}  // namespace

RadioService& RadioService::GetInstance() {
    static RadioService instance;
    return instance;
}

bool RadioService::Init(OutputCallback output_cb) {
    if (!output_cb) {
        ESP_LOGE(kTag, "Invalid output callback");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        output_cb_ = std::move(output_cb);
    }

    if (!LoadSettings()) {
        ESP_LOGI(kTag, "Using built-in default station");
    }

    if (task_handle_ == nullptr) {
        BaseType_t created = xTaskCreate(
            &RadioService::TaskEntry,
            "care_radio",
            20 * 1024,
            this,
            3,
            &task_handle_);
        if (created != pdPASS) {
            task_handle_ = nullptr;
            ESP_LOGE(kTag, "Unable to create radio task");
            return false;
        }
    }

    initialized_.store(true);
    ESP_LOGI(kTag, "Radio ready: station='%s'", GetStationName().c_str());
    return true;
}

bool RadioService::LoadSettings() {
    std::lock_guard<std::mutex> lock(mutex_);

    station_name_ = kDefaultStationName;
    station_url_ = kDefaultStationUrl;

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Unable to open radio NVS: %s", esp_err_to_name(err));
        return false;
    }

    size_t name_len = 0;
    size_t url_len = 0;
    const esp_err_t name_err = nvs_get_str(handle, kNvsName, nullptr, &name_len);
    const esp_err_t url_err = nvs_get_str(handle, kNvsUrl, nullptr, &url_len);

    bool loaded = false;
    if (name_err == ESP_OK && url_err == ESP_OK &&
        name_len > 1 && name_len <= 96 &&
        url_len > 8 && url_len <= 512) {
        std::vector<char> name(name_len);
        std::vector<char> url(url_len);

        if (nvs_get_str(handle, kNvsName, name.data(), &name_len) == ESP_OK &&
            nvs_get_str(handle, kNvsUrl, url.data(), &url_len) == ESP_OK) {
            station_name_ = name.data();
            station_url_ = url.data();
            loaded = true;
        }
    }

    if (!loaded) {
        nvs_set_str(handle, kNvsName, station_name_.c_str());
        nvs_set_str(handle, kNvsUrl, station_url_.c_str());
        nvs_commit(handle);
    }

    nvs_close(handle);
    return loaded;
}

bool RadioService::SaveSettingsLocked() {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, kNvsName, station_name_.c_str());
    }
    if (err == ESP_OK) {
        err = nvs_set_str(handle, kNvsUrl, station_url_.c_str());
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    if (handle != 0) {
        nvs_close(handle);
    }

    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Unable to save station: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool RadioService::SetStation(const std::string& name,
                              const std::string& url) {
    if (name.empty() || name.size() > 95 ||
        url.size() < 8 || url.size() > 511 ||
        !(url.rfind("http://", 0) == 0 ||
          url.rfind("https://", 0) == 0)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    station_name_ = name;
    station_url_ = url;

    if (!SaveSettingsLocked()) {
        return false;
    }

    ESP_LOGI(kTag, "Station saved: %s", name.c_str());
    return true;
}

std::string RadioService::GetStationName() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return station_name_;
}

std::string RadioService::GetStationUrl() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return station_url_;
}

bool RadioService::Play() {
    if (!initialized_.load()) {
        ESP_LOGW(kTag, "Play requested before initialization");
        return false;
    }
    if (GetStationUrl().empty()) {
        ESP_LOGW(kTag, "No station configured");
        return false;
    }

    user_paused_.store(false);
    wants_playing_.store(true);
    ESP_LOGI(kTag, "Play requested: %s", GetStationName().c_str());
    return true;
}

void RadioService::Stop() {
    wants_playing_.store(false);
    user_paused_.store(false);
    streaming_.store(false);
    ESP_LOGI(kTag, "Stop requested");
}

bool RadioService::Pause() {
    if (!wants_playing_.load()) {
        return false;
    }
    user_paused_.store(true);
    ESP_LOGI(kTag, "User pause");
    return true;
}

bool RadioService::Resume() {
    if (!initialized_.load() || GetStationUrl().empty()) {
        return false;
    }

    wants_playing_.store(true);
    user_paused_.store(false);
    ESP_LOGI(kTag, "User resume");
    return true;
}

void RadioService::SetSystemPaused(bool paused) {
    const bool old = system_paused_.exchange(paused);
    if (old != paused && wants_playing_.load()) {
        ESP_LOGI(kTag, "System %s radio", paused ? "paused" : "resumed");
    }
}

std::string RadioService::StatusJson() const {
    std::string state = "stopped";

    if (wants_playing_.load()) {
        if (user_paused_.load()) {
            state = "paused";
        } else if (system_paused_.load()) {
            state = "system_paused";
        } else if (streaming_.load()) {
            state = "playing";
        } else {
            state = "connecting";
        }
    }

    return std::string("{\"ok\":true,\"state\":\"") +
           state +
           "\",\"station\":\"" +
           JsonEscape(GetStationName()) +
           "\"}";
}

void RadioService::TaskEntry(void* arg) {
    static_cast<RadioService*>(arg)->WorkerTask();
    vTaskDelete(nullptr);
}

void RadioService::WorkerTask() {
    while (true) {
        if (!wants_playing_.load()) {
            streaming_.store(false);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        const std::string url = GetStationUrl();
        if (url.empty()) {
            wants_playing_.store(false);
            streaming_.store(false);
            continue;
        }

        const bool ok = StreamOnce(url);
        streaming_.store(false);

        if (wants_playing_.load()) {
            ESP_LOGW(kTag,
                     "Stream ended (%s); reconnecting in %d ms",
                     ok ? "eof" : "error",
                     kReconnectDelayMs);
            vTaskDelay(pdMS_TO_TICKS(kReconnectDelayMs));
        }
    }
}

bool RadioService::StreamOnce(const std::string& url) {
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.timeout_ms = kHttpTimeoutMs;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.keep_alive_enable = true;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        ESP_LOGE(kTag, "esp_http_client_init failed");
        return false;
    }

    esp_http_client_set_header(
        client, "User-Agent", "XiaoZhi-Care-Radio/0.1");
    esp_http_client_set_header(
        client, "Accept", "audio/mpeg,*/*");
    // Evita insertar metadata ICY entre los frames MP3.
    esp_http_client_set_header(
        client, "Icy-MetaData", "0");

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "HTTP open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    (void)esp_http_client_fetch_headers(client);

    const int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGW(kTag, "Radio HTTP status=%d", status);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    esp_audio_simple_dec_cfg_t dec_cfg = {};
    dec_cfg.dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
    dec_cfg.dec_cfg = nullptr;
    dec_cfg.cfg_size = 0;
    dec_cfg.use_frame_dec = false;

    esp_audio_simple_dec_handle_t decoder = nullptr;
    const esp_audio_err_t open_ret =
        esp_audio_simple_dec_open(&dec_cfg, &decoder);

    if (open_ret != ESP_AUDIO_ERR_OK || decoder == nullptr) {
        ESP_LOGE(kTag,
                 "MP3 decoder open failed: %d",
                 static_cast<int>(open_ret));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    // Con CONFIG_SPIRAM_USE_MALLOC y umbral 1024, estos buffers grandes
    // pueden ir a PSRAM sin consumir la SRAM interna crítica.
    std::vector<uint8_t> http_buffer(kHttpBufferBytes);
    std::vector<uint8_t> pcm_buffer(kPcmBufferBytes);

    streaming_.store(true);
    ESP_LOGI(kTag,
             "Streaming MP3: %s",
             GetStationName().c_str());

    bool result = true;

    while (wants_playing_.load()) {
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
               wants_playing_.load()) {
            esp_audio_simple_dec_raw_t raw = {};
            raw.buffer = http_buffer.data() + offset;
            raw.len =
                static_cast<uint32_t>(
                    static_cast<size_t>(received) - offset);
            raw.eos = false;
            raw.consumed = 0;

            esp_audio_simple_dec_out_t out = {};
            out.buffer = pcm_buffer.data();
            out.len = static_cast<uint32_t>(pcm_buffer.size());
            out.needed_size = 0;
            out.decoded_size = 0;

            esp_audio_err_t ret =
                esp_audio_simple_dec_process(decoder, &raw, &out);

            // No dependemos del nombre del error: si el decoder informa
            // un tamaño mayor, ampliamos el buffer y reintentamos.
            if (ret != ESP_AUDIO_ERR_OK &&
                out.needed_size > pcm_buffer.size()) {
                pcm_buffer.resize(out.needed_size);

                raw.buffer = http_buffer.data() + offset;
                raw.len =
                    static_cast<uint32_t>(
                        static_cast<size_t>(received) - offset);
                raw.eos = false;
                raw.consumed = 0;

                out.buffer = pcm_buffer.data();
                out.len = static_cast<uint32_t>(pcm_buffer.size());
                out.needed_size = 0;
                out.decoded_size = 0;

                ret = esp_audio_simple_dec_process(
                    decoder, &raw, &out);
            }

            if (raw.consumed > 0) {
                offset += raw.consumed;
            }

            if (ret != ESP_AUDIO_ERR_OK) {
                // El Simple Decoder conserva su parser interno. Si no hubo
                // consumo, salimos de este bloque y esperamos más bytes.
                if (raw.consumed == 0) {
                    break;
                }
                continue;
            }

            if (out.decoded_size > 0) {
                esp_audio_simple_dec_info_t info = {};
                const esp_audio_err_t info_ret =
                    esp_audio_simple_dec_get_info(
                        decoder, &info);

                if (info_ret == ESP_AUDIO_ERR_OK &&
                    info.bits_per_sample == 16 &&
                    out.decoded_size >= sizeof(int16_t)) {
                    // Seguimos consumiendo/decodeando durante una pausa del
                    // sistema, pero descartamos el PCM. Así al volver a idle
                    // la emisora continúa en vivo y no reproduce audio viejo.
                    if (!system_paused_.load() &&
                        !user_paused_.load()) {
                        const size_t samples =
                            out.decoded_size / sizeof(int16_t);

                        std::vector<int16_t> pcm(samples);
                        std::memcpy(
                            pcm.data(),
                            out.buffer,
                            samples * sizeof(int16_t));

                        OutputCallback cb;
                        {
                            std::lock_guard<std::mutex> lock(mutex_);
                            cb = output_cb_;
                        }

                        if (cb) {
                            cb(pcm,
                               static_cast<int>(info.sample_rate),
                               static_cast<int>(info.channel));
                        }
                    }
                }
            }

            if (raw.consumed == 0 && out.decoded_size == 0) {
                break;
            }
        }
    }

    streaming_.store(false);

    esp_audio_simple_dec_close(decoder);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    return result;
}

}  // namespace xiaozhi_care::radio
'''

radio_cmake = r'''idf_component_register(
    SRCS "radio_service.cc"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_http_client
        esp-tls
        nvs_flash
        espressif__esp_audio_codec
)
'''

# =============================================================================
# AudioService: una única salida física para TTS/OGG/radio
# =============================================================================

public_anchor = "    void PlaySound(const std::string_view& sound);\n"
if public_anchor not in audio_h:
    raise SystemExit("ERROR: no encontré PlaySound en audio_service.h")

audio_h = audio_h.replace(
    public_anchor,
    public_anchor +
    "    // DP042A_RADIO_MP3_BASE: PCM externo proveniente de la radio.\n"
    "    bool OutputExternalPcm(std::vector<int16_t>& data,\n"
    "                           int source_sample_rate,\n"
    "                           int source_channels);\n",
    1
)

decoder_anchor = "    std::mutex decoder_mutex_;\n"
if decoder_anchor not in audio_h:
    raise SystemExit("ERROR: no encontré decoder_mutex_ en audio_service.h")

audio_h = audio_h.replace(
    decoder_anchor,
    decoder_anchor +
    "    // DP042A_RADIO_MP3_BASE: radio y playback normal comparten I2S.\n"
    "    std::mutex output_write_mutex_;\n"
    "    std::mutex external_output_resampler_mutex_;\n"
    "    esp_ae_rate_cvt_handle_t external_output_resampler_ = nullptr;\n"
    "    int external_output_resampler_source_rate_ = 0;\n",
    1
)

# Cerrar el resampler adicional en el destructor.
destructor_anchor = '''    if (output_resampler_ != nullptr) {
        esp_ae_rate_cvt_close(output_resampler_);
    }
'''
if destructor_anchor not in audio_cc:
    raise SystemExit("ERROR: no encontré destructor del output_resampler_")

audio_cc = audio_cc.replace(
    destructor_anchor,
    destructor_anchor +
    "    if (external_output_resampler_ != nullptr) {\n"
    "        esp_ae_rate_cvt_close(external_output_resampler_);\n"
    "        external_output_resampler_ = nullptr;\n"
    "    }\n",
    1
)

old_output = '''        if (!codec_->output_enabled()) {
            esp_timer_stop(audio_power_timer_);
            esp_timer_start_periodic(audio_power_timer_, AUDIO_POWER_CHECK_INTERVAL_MS * 1000);
            codec_->EnableOutput(true);
        }

        if (task.playback_id != 0 && callbacks_.on_playback_progress) {
            callbacks_.on_playback_progress(task.playback_id, task.media_position_ms);
        }

        codec_->OutputData(task.pcm);

        /* Update the last output time */
        last_output_time_ = std::chrono::steady_clock::now();
        debug_statistics_.playback_count++;
'''

new_output = '''        {
            // DP042A_RADIO_MP3_BASE
            // TTS/OGG y radio nunca escriben simultáneamente sobre I2S.
            std::lock_guard<std::mutex> output_lock(output_write_mutex_);

            if (!codec_->output_enabled()) {
                esp_timer_stop(audio_power_timer_);
                esp_timer_start_periodic(audio_power_timer_, AUDIO_POWER_CHECK_INTERVAL_MS * 1000);
                codec_->EnableOutput(true);
            }

            if (task.playback_id != 0 && callbacks_.on_playback_progress) {
                callbacks_.on_playback_progress(task.playback_id, task.media_position_ms);
            }

            codec_->OutputData(task.pcm);

            /* Update the last output time */
            last_output_time_ = std::chrono::steady_clock::now();
            debug_statistics_.playback_count++;
        }
'''

if old_output not in audio_cc:
    raise SystemExit(
        "ERROR: no encontré el bloque actual de AudioOutputTask esperado.\n"
        "No se modificó ningún archivo."
    )
audio_cc = audio_cc.replace(old_output, new_output, 1)

external_impl = r'''
// DP042A_RADIO_MP3_BASE
bool AudioService::OutputExternalPcm(std::vector<int16_t>& data,
                                     int source_sample_rate,
                                     int source_channels) {
    if (codec_ == nullptr ||
        data.empty() ||
        source_sample_rate <= 0 ||
        source_channels <= 0 ||
        service_stopped_.load()) {
        return false;
    }

    const size_t frames =
        data.size() / static_cast<size_t>(source_channels);
    if (frames == 0) {
        return false;
    }

    // La salida CARE/MAX98357A actual es mono. Para mantener también
    // compatibilidad con otros codecs, primero normalizamos a mono.
    std::vector<int16_t> mono(frames);

    if (source_channels == 1) {
        std::copy_n(data.begin(), frames, mono.begin());
    } else {
        for (size_t i = 0; i < frames; ++i) {
            int32_t sum = 0;
            for (int ch = 0; ch < source_channels; ++ch) {
                sum += data[
                    i * static_cast<size_t>(source_channels) +
                    static_cast<size_t>(ch)];
            }
            mono[i] =
                static_cast<int16_t>(sum / source_channels);
        }
    }

    std::vector<int16_t> converted;

    if (source_sample_rate != codec_->output_sample_rate()) {
        std::lock_guard<std::mutex> resampler_lock(
            external_output_resampler_mutex_);

        if (external_output_resampler_ == nullptr ||
            external_output_resampler_source_rate_ !=
                source_sample_rate) {
            if (external_output_resampler_ != nullptr) {
                esp_ae_rate_cvt_close(
                    external_output_resampler_);
                external_output_resampler_ = nullptr;
            }

            esp_ae_rate_cvt_cfg_t cfg =
                RATE_CVT_CFG(
                    source_sample_rate,
                    codec_->output_sample_rate(),
                    ESP_AUDIO_MONO);

            auto ret = esp_ae_rate_cvt_open(
                &cfg,
                &external_output_resampler_);

            if (external_output_resampler_ == nullptr) {
                ESP_LOGE(
                    TAG,
                    "Radio resampler open failed: %d",
                    ret);
                return false;
            }

            external_output_resampler_source_rate_ =
                source_sample_rate;

            ESP_LOGI(
                TAG,
                "Radio resampling audio from %d to %d",
                source_sample_rate,
                codec_->output_sample_rate());
        }

        uint32_t max_output = 0;
        esp_ae_rate_cvt_get_max_out_sample_num(
            external_output_resampler_,
            static_cast<uint32_t>(mono.size()),
            &max_output);

        if (max_output == 0) {
            return false;
        }

        converted.resize(max_output);
        uint32_t actual_output = max_output;

        auto ret = esp_ae_rate_cvt_process(
            external_output_resampler_,
            (esp_ae_sample_t)mono.data(),
            static_cast<uint32_t>(mono.size()),
            (esp_ae_sample_t)converted.data(),
            &actual_output);

        if (ret != ESP_AUDIO_ERR_OK) {
            ESP_LOGW(
                TAG,
                "Radio resample failed: %d",
                ret);
            return false;
        }

        converted.resize(actual_output);
    } else {
        converted = std::move(mono);
    }

    // Si algún board futuro usa salida estéreo, duplicamos el mono.
    if (codec_->output_channels() > 1) {
        std::vector<int16_t> interleaved(
            converted.size() *
            static_cast<size_t>(codec_->output_channels()));

        for (size_t i = 0; i < converted.size(); ++i) {
            for (int ch = 0;
                 ch < codec_->output_channels();
                 ++ch) {
                interleaved[
                    i * static_cast<size_t>(
                        codec_->output_channels()) +
                    static_cast<size_t>(ch)] =
                    converted[i];
            }
        }

        converted = std::move(interleaved);
    }

    {
        std::lock_guard<std::mutex> output_lock(
            output_write_mutex_);

        if (!codec_->output_enabled()) {
            esp_timer_stop(audio_power_timer_);
            esp_timer_start_periodic(
                audio_power_timer_,
                AUDIO_POWER_CHECK_INTERVAL_MS * 1000);
            codec_->EnableOutput(true);
        }

        codec_->OutputData(converted);
        last_output_time_ =
            std::chrono::steady_clock::now();
    }

    return true;
}

'''

read_anchor = (
    "bool AudioService::ReadAudioData("
    "std::vector<int16_t>& data, int sample_rate, int samples) {\n"
)
if read_anchor not in audio_cc:
    raise SystemExit("ERROR: no encontré ReadAudioData en audio_service.cc")

audio_cc = audio_cc.replace(
    read_anchor,
    external_impl + read_anchor,
    1
)

# =============================================================================
# Application: inicialización + pausa automática por estado
# =============================================================================

app_include_anchor = '#include "application.h"\n'
if '#include "care_radio/radio_service.h"' not in app:
    if app_include_anchor not in app:
        raise SystemExit("ERROR: no encontré include application.h")
    app = app.replace(
        app_include_anchor,
        app_include_anchor +
        '#include "care_radio/radio_service.h"\n',
        1
    )

init_anchor = '''    audio_service_.Initialize(codec);
    audio_service_.Start();
'''

radio_init = '''    audio_service_.Initialize(codec);
    audio_service_.Start();

    // DP042A_RADIO_MP3_BASE
    auto& care_radio =
        xiaozhi_care::radio::RadioService::GetInstance();
    care_radio.Init(
        [this](std::vector<int16_t>& pcm,
               int sample_rate,
               int channels) -> bool {
            return audio_service_.OutputExternalPcm(
                pcm, sample_rate, channels);
        });
    care_radio.SetSystemPaused(
        GetDeviceState() != kDeviceStateIdle);
'''

if init_anchor not in app:
    raise SystemExit("ERROR: no encontré inicialización actual de AudioService")

app = app.replace(init_anchor, radio_init, 1)

state_anchor = '''void Application::HandleStateChangedEvent() {
    DeviceState new_state = state_machine_.GetState();
    clock_ticks_ = 0;
'''

state_repl = '''void Application::HandleStateChangedEvent() {
    DeviceState new_state = state_machine_.GetState();
    clock_ticks_ = 0;

    // DP042A_RADIO_MP3_BASE
    // Sólo suena en idle. Al escuchar/hablar/notificar/conectar,
    // se descarta el PCM de radio; al volver a idle continúa en vivo.
    xiaozhi_care::radio::RadioService::GetInstance()
        .SetSystemPaused(new_state != kDeviceStateIdle);
'''

if state_anchor not in app:
    raise SystemExit("ERROR: no encontré HandleStateChangedEvent esperado")

app = app.replace(state_anchor, state_repl, 1)

# =============================================================================
# MCP: control natural por voz
# =============================================================================

pragma_anchor = "#pragma once\n"
if '#include "care_radio/radio_service.h"' not in mcp_hook:
    if pragma_anchor not in mcp_hook:
        raise SystemExit("ERROR: no encontré #pragma once en care_mcp_xiaozhi.h")
    mcp_hook = mcp_hook.replace(
        pragma_anchor,
        pragma_anchor +
        '\n#include "care_radio/radio_service.h"\n',
        1
    )

radio_tools = r'''
    // DP042A_RADIO_MP3_BASE
    server.AddTool(
        "care.radio_play",
        "RADIO POR INTERNET. Usa esta herramienta cuando el usuario diga "
        "'pone la radio', 'prende la radio', 'quiero escuchar la radio', "
        "'escuchemos radio' o frases equivalentes. NO repreguntes. "
        "Inicia la emisora guardada. La radio se silencia automaticamente "
        "mientras XiaoZhi escucha o habla y continua al volver a reposo.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            ESP_LOGI("CARE_MCP", "Tool call: care.radio_play");
            auto& radio =
                xiaozhi_care::radio::RadioService::GetInstance();
            if (!radio.Play()) {
                return std::string(
                    "{\"ok\":false,\"message\":\"No pude iniciar la radio.\"}");
            }
            return std::string(
                "{\"ok\":true,\"message\":\"Radio iniciada. Responde solo: Listo.\"}");
        });

    server.AddTool(
        "care.radio_stop",
        "RADIO POR INTERNET. Usa esta herramienta cuando el usuario diga "
        "'para la radio', 'apaga la radio', 'detene la radio', "
        "'saca la radio' o frases equivalentes.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            ESP_LOGI("CARE_MCP", "Tool call: care.radio_stop");
            xiaozhi_care::radio::RadioService::GetInstance().Stop();
            return std::string(
                "{\"ok\":true,\"message\":\"Radio detenida. Responde solo: Listo.\"}");
        });

    server.AddTool(
        "care.radio_pause",
        "RADIO POR INTERNET. Usa esta herramienta si el usuario pide "
        "pausar temporalmente la radio.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            ESP_LOGI("CARE_MCP", "Tool call: care.radio_pause");
            const bool ok =
                xiaozhi_care::radio::RadioService::GetInstance()
                    .Pause();
            return ok
                ? std::string(
                    "{\"ok\":true,\"message\":\"Radio pausada.\"}")
                : std::string(
                    "{\"ok\":false,\"message\":\"La radio no estaba reproduciendo.\"}");
        });

    server.AddTool(
        "care.radio_resume",
        "RADIO POR INTERNET. Usa esta herramienta cuando el usuario diga "
        "'segui con la radio', 'reanuda la radio' o "
        "'continua la radio'.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            ESP_LOGI("CARE_MCP", "Tool call: care.radio_resume");
            const bool ok =
                xiaozhi_care::radio::RadioService::GetInstance()
                    .Resume();
            return ok
                ? std::string(
                    "{\"ok\":true,\"message\":\"Radio reanudada.\"}")
                : std::string(
                    "{\"ok\":false,\"message\":\"No pude reanudar la radio.\"}");
        });

    server.AddTool(
        "care.radio_status",
        "RADIO POR INTERNET. Usa esta herramienta si el usuario pregunta "
        "'que radio esta sonando', 'esta prendida la radio' o "
        "por el estado de la radio.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            ESP_LOGI("CARE_MCP", "Tool call: care.radio_status");
            return xiaozhi_care::radio::RadioService::GetInstance()
                .StatusJson();
        });

'''

reg_pos = mcp_hook.find("    registered = true;")
if reg_pos < 0:
    raise SystemExit(
        "ERROR: no encontré registered = true; en care_mcp_xiaozhi.h"
    )

mcp_hook = (
    mcp_hook[:reg_pos] +
    radio_tools +
    mcp_hook[reg_pos:]
)

count_pattern = re.compile(
    r'Registered (\d+) XiaoZhi Care MCP tools'
)
count_matches = list(count_pattern.finditer(mcp_hook))

if len(count_matches) == 1:
    old_count = int(count_matches[0].group(1))
    mcp_hook = count_pattern.sub(
        f"Registered {old_count + 5} XiaoZhi Care MCP tools",
        mcp_hook,
        count=1
    )
    print(f"Contador MCP: {old_count} -> {old_count + 5}")
elif len(count_matches) > 1:
    raise SystemExit(
        "ERROR: encontré más de un contador MCP; no se modificó nada."
    )
else:
    print("AVISO: no hay contador literal MCP; no es funcional.")

# =============================================================================
# CMake
# =============================================================================

def add_to_requires(text: str, dep: str) -> str:
    if re.search(
        rf'(^|[\s\n]){re.escape(dep)}([\s\n]|$)',
        text
    ):
        return text

    marker = "    REQUIRES\n"
    pos = text.find(marker)
    if pos < 0:
        raise SystemExit(
            f"ERROR: no encontré REQUIRES para agregar {dep}"
        )
    pos += len(marker)
    return (
        text[:pos] +
        f"        {dep}\n" +
        text[pos:]
    )

mcp_cmake = add_to_requires(mcp_cmake, "care-radio")

if re.search(
    r'(^|[\s\n])care-radio([\s\n]|$)',
    main_cmake
) is None:
    main_dep_anchor = "                        care-mcp\n"
    if main_dep_anchor not in main_cmake:
        raise SystemExit(
            "ERROR: no encontré care-mcp en main/CMakeLists.txt"
        )
    main_cmake = main_cmake.replace(
        main_dep_anchor,
        main_dep_anchor +
        "                        care-radio\n",
        1
    )

# =============================================================================
# Validación final antes de tocar el proyecto
# =============================================================================

checks = {
    "radio service": "class RadioService" in radio_h,
    "HTTP streaming": "esp_http_client_read" in radio_cc,
    "MP3 decoder": "ESP_AUDIO_SIMPLE_DEC_TYPE_MP3" in radio_cc,
    "default test stream": "groovesalad-128-mp3" in radio_cc,
    "external PCM declaration": "OutputExternalPcm" in audio_h,
    "external PCM implementation": "AudioService::OutputExternalPcm" in audio_cc,
    "same I2S mutex": "output_write_mutex_" in audio_h and "output_write_mutex_" in audio_cc,
    "existing TTS path preserved": "codec_->OutputData(task.pcm);" in audio_cc,
    "radio resampler": "Radio resampling audio" in audio_cc,
    "application init": "care_radio.Init" in app,
    "automatic state pause": "SetSystemPaused(new_state != kDeviceStateIdle)" in app,
    "MCP play": '"care.radio_play"' in mcp_hook,
    "MCP stop": '"care.radio_stop"' in mcp_hook,
    "MCP pause": '"care.radio_pause"' in mcp_hook,
    "MCP resume": '"care.radio_resume"' in mcp_hook,
    "MCP status": '"care.radio_status"' in mcp_hook,
    "main dependency": "care-radio" in main_cmake,
    "MCP dependency": "care-radio" in mcp_cmake,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit(
        "ERROR: validación final falló:\n  - "
        + "\n  - ".join(failed)
        + "\nNo se modificó ningún archivo."
    )

# =============================================================================
# Backups + escritura
# =============================================================================

stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
to_backup = [
    MAIN_CMAKE,
    APP_CC,
    AUDIO_H,
    AUDIO_CC,
    MCP_HOOK,
    MCP_CMAKE,
]

for p in to_backup:
    backup = p.with_name(
        p.name + f".before-dp042a-radio-{stamp}.bak"
    )
    shutil.copy2(p, backup)
    print("Backup:", backup)

RADIO_DIR.mkdir(parents=True, exist_ok=False)
(RADIO_DIR / "include" / "care_radio").mkdir(
    parents=True,
    exist_ok=True
)

(RADIO_DIR / "include" / "care_radio" / "radio_service.h").write_text(
    radio_h,
    encoding="utf-8"
)
(RADIO_DIR / "radio_service.cc").write_text(
    radio_cc,
    encoding="utf-8"
)
(RADIO_DIR / "CMakeLists.txt").write_text(
    radio_cmake,
    encoding="utf-8"
)

MAIN_CMAKE.write_text(main_cmake, encoding="utf-8")
APP_CC.write_text(app, encoding="utf-8")
AUDIO_H.write_text(audio_h, encoding="utf-8")
AUDIO_CC.write_text(audio_cc, encoding="utf-8")
MCP_HOOK.write_text(mcp_hook, encoding="utf-8")
MCP_CMAKE.write_text(mcp_cmake, encoding="utf-8")

print()
print("====================================================")
print(" DP-042A - RADIO MP3 BASE APLICADA")
print("====================================================")
print()
print("Incluye:")
print(" - componente care-radio independiente")
print(" - streaming HTTP/HTTPS directo")
print(" - decoder MP3 oficial esp_audio_codec")
print(" - buffers grandes aptos para PSRAM")
print(" - downmix estéreo -> mono")
print(" - resampling dentro de AudioService al sample rate real")
print(" - mismo I2S/MAX98357A que XiaoZhi, protegido por mutex")
print(" - radio audible sólo cuando el estado es idle")
print(" - pausa automática mientras XiaoZhi escucha/habla/notifica")
print(" - continuación en vivo al volver a idle")
print(" - MCP: play / stop / pause / resume / status")
print(" - emisora inicial: SomaFM Groove Salad MP3 128k")
print(" - nombre y URL ya quedan guardados en NVS")
print()
print("Frases para probar:")
print(" - Poné la radio")
print(" - Pará la radio")
print(" - Pausá la radio")
print(" - Seguí con la radio")
print(" - ¿Qué radio está sonando?")
print()
print("Esta fase prueba el motor. Después agregaremos")
print("Sistema -> Radios de Internet para editar emisoras.")
print()
print("Ahora ejecutá:")
print("  idf.py build")
