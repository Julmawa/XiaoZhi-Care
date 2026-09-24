#pragma once

#include <atomic>
#include <functional>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace xiaozhi_care::radio {

// DP043A_RADIO_STATIONS_WEB
struct RadioStation {
    std::string name;
    std::string url;
};

class RadioService {
public:
    using OutputCallback =
        std::function<bool(std::vector<int16_t>& pcm,
                           int sample_rate,
                           int channels)>;

    static RadioService& GetInstance();

    bool Init(OutputCallback output_cb);

    // DP043C_RADIO_FINISHING
    using NetworkActivityCallback = std::function<void(bool active)>;
    void SetNetworkActivityCallback(NetworkActivityCallback callback);

    bool Play();
    // DP043B_RADIO_AAC_VOICE
    bool PlayStation(const std::string& selector);
    bool NextStation();
    std::string StationsJson() const;

    void Stop();
    bool Pause();
    bool Resume();

    // Pausa automática impuesta por XiaoZhi.
    // Se usa mientras escucha, habla, conecta o notifica.
    void SetSystemPaused(bool paused);

    // Compatibilidad: reemplaza la emisora predeterminada actual.
    bool SetStation(const std::string& name, const std::string& url);

    // DP043A_RADIO_STATIONS_WEB
    static constexpr size_t kMaxStations = 10;
    std::vector<RadioStation> ListStations() const;
    int GetDefaultStationIndex() const;
    bool AddOrUpdateStation(int index,
                            const std::string& name,
                            const std::string& url,
                            int* saved_index = nullptr);
    bool DeleteStation(int index);
    bool SetDefaultStation(int index);

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
    static void PlaybackTaskEntry(void* arg);

    void WorkerTask();
    void PlaybackTask();
    bool StreamOnce(const std::string& url, uint32_t generation);

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
    std::string station_name_;
    std::string station_url_;

    // DP043A_RADIO_STATIONS_WEB
    std::vector<RadioStation> stations_;
    int default_station_index_ = 0;

    OutputCallback output_cb_;
    NetworkActivityCallback network_activity_cb_;

    std::atomic<bool> initialized_{false};
    std::atomic<bool> wants_playing_{false};
    std::atomic<bool> user_paused_{false};
    std::atomic<bool> system_paused_{true};
    std::atomic<bool> streaming_{false};

    // DP043B_RADIO_AAC_VOICE
    std::atomic<uint32_t> stream_generation_{0};

    // DP042B_RADIO_JITTER_BUFFER
    // Productor: HTTP + MP3. Consumidor: salida PCM continua.
    mutable std::mutex pcm_buffer_mutex_;
    std::deque<BufferedPcm> pcm_buffer_;
    uint32_t buffered_ms_ = 0;
    std::atomic<bool> buffer_ready_{false};

    TaskHandle_t task_handle_ = nullptr;
    TaskHandle_t playback_task_handle_ = nullptr;
};

}  // namespace xiaozhi_care::radio
