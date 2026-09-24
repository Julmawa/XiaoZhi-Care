#include "care_radio/radio_service.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <esp_crt_bundle.h>
#include <esp_err.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <nvs.h>

#include "esp_audio_simple_dec.h"
#include "decoder/impl/esp_mp3_dec.h"
#include "decoder/impl/esp_aac_dec.h"

namespace xiaozhi_care::radio {
namespace {

constexpr char kTag[] = "CARE_RADIO";
constexpr char kNvsNamespace[] = "care_radio";
constexpr char kNvsName[] = "name";
constexpr char kNvsUrl[] = "url";

// DP043A_RADIO_STATIONS_WEB
constexpr char kNvsCount[] = "count";
constexpr char kNvsDefault[] = "default";

// DP-042A: una única emisora de prueba.
// En la fase siguiente se editará desde Sistema.
constexpr char kDefaultStationName[] = "SomaFM Groove Salad";
constexpr char kDefaultStationUrl[] =
    "https://ice5.somafm.com/groovesalad-128-mp3";

constexpr size_t kHttpBufferBytes = 8192;
constexpr size_t kPcmBufferBytes = 16384;
constexpr int kHttpTimeoutMs = 5000;
constexpr int kReconnectDelayMs = 1500;

// DP042B_RADIO_JITTER_BUFFER
// Un segundo de colchón elimina huecos producidos por variación de red,
// decodificación y scheduling. El máximo mantiene la radio cerca del vivo.
constexpr uint32_t kPrebufferMs = 1000;
constexpr uint32_t kMaxBufferMs = 3000;

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

    // DP042A_MP3_REGISTER_FIX
    // esp_audio_simple_dec usa el registro común de decoders.
    // Tener CONFIG_AUDIO_DECODER_MP3_SUPPORT=y compila MP3, pero no lo
    // registra automáticamente en runtime.
    const esp_audio_err_t mp3_register_ret =
        esp_mp3_dec_register();

    if (mp3_register_ret != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(
            kTag,
            "Unable to register MP3 decoder: %d",
            static_cast<int>(mp3_register_ret));
        return false;
    }

    ESP_LOGI(kTag, "MP3 decoder registered");

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
    ESP_LOGI(kTag, "Radio ready: station='%s'", GetStationName().c_str());
    return true;
}

bool RadioService::LoadSettings() {
    std::lock_guard<std::mutex> lock(mutex_);

    stations_.clear();
    default_station_index_ = 0;

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        stations_.push_back({kDefaultStationName, kDefaultStationUrl});
        station_name_ = stations_[0].name;
        station_url_ = stations_[0].url;
        return false;
    }

    auto read_string = [&](const char* key, std::string& out) -> bool {
        size_t len = 0;
        if (nvs_get_str(handle, key, nullptr, &len) != ESP_OK ||
            len <= 1 || len > 512) {
            return false;
        }
        std::vector<char> buffer(len);
        if (nvs_get_str(handle, key, buffer.data(), &len) != ESP_OK) {
            return false;
        }
        out = buffer.data();
        return true;
    };

    uint8_t count = 0;
    const esp_err_t count_err = nvs_get_u8(handle, kNvsCount, &count);

    if (count_err == ESP_OK &&
        count >= 1 &&
        count <= static_cast<uint8_t>(kMaxStations)) {
        for (uint8_t i = 0; i < count; ++i) {
            char name_key[8];
            char url_key[8];
            snprintf(name_key, sizeof(name_key), "n%u",
                     static_cast<unsigned>(i));
            snprintf(url_key, sizeof(url_key), "u%u",
                     static_cast<unsigned>(i));

            std::string name;
            std::string url;
            if (read_string(name_key, name) &&
                read_string(url_key, url) &&
                !name.empty() &&
                (url.rfind("http://", 0) == 0 ||
                 url.rfind("https://", 0) == 0)) {
                stations_.push_back({name, url});
            }
        }
    }

    bool migrated = false;

    // Migración automática desde DP-042: una sola emisora name/url.
    if (stations_.empty()) {
        std::string legacy_name;
        std::string legacy_url;

        if (read_string(kNvsName, legacy_name) &&
            read_string(kNvsUrl, legacy_url) &&
            !legacy_name.empty() &&
            (legacy_url.rfind("http://", 0) == 0 ||
             legacy_url.rfind("https://", 0) == 0)) {
            stations_.push_back({legacy_name, legacy_url});
        } else {
            stations_.push_back(
                {kDefaultStationName, kDefaultStationUrl});
        }
        migrated = true;
    }

