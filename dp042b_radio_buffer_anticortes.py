from pathlib import Path
from datetime import datetime
import shutil

ROOT = Path(r"C:\xiaozhi-esp32_prueba3")
H = ROOT / "components" / "care-radio" / "include" / "care_radio" / "radio_service.h"
CC = ROOT / "components" / "care-radio" / "radio_service.cc"

for p in (H, CC):
    if not p.exists():
        raise SystemExit(f"ERROR: no existe {p}")

h = H.read_text(encoding="utf-8")
cc = CC.read_text(encoding="utf-8")

MARKER = "DP042B_RADIO_JITTER_BUFFER"
if MARKER in h or MARKER in cc:
    print("DP-042B ya parece aplicado. No se hicieron cambios.")
    raise SystemExit(0)

required = {
    "RadioService": "class RadioService" in h,
    "StreamOnce": "bool RadioService::StreamOnce" in cc,
    "MP3 decoder": "ESP_AUDIO_SIMPLE_DEC_TYPE_MP3" in cc,
    "MP3 registrado": "esp_mp3_dec_register();" in cc,
    "callback directo actual": "cb(pcm," in cc,
    "system pause": "void RadioService::SetSystemPaused(bool paused)" in cc,
}
bad = [name for name, ok in required.items() if not ok]
if bad:
    raise SystemExit(
        "ERROR: el código actual no coincide con DP-042A esperado:\n  - "
        + "\n  - ".join(bad)
        + "\nNo se modificó ningún archivo."
    )

# radio_service.h
if "#include <deque>\n" not in h:
    anchor = "#include <functional>\n"
    if anchor not in h:
        raise SystemExit("ERROR: no encontré include <functional> en radio_service.h")
    h = h.replace(anchor, anchor + "#include <deque>\n", 1)

old_private = '''    static void TaskEntry(void* arg);
    void WorkerTask();
    bool StreamOnce(const std::string& url);

    bool LoadSettings();
    bool SaveSettingsLocked();

    mutable std::mutex mutex_;
'''

new_private = '''    static void TaskEntry(void* arg);
    static void PlaybackTaskEntry(void* arg);

    void WorkerTask();
    void PlaybackTask();
    bool StreamOnce(const std::string& url);

    // DP042B_RADIO_JITTER_BUFFER
    struct BufferedPcm {
        std::vector<int16_t> pcm;
        int sample_rate = 0;
        int channels = 0;
        uint32_t duration_ms = 0;
    };

    bool EnqueuePcm(std::vector<int16_t>&& pcm,
                    int sample_rate,
                    int channels);
    void ClearPcmBuffer();

    bool LoadSettings();
    bool SaveSettingsLocked();

    mutable std::mutex mutex_;
'''

if old_private not in h:
    raise SystemExit(
        "ERROR: no encontré el bloque privado esperado en radio_service.h.\n"
        "No se modificó ningún archivo."
    )
h = h.replace(old_private, new_private, 1)

old_tail = '''    std::atomic<bool> streaming_{false};

    TaskHandle_t task_handle_ = nullptr;
};
'''

new_tail = '''    std::atomic<bool> streaming_{false};

    // DP042B_RADIO_JITTER_BUFFER
    // Productor: HTTP + MP3. Consumidor: salida PCM continua.
    mutable std::mutex pcm_buffer_mutex_;
    std::deque<BufferedPcm> pcm_buffer_;
    uint32_t buffered_ms_ = 0;
    std::atomic<bool> buffer_ready_{false};

    TaskHandle_t task_handle_ = nullptr;
    TaskHandle_t playback_task_handle_ = nullptr;
};
'''

if old_tail not in h:
    raise SystemExit(
        "ERROR: no encontré el final esperado de RadioService.\n"
        "No se modificó ningún archivo."
    )
h = h.replace(old_tail, new_tail, 1)

# radio_service.cc constants
old_constants = '''constexpr size_t kHttpBufferBytes = 8192;
constexpr size_t kPcmBufferBytes = 16384;
constexpr int kHttpTimeoutMs = 5000;
constexpr int kReconnectDelayMs = 1500;
'''

new_constants = '''constexpr size_t kHttpBufferBytes = 8192;
constexpr size_t kPcmBufferBytes = 16384;
constexpr int kHttpTimeoutMs = 5000;
constexpr int kReconnectDelayMs = 1500;

// DP042B_RADIO_JITTER_BUFFER
// Un segundo de colchón elimina huecos producidos por variación de red,
// decodificación y scheduling. El máximo mantiene la radio cerca del vivo.
constexpr uint32_t kPrebufferMs = 1000;
constexpr uint32_t kMaxBufferMs = 3000;
'''

if old_constants not in cc:
    raise SystemExit(
        "ERROR: no encontré las constantes esperadas en radio_service.cc.\n"
        "No se modificó ningún archivo."
    )
cc = cc.replace(old_constants, new_constants, 1)

# Init: crear playback task separada
old_init_task = '''    if (task_handle_ == nullptr) {
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
'''

new_init_task = '''    if (task_handle_ == nullptr) {
        BaseType_t created = xTaskCreate(
            &RadioService::TaskEntry,
            "care_radio_net",
            20 * 1024,
            this,
            3,
            &task_handle_);
        if (created != pdPASS) {
            task_handle_ = nullptr;
            ESP_LOGE(kTag, "Unable to create radio network task");
            return false;
        }
    }

    // DP042B_RADIO_JITTER_BUFFER
    if (playback_task_handle_ == nullptr) {
        BaseType_t created = xTaskCreate(
            &RadioService::PlaybackTaskEntry,
            "care_radio_play",
            8 * 1024,
            this,
            4,
            &playback_task_handle_);
        if (created != pdPASS) {
            playback_task_handle_ = nullptr;
            ESP_LOGE(kTag, "Unable to create radio playback task");
            return false;
        }
    }

    initialized_.store(true);
'''

if old_init_task not in cc:
    raise SystemExit(
        "ERROR: no encontré la creación de tarea de radio esperada.\n"
        "No se modificó ningún archivo."
    )
cc = cc.replace(old_init_task, new_init_task, 1)

# Stop
old_stop = '''void RadioService::Stop() {
    wants_playing_.store(false);
    user_paused_.store(false);
    streaming_.store(false);
    ESP_LOGI(kTag, "Stop requested");
}
'''
new_stop = '''void RadioService::Stop() {
    wants_playing_.store(false);
    user_paused_.store(false);
    streaming_.store(false);
    ClearPcmBuffer();
    ESP_LOGI(kTag, "Stop requested");
}
'''
if old_stop not in cc:
    raise SystemExit("ERROR: no encontré RadioService::Stop esperado")
cc = cc.replace(old_stop, new_stop, 1)

# Pause
old_pause = '''bool RadioService::Pause() {
    if (!wants_playing_.load()) {
        return false;
    }
    user_paused_.store(true);
    ESP_LOGI(kTag, "User pause");
    return true;
}
'''
new_pause = '''bool RadioService::Pause() {
    if (!wants_playing_.load()) {
        return false;
    }
    user_paused_.store(true);
    ClearPcmBuffer();
    ESP_LOGI(kTag, "User pause");
    return true;
}
'''
if old_pause not in cc:
    raise SystemExit("ERROR: no encontré RadioService::Pause esperado")
cc = cc.replace(old_pause, new_pause, 1)

# Resume
old_resume = '''    wants_playing_.store(true);
    user_paused_.store(false);
    ESP_LOGI(kTag, "User resume");
    return true;
}
'''
new_resume = '''    wants_playing_.store(true);
    user_paused_.store(false);
    buffer_ready_.store(false);
    ESP_LOGI(kTag, "User resume");
    return true;
}
'''
if old_resume not in cc:
    raise SystemExit("ERROR: no encontré RadioService::Resume esperado")