    uint8_t default_index = 0;
    if (nvs_get_u8(handle, kNvsDefault, &default_index) == ESP_OK &&
        default_index < stations_.size()) {
        default_station_index_ = default_index;
    } else {
        default_station_index_ = 0;
        migrated = true;
    }

    // DP043B_RADIO_AAC_VOICE
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

    if (migrated) {
        return SaveSettingsLocked();
    }

    ESP_LOGI(
        kTag,
        "Loaded %u radio stations; default=%d (%s)",
        static_cast<unsigned>(stations_.size()),
        default_station_index_,
        station_name_.c_str());

    return true;
}


bool RadioService::SaveSettingsLocked() {
    if (stations_.empty() ||
        stations_.size() > kMaxStations ||
        default_station_index_ < 0 ||
        default_station_index_ >= static_cast<int>(stations_.size())) {
        return false;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);

    if (err == ESP_OK) {
        err = nvs_set_u8(
            handle,
            kNvsCount,
            static_cast<uint8_t>(stations_.size()));
    }

    if (err == ESP_OK) {
        err = nvs_set_u8(
            handle,
            kNvsDefault,
            static_cast<uint8_t>(default_station_index_));
    }

    for (size_t i = 0; err == ESP_OK && i < kMaxStations; ++i) {
        char name_key[8];
        char url_key[8];
        snprintf(name_key, sizeof(name_key), "n%u",
                 static_cast<unsigned>(i));
        snprintf(url_key, sizeof(url_key), "u%u",
                 static_cast<unsigned>(i));

        if (i < stations_.size()) {
            err = nvs_set_str(
                handle, name_key, stations_[i].name.c_str());
            if (err == ESP_OK) {
                err = nvs_set_str(
                    handle, url_key, stations_[i].url.c_str());
            }
        } else {
            nvs_erase_key(handle, name_key);
            nvs_erase_key(handle, url_key);
        }
    }

    if (err == ESP_OK) {
        const auto& def =
            stations_[static_cast<size_t>(default_station_index_)];
        err = nvs_set_str(handle, kNvsName, def.name.c_str());
        if (err == ESP_OK) {
            err = nvs_set_str(handle, kNvsUrl, def.url.c_str());
        }
    }

    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    if (handle != 0) {
        nvs_close(handle);
    }

    if (err != ESP_OK) {
        ESP_LOGE(
            kTag,
            "Unable to save radio stations: %s",
            esp_err_to_name(err));
        return false;
    }

    return true;
}


bool RadioService::SetStation(const std::string& name,
                              const std::string& url) {
    return AddOrUpdateStation(
        GetDefaultStationIndex(),
        name,
        url,
        nullptr);
}


// DP043A_RADIO_STATIONS_WEB
std::vector<RadioStation> RadioService::ListStations() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stations_;
}

int RadioService::GetDefaultStationIndex() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return default_station_index_;
}

bool RadioService::AddOrUpdateStation(int index,
                                      const std::string& name,
                                      const std::string& url,
                                      int* saved_index) {
    if (name.empty() || name.size() > 95 ||
        url.size() < 8 || url.size() > 511 ||
        !(url.rfind("http://", 0) == 0 ||
          url.rfind("https://", 0) == 0)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    int target = index;

    if (target < 0) {
        if (stations_.size() >= kMaxStations) {
            return false;
        }
        stations_.push_back({name, url});
        target = static_cast<int>(stations_.size()) - 1;
    } else {
        if (target >= static_cast<int>(stations_.size())) {
            return false;
        }
        stations_[static_cast<size_t>(target)] = {name, url};
    }

    if (!SaveSettingsLocked()) {
        return false;
    }

    if (saved_index != nullptr) {
        *saved_index = target;
    }

    ESP_LOGI(
        kTag,
        "Station %s index=%d name=%s",
        index < 0 ? "added" : "updated",
        target,
        name.c_str());

    return true;
}

bool RadioService::DeleteStation(int index) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (stations_.size() <= 1 ||
        index < 0 ||
        index >= static_cast<int>(stations_.size())) {
        return false;
    }

    stations_.erase(stations_.begin() + index);

    if (default_station_index_ == index) {
        default_station_index_ = 0;
    } else if (default_station_index_ > index) {
        --default_station_index_;
    }

    const auto& def =
        stations_[static_cast<size_t>(default_station_index_)];

    if (!wants_playing_.load()) {
        station_name_ = def.name;
        station_url_ = def.url;
    }

    if (!SaveSettingsLocked()) {
        return false;
    }

    ESP_LOGI(
        kTag,
        "Station deleted index=%d remaining=%u",
        index,
        static_cast<unsigned>(stations_.size()));

    return true;
}