cc = cc.replace(old_resume, new_resume, 1)

# System pause
old_system_pause = '''void RadioService::SetSystemPaused(bool paused) {
    const bool old = system_paused_.exchange(paused);
    if (old != paused && wants_playing_.load()) {
        ESP_LOGI(kTag, "System %s radio", paused ? "paused" : "resumed");
    }
}
'''
new_system_pause = '''void RadioService::SetSystemPaused(bool paused) {
    const bool old = system_paused_.exchange(paused);

    if (paused && !old) {
        // DP042B_RADIO_JITTER_BUFFER
        // No reproducir audio viejo después de que XiaoZhi habló.
        ClearPcmBuffer();
    }

    if (!paused && old) {
        buffer_ready_.store(false);
    }

    if (old != paused && wants_playing_.load()) {
        ESP_LOGI(kTag, "System %s radio", paused ? "paused" : "resumed");
    }
}
'''
if old_system_pause not in cc:
    raise SystemExit("ERROR: no encontré SetSystemPaused esperado")
cc = cc.replace(old_system_pause, new_system_pause, 1)

# Task entry + playback
old_task_entry = '''void RadioService::TaskEntry(void* arg) {
    static_cast<RadioService*>(arg)->WorkerTask();
    vTaskDelete(nullptr);
}

void RadioService::WorkerTask() {
'''

new_task_entry = '''void RadioService::TaskEntry(void* arg) {
    static_cast<RadioService*>(arg)->WorkerTask();
    vTaskDelete(nullptr);
}

// DP042B_RADIO_JITTER_BUFFER
void RadioService::PlaybackTaskEntry(void* arg) {
    static_cast<RadioService*>(arg)->PlaybackTask();
    vTaskDelete(nullptr);
}

void RadioService::ClearPcmBuffer() {
    std::lock_guard<std::mutex> lock(pcm_buffer_mutex_);
    pcm_buffer_.clear();
    buffered_ms_ = 0;
    buffer_ready_.store(false);
}

bool RadioService::EnqueuePcm(std::vector<int16_t>&& pcm,
                              int sample_rate,
                              int channels) {
    if (pcm.empty() || sample_rate <= 0 || channels <= 0) {
        return false;
    }

    const size_t frames =
        pcm.size() / static_cast<size_t>(channels);
    if (frames == 0) {
        return false;
    }

    uint32_t duration_ms =
        static_cast<uint32_t>(
            (frames * 1000ULL) /
            static_cast<uint64_t>(sample_rate));
    if (duration_ms == 0) {
        duration_ms = 1;
    }

    while (wants_playing_.load() &&
           !system_paused_.load() &&
           !user_paused_.load()) {
        {
            std::lock_guard<std::mutex> lock(pcm_buffer_mutex_);
            if (buffered_ms_ < kMaxBufferMs) {
                BufferedPcm item;
                item.pcm = std::move(pcm);
                item.sample_rate = sample_rate;
                item.channels = channels;
                item.duration_ms = duration_ms;

                pcm_buffer_.push_back(std::move(item));
                buffered_ms_ += duration_ms;
                return true;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }

    return false;
}

void RadioService::PlaybackTask() {
    bool reported_ready = false;

    while (true) {
        if (!wants_playing_.load() ||
            system_paused_.load() ||
            user_paused_.load()) {
            reported_ready = false;
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        BufferedPcm item;
        bool have_item = false;
        uint32_t buffered_now = 0;

        {
            std::lock_guard<std::mutex> lock(pcm_buffer_mutex_);
            buffered_now = buffered_ms_;

            if (!buffer_ready_.load() &&
                buffered_ms_ >= kPrebufferMs) {
                buffer_ready_.store(true);
            }

            if (buffer_ready_.load() &&
                !pcm_buffer_.empty()) {
                item = std::move(pcm_buffer_.front());
                pcm_buffer_.pop_front();

                if (buffered_ms_ >= item.duration_ms) {
                    buffered_ms_ -= item.duration_ms;
                } else {
                    buffered_ms_ = 0;
                }

                have_item = true;
            }
        }

        if (!buffer_ready_.load()) {
            reported_ready = false;
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        if (!reported_ready) {
            ESP_LOGI(
                kTag,
                "Radio buffer ready: %lu ms",
                static_cast<unsigned long>(buffered_now));
            reported_ready = true;
        }

        if (!have_item) {
            buffer_ready_.store(false);
            reported_ready = false;
            ESP_LOGW(
                kTag,
                "Radio buffer underrun; rebuffering");
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        OutputCallback cb;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            cb = output_cb_;
        }

        if (cb) {
            cb(
                item.pcm,
                item.sample_rate,
                item.channels);
        }
    }
}

void RadioService::WorkerTask() {
'''