bool RadioService::SetDefaultStation(int index) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (index < 0 ||
        index >= static_cast<int>(stations_.size())) {
        return false;
    }

    default_station_index_ = index;

    if (!wants_playing_.load()) {
        station_name_ =
            stations_[static_cast<size_t>(index)].name;
        station_url_ =
            stations_[static_cast<size_t>(index)].url;
    }

    if (!SaveSettingsLocked()) {
        return false;
    }

    ESP_LOGI(
        kTag,
        "Default station changed: index=%d name=%s",
        index,
        stations_[static_cast<size_t>(index)].name.c_str());

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

// DP043C_RADIO_FINISHING
void RadioService::SetNetworkActivityCallback(
    NetworkActivityCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    network_activity_cb_ = std::move(callback);
}

bool RadioService::Play() {
    if (!initialized_.load()) return false;

    NetworkActivityCallback cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stations_.empty() ||
            default_station_index_ < 0 ||
            default_station_index_ >= static_cast<int>(stations_.size())) {
            return false;
        }

        const auto& s =
            stations_[static_cast<size_t>(default_station_index_)];
        station_name_ = s.name;
        station_url_ = s.url;
        cb = network_activity_cb_;
    }

    ClearPcmBuffer();
    stream_generation_.fetch_add(1);
    user_paused_.store(false);
    wants_playing_.store(true);

    if (cb) cb(true);

    ESP_LOGI(kTag, "Play requested: %s", GetStationName().c_str());
    return true;
}




// DP043B_RADIO_AAC_VOICE
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

    NetworkActivityCallback cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cb = network_activity_cb_;
    }

    ClearPcmBuffer();
    stream_generation_.fetch_add(1);
    user_paused_.store(false);
    wants_playing_.store(true);
    if (cb) cb(true);

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

    NetworkActivityCallback cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cb = network_activity_cb_;
    }

    ClearPcmBuffer();
    stream_generation_.fetch_add(1);
    user_paused_.store(false);
    wants_playing_.store(true);
    if (cb) cb(true);

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

void RadioService::Stop() {
    NetworkActivityCallback cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cb = network_activity_cb_;
    }

    wants_playing_.store(false);
    user_paused_.store(false);
    stream_generation_.fetch_add(1);
    streaming_.store(false);
    ClearPcmBuffer();

    if (cb) cb(false);

    ESP_LOGI(kTag, "Stop requested");
}



bool RadioService::Pause() {
    if (!wants_playing_.load()) {
        return false;
    }
    user_paused_.store(true);
    ClearPcmBuffer();
    ESP_LOGI(kTag, "User pause");
    return true;
}

bool RadioService::Resume() {
    if (!initialized_.load() || GetStationUrl().empty()) {
        return false;
    }

    wants_playing_.store(true);
    user_paused_.store(false);
    buffer_ready_.store(false);

    NetworkActivityCallback cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cb = network_activity_cb_;
    }
    if (cb) cb(true);

    ESP_LOGI(kTag, "User resume");
    return true;
}

void RadioService::SetSystemPaused(bool paused) {
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

std::string RadioService::StatusJson() const {
    std::string state = "stopped";

    if (wants_playing_.load()) {
        if (user_paused_.load()) state = "paused";
        else if (system_paused_.load()) state = "system_paused";
        else if (streaming_.load()) state = "playing";
        else state = "connecting";
    }

    return std::string("{\"ok\":true,\"state\":\"") +
           state +
           "\",\"station\":\"" +
           JsonEscape(GetStationName()) +
           "\",\"instruction\":\"Usa exactamente station como nombre de la emisora; no lo inventes, traduzcas ni corrijas.\"}";
}


void RadioService::TaskEntry(void* arg) {
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
}


bool RadioService::StreamOnce(const std::string& url,
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
}


}  // namespace xiaozhi_care::radio