if old_task_entry not in cc:
    raise SystemExit(
        "ERROR: no encontré TaskEntry/WorkerTask esperado.\n"
        "No se modificó ningún archivo."
    )
cc = cc.replace(old_task_entry, new_task_entry, 1)

# Enqueue instead of direct callback
old_decode_output = '''                        OutputCallback cb;
                        {
                            std::lock_guard<std::mutex> lock(mutex_);
                            cb = output_cb_;
                        }

                        if (cb) {
                            cb(pcm,
                               static_cast<int>(info.sample_rate),
                               static_cast<int>(info.channel));
                        }
'''

new_decode_output = '''                        // DP042B_RADIO_JITTER_BUFFER
                        // HTTP/MP3 sólo produce PCM. La tarea de playback
                        // mantiene la salida al parlante continua.
                        EnqueuePcm(
                            std::move(pcm),
                            static_cast<int>(info.sample_rate),
                            static_cast<int>(info.channel));
'''

if old_decode_output not in cc:
    raise SystemExit(
        "ERROR: no encontré la salida PCM directa esperada.\n"
        "No se modificó ningún archivo."
    )
cc = cc.replace(old_decode_output, new_decode_output, 1)

checks = {
    "deque": "#include <deque>" in h,
    "BufferedPcm": "struct BufferedPcm" in h,
    "playback task": "PlaybackTask()" in h and "RadioService::PlaybackTask()" in cc,
    "prebuffer": "kPrebufferMs = 1000" in cc,
    "max buffer": "kMaxBufferMs = 3000" in cc,
    "enqueue": "EnqueuePcm(" in cc,
    "underrun": "Radio buffer underrun; rebuffering" in cc,
    "MP3 register preservado": "esp_mp3_dec_register();" in cc,
    "HTTP preservado": "esp_http_client_read" in cc,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit(
        "ERROR: validación final falló:\n  - "
        + "\n  - ".join(failed)
        + "\nNo se modificó ningún archivo."
    )

stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
for p in (H, CC):
    backup = p.with_name(
        p.name + f".before-dp042b-jitter-{stamp}.bak"
    )
    shutil.copy2(p, backup)
    print("Backup:", backup)

H.write_text(h, encoding="utf-8")
CC.write_text(cc, encoding="utf-8")

print()
print("====================================================")
print(" DP-042B - BUFFER ANTICORTES APLICADO")
print("====================================================")
print()
print("Cambios:")
print(" - tarea 1: HTTP + MP3")
print(" - tarea 2: playback PCM")
print(" - prebuffer inicial: 1000 ms")
print(" - buffer máximo: 3000 ms")
print(" - al escuchar/hablar se vacía para no reproducir audio viejo")
print(" - al volver a idle acumula 1 s y continúa en vivo")
print()
print("Logs esperados:")
print("  CARE_RADIO: Radio buffer ready: ... ms")
print()
print("Si vuelve a faltar audio veremos:")
print("  CARE_RADIO: Radio buffer underrun; rebuffering")
print()
print("Ahora ejecutá:")
print("  idf.py build")
print("  idf.py flash")
