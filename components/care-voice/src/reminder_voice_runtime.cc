#include "care_voice/reminder_voice_runtime.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <string>
#include <utility>

#include <esp_log.h>
#include <nvs.h>

#include "care_manager.h"
#include "care_models.h"
#include "care_voice/voice_recording_store.h"

// DP040C_FASE1B_REMINDER_VISUAL
// Hook visual implementado por el driver WS2812B activo (SingleLed).
// Se declara directamente para evitar una dependencia circular
// care-voice -> care-daily -> care-voice.
extern "C" bool xiaozhi_care_alarm_visual_notify(
    const char* routine_or_reminder_id,
    int visual_pattern,
    int visual_color,
    const char* message);

extern "C" bool __attribute__((weak)) xiaozhi_care_reminder_audio_notify(
    const char* reminder_id,
    const char* message,
    const uint8_t* audio_data,
    size_t audio_size) {
    (void)reminder_id;
    (void)message;
    (void)audio_data;
    (void)audio_size;
    return false;
}

// DP044B_UNIVERSAL_ACK
// Hook opcional hacia Application para abrir una escucha breve tras presentar
// un aviso. Se mantiene weak para no crear una dependencia care-voice -> main.
extern "C" bool __attribute__((weak)) xiaozhi_care_ack_listening_notify(
    const char* target_id) {
    (void)target_id;
    return false;
}

namespace {

constexpr char kTag[] = "CARE_REMINDER_VOICE";
constexpr time_t kMinTrustedEpoch = 1704067200;  // 2024-01-01 UTC
constexpr uint32_t kTickIntervalMs = 10000;
constexpr int kGraceSeconds = 120;
constexpr int kAckResponseWindowSeconds = 600;
constexpr size_t kMaxAckEntries = 32;
constexpr uint32_t kTaskStackBytes = 6144;
constexpr UBaseType_t kTaskPriority = 4;


// DP044B2_CONFIG_ACK_AND_VOICE_LED
constexpr char kAckSettingsNamespace[] = "care_ack";
constexpr char kAckDelaySecondsKey[] = "delay_s";
constexpr char kAckMaxPresentationsKey[] = "max_show";
constexpr uint8_t kAckDefaultDelaySeconds = 60;

uint8_t ReadCareAckSettingU8(const char* key,
                             uint8_t fallback,
                             uint8_t min_value,
                             uint8_t max_value) {
    nvs_handle_t handle = 0;
    const esp_err_t open_err =
        nvs_open(kAckSettingsNamespace, NVS_READONLY, &handle);
    if (open_err != ESP_OK) {
        return fallback;
    }

    uint8_t value = fallback;
    const esp_err_t get_err = nvs_get_u8(handle, key, &value);
    nvs_close(handle);

    if (get_err != ESP_OK || value < min_value || value > max_value) {
        return fallback;
    }
    return value;
}

uint8_t CareAckPromptDelaySeconds() {
    return ReadCareAckSettingU8(
        kAckDelaySecondsKey, kAckDefaultDelaySeconds, 0, 60);
}

uint8_t CareAckMaxPresentations() {
    return ReadCareAckSettingU8(
        kAckMaxPresentationsKey,
        3,
        1,
        3);
}

bool SaveCareAckSettings(uint8_t delay_seconds,
                         uint8_t max_presentations) {
    if (delay_seconds > 60 ||
        max_presentations < 1 ||
        max_presentations > 3) {
        return false;
    }

    // DP044B2_1_TOUCH_TEXT_TTS_CONFIRMATION
    // Guard idempotente: no desgastar NVS ni generar una segunda escritura
    // cuando llegan exactamente los mismos valores.
    if (CareAckPromptDelaySeconds() == delay_seconds &&
        CareAckMaxPresentations() == max_presentations) {
        ESP_LOGI(kTag,
                 "DP044B2.1 ACK settings unchanged; NVS write skipped: delay_s=%u max_presentations=%u",
                 static_cast<unsigned>(delay_seconds),
                 static_cast<unsigned>(max_presentations));
        return true;
    }

    nvs_handle_t handle = 0;
    esp_err_t err =
        nvs_open(kAckSettingsNamespace, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, kAckDelaySecondsKey, delay_seconds);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(handle,
                         kAckMaxPresentationsKey,
                         max_presentations);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    if (handle != 0) {
        nvs_close(handle);
    }

    if (err != ESP_OK) {
        ESP_LOGE(kTag,
                 "DP044B2 failed to save ACK settings: err=%s",
                 esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(kTag,
             "DP044B2 ACK settings saved: delay_s=%u max_presentations=%u",
             static_cast<unsigned>(delay_seconds),
             static_cast<unsigned>(max_presentations));
    return true;
}

// DP040C_FASE1B_REMINDER_VISUAL
// Valores compatibles con CareVisualPattern / CareAlertColor:
// Breathing=2, Auto=0. En el driver actual, Auto para recordatorios
// cotidianos se representa como violeta, distinto de:
//   escuchar = verde
//   hablar   = azul
//   pastillero = ámbar
constexpr int kReminderVisualPattern = 2;
constexpr int kReminderVisualColor = 0;

bool IsLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int DaysInMonth(int year, int month) {
    static constexpr int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 0;
    if (month == 2 && IsLeapYear(year)) return 29;
    return kDays[month - 1];
}

bool ParseDate(const std::string& value, int& year, int& month, int& day) {
    if (value.size() != 10 || value[4] != '-' || value[7] != '-') return false;
    for (size_t i = 0; i < value.size(); ++i) {
        if (i == 4 || i == 7) continue;
        if (!std::isdigit(static_cast<unsigned char>(value[i]))) return false;
    }

    year = std::stoi(value.substr(0, 4));
    month = std::stoi(value.substr(5, 2));
    day = std::stoi(value.substr(8, 2));
    if (year < 1900 || year > 2200 || month < 1 || month > 12) return false;
    return day >= 1 && day <= DaysInMonth(year, month);
}

int IsoWeekday(int year, int month, int day) {
    // Sakamoto: 0=domingo ... 6=sÃ¡bado; se convierte a ISO 1=lunes ... 7=domingo.
    static constexpr int kOffsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3) --year;
    const int sunday_zero =
        (year + year / 4 - year / 100 + year / 400 + kOffsets[month - 1] + day) % 7;
    return sunday_zero == 0 ? 7 : sunday_zero;
}

std::string FormatDate(int year, int month, int day) {
    char buffer[64] = {};
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", year, month, day);
    return std::string(buffer);
}

bool ParseTime(const std::string& value, int& hour, int& minute) {
    if (value.size() != 5 || value[2] != ':') return false;
    if (!std::isdigit(static_cast<unsigned char>(value[0])) ||
        !std::isdigit(static_cast<unsigned char>(value[1])) ||
        !std::isdigit(static_cast<unsigned char>(value[3])) ||
        !std::isdigit(static_cast<unsigned char>(value[4]))) {
        return false;
    }
    hour = (value[0] - '0') * 10 + (value[1] - '0');
    minute = (value[3] - '0') * 10 + (value[4] - '0');
    return hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59;
}

bool ReminderOccursOn(const xiaozhi_care::CareReminder& reminder,
                      const std::string& target_date) {
    int ty = 0, tm = 0, td = 0;
    int ry = 0, rm = 0, rd = 0;
    if (!ParseDate(target_date, ty, tm, td) ||
        !ParseDate(reminder.date, ry, rm, rd)) {
        return false;
    }

    // Las recurrencias empiezan en su fecha configurada y nunca disparan antes.
    if (target_date < reminder.date) return false;

    if (reminder.recurrence.empty() || reminder.recurrence == "none") {
        return target_date == reminder.date;
    }
    if (reminder.recurrence == "daily") return true;
    if (reminder.recurrence == "weekly") {
        return IsoWeekday(ty, tm, td) == IsoWeekday(ry, rm, rd);
    }
    if (reminder.recurrence == "monthly") return td == rd;
    if (reminder.recurrence == "yearly") return tm == rm && td == rd;
    return false;
}

bool GetLocalClock(std::tm& local_tm, time_t& now) {
    now = std::time(nullptr);
    if (now < kMinTrustedEpoch) return false;
#if defined(_WIN32)
    return localtime_s(&local_tm, &now) == 0;
#else
    return localtime_r(&now, &local_tm) != nullptr;
#endif
}

}  // namespace

namespace xiaozhi_care::voice {

ReminderVoiceRuntime& ReminderVoiceRuntime::GetInstance() {
    static ReminderVoiceRuntime instance;
    return instance;
}

bool ReminderVoiceRuntime::Start() {
    if (task_ != nullptr) return true;

    if (!VoiceRecordingStore::GetInstance().Init()) {
        ESP_LOGE(kTag, "Voice store not ready; reminder audio runtime not started");
        return false;
    }

    TaskHandle_t created_task = nullptr;
    const BaseType_t result = xTaskCreate(&ReminderVoiceRuntime::TaskEntry,
                                          "care_rem_voice",
                                          kTaskStackBytes,
                                          this,
                                          kTaskPriority,
                                          &created_task);
    if (result != pdPASS || created_task == nullptr) {
        ESP_LOGE(kTag, "Unable to create reminder audio task");
        return false;
    }

    task_ = created_task;
    ESP_LOGI(kTag,
             "Reminder audio runtime started tick_ms=%u grace_s=%d",
             static_cast<unsigned>(kTickIntervalMs),
             kGraceSeconds);
    return true;
}

void ReminderVoiceRuntime::TaskEntry(void* arg) {
    auto* runtime = static_cast<ReminderVoiceRuntime*>(arg);
    runtime->Run();
}

void ReminderVoiceRuntime::Run() {
    while (true) {
        Tick();
        vTaskDelay(pdMS_TO_TICKS(kTickIntervalMs));
    }
}

// DP044B_UNIVERSAL_ACK
void ReminderVoiceRuntime::RegisterPresentation(
    const std::vector<std::string>& target_ids,
    const std::string& occurrence,
    const std::string& message,
    std::time_t presented_at) {
    if (target_ids.empty() || occurrence.empty()) {
        return;
    }

    if (presented_at <= 0) {
        presented_at = std::time(nullptr);
    }

    std::string listen_target;
    uint8_t attempts = 0;
    {
        std::lock_guard<std::mutex> lock(ack_mutex_);

        AckEntry* entry = nullptr;
        for (auto& candidate : ack_entries_) {
            if (candidate.occurrence != occurrence) continue;

            bool same_target = false;
            for (const auto& requested : target_ids) {
                if (std::find(candidate.target_ids.begin(),
                              candidate.target_ids.end(),
                              requested) != candidate.target_ids.end()) {
                    same_target = true;
                    break;
                }
            }
            if (same_target) {
                entry = &candidate;
                break;
            }
        }

        if (entry == nullptr) {
            if (ack_entries_.size() >= kMaxAckEntries) {
                auto oldest = std::min_element(
                    ack_entries_.begin(), ack_entries_.end(),
                    [](const AckEntry& a, const AckEntry& b) {
                        return a.sequence < b.sequence;
                    });
                if (oldest != ack_entries_.end()) {
                    ack_entries_.erase(oldest);
                }
            }

            AckEntry created;
            created.target_ids = target_ids;
            created.occurrence = occurrence;
            created.message = message;
            ack_entries_.push_back(std::move(created));
            entry = &ack_entries_.back();
        } else {
            for (const auto& requested : target_ids) {
                if (!requested.empty() &&
                    std::find(entry->target_ids.begin(),
                              entry->target_ids.end(),
                              requested) == entry->target_ids.end()) {
                    entry->target_ids.push_back(requested);
                }
            }
            if (!message.empty()) {
                entry->message = message;
            }
        }

        if (entry->acknowledged || entry->attempts >= CareAckMaxPresentations()) {
            return;
        }

        ++entry->attempts;
        entry->last_presented_at = presented_at;
        entry->sequence = ++ack_sequence_;
        attempts = entry->attempts;
        listen_target = entry->target_ids.empty() ? std::string() : entry->target_ids.front();
    }

    ESP_LOGI(kTag,
             "DP044B alert presented: target=%s occurrence=%s attempt=%u/%u",
             listen_target.c_str(),
             occurrence.c_str(),
             static_cast<unsigned>(attempts),
             static_cast<unsigned>(CareAckMaxPresentations()));

    if (!listen_target.empty()) {
        const bool listening_requested =
            ::xiaozhi_care_ack_listening_notify(listen_target.c_str());
        ESP_LOGI(kTag,
                 "DP044B acknowledgement listening requested: target=%s accepted=%d",
                 listen_target.c_str(),
                 listening_requested ? 1 : 0);
    }
}

ReminderAckState ReminderVoiceRuntime::GetAcknowledgementState(
    const std::string& target_id,
    const std::string& occurrence) const {
    ReminderAckState state;
    if (target_id.empty() || occurrence.empty()) return state;

    std::lock_guard<std::mutex> lock(ack_mutex_);
    for (const auto& entry : ack_entries_) {
        if (entry.occurrence != occurrence) continue;
        if (std::find(entry.target_ids.begin(), entry.target_ids.end(), target_id) ==
            entry.target_ids.end()) {
            continue;
        }

        state.found = true;
        state.acknowledged = entry.acknowledged;
        state.attempts = entry.attempts;
        state.last_presented_at = entry.last_presented_at;
        return state;
    }
    return state;
}

bool ReminderVoiceRuntime::IsRetryDue(const std::string& target_id,
                                      const std::string& occurrence,
                                      std::time_t now) const {
    const ReminderAckState state = GetAcknowledgementState(target_id, occurrence);
    if (!state.found || state.acknowledged || state.attempts >= CareAckMaxPresentations()) {
        return false;
    }
    if (now <= 0) now = std::time(nullptr);
    if (state.last_presented_at <= 0 || now <= 0) return false;
    return now - state.last_presented_at >= static_cast<std::time_t>(kAckRetrySeconds);
}

bool ReminderVoiceRuntime::HasPendingTarget(const std::string& target_id) const {
    if (target_id.empty()) return false;
    const std::time_t now = std::time(nullptr);

    std::lock_guard<std::mutex> lock(ack_mutex_);
    for (const auto& entry : ack_entries_) {
        if (entry.acknowledged || entry.attempts == 0 || entry.last_presented_at <= 0) continue;
        if (std::find(entry.target_ids.begin(), entry.target_ids.end(), target_id) ==
            entry.target_ids.end()) {
            continue;
        }
        if (now > 0 &&
            now - entry.last_presented_at > static_cast<std::time_t>(kAckResponseWindowSeconds)) {
            continue;
        }
        return true;
    }
    return false;
}

ReminderAckResult ReminderVoiceRuntime::AcknowledgeTarget(const std::string& target_id) {
    ReminderAckResult result;
    if (target_id.empty()) return result;

    const std::time_t now = std::time(nullptr);
    std::lock_guard<std::mutex> lock(ack_mutex_);

    AckEntry* latest = nullptr;
    for (auto& entry : ack_entries_) {
        if (entry.attempts == 0 || entry.last_presented_at <= 0) continue;
        if (std::find(entry.target_ids.begin(), entry.target_ids.end(), target_id) ==
            entry.target_ids.end()) {
            continue;
        }
        if (now > 0 &&
            now - entry.last_presented_at > static_cast<std::time_t>(kAckResponseWindowSeconds)) {
            continue;
        }
        if (latest == nullptr || entry.sequence > latest->sequence) {
            latest = &entry;
        }
    }

    if (latest == nullptr) return result;

    if (!latest->acknowledged) {
        latest->acknowledged = true;
        latest->sequence = ++ack_sequence_;
        ESP_LOGI(kTag,
                 "DP044B alert acknowledged locally: target=%s occurrence=%s attempts=%u",
                 target_id.c_str(),
                 latest->occurrence.c_str(),
                 static_cast<unsigned>(latest->attempts));
    }

    result.acknowledged = true;
    result.target_id = target_id;
    result.occurrence = latest->occurrence;
    result.message = latest->message;
    result.attempts = latest->attempts;
    return result;
}

ReminderAckResult ReminderVoiceRuntime::AcknowledgeLatest() {
    ReminderAckResult result;
    const std::time_t now = std::time(nullptr);

    std::lock_guard<std::mutex> lock(ack_mutex_);
    AckEntry* latest = nullptr;
    for (auto& entry : ack_entries_) {
        if (entry.acknowledged || entry.attempts == 0 || entry.last_presented_at <= 0) continue;
        if (now > 0 &&
            now - entry.last_presented_at > static_cast<std::time_t>(kAckResponseWindowSeconds)) {
            continue;
        }
        if (latest == nullptr || entry.sequence > latest->sequence) {
            latest = &entry;
        }
    }

    if (latest == nullptr) {
        // Idempotencia: si el STT local ya cerró el aviso y el LLM igualmente
        // llama care.acknowledge_last_alert unos milisegundos después, devolvemos
        // el último acuse reciente como éxito en vez de contradecirlo.
        for (auto& entry : ack_entries_) {
            if (!entry.acknowledged || entry.attempts == 0 || entry.last_presented_at <= 0) continue;
            if (now > 0 &&
                now - entry.last_presented_at > static_cast<std::time_t>(kAckResponseWindowSeconds)) {
                continue;
            }
            if (latest == nullptr || entry.sequence > latest->sequence) {
                latest = &entry;
            }
        }
        if (latest == nullptr) return result;
    } else {
        latest->acknowledged = true;
        latest->sequence = ++ack_sequence_;
    }

    result.acknowledged = true;
    result.target_id = latest->target_ids.empty() ? std::string() : latest->target_ids.front();
    result.occurrence = latest->occurrence;
    result.message = latest->message;
    result.attempts = latest->attempts;

    ESP_LOGI(kTag,
             "DP044B alert acknowledged: target=%s occurrence=%s attempts=%u",
             result.target_id.c_str(),
             result.occurrence.c_str(),
             static_cast<unsigned>(result.attempts));
    return result;
}

void ReminderVoiceRuntime::Tick() {
    std::tm local_tm{};
    time_t now = 0;
    if (!GetLocalClock(local_tm, now)) {
        if (!clock_warning_logged_) {
            ESP_LOGW(kTag, "Device clock not ready; reminder audio waiting for valid local time");
            clock_warning_logged_ = true;
        }
        return;
    }
    if (clock_warning_logged_) {
        ESP_LOGI(kTag, "Device clock ready; reminder audio evaluation enabled");
        clock_warning_logged_ = false;
    }

    const std::string today = FormatDate(local_tm.tm_year + 1900,
                                         local_tm.tm_mon + 1,
                                         local_tm.tm_mday);
    const int now_seconds = local_tm.tm_hour * 3600 + local_tm.tm_min * 60 + local_tm.tm_sec;

    auto& store = VoiceRecordingStore::GetInstance();
    const std::vector<VoiceRecordingInfo> recordings = store.List();

    for (const auto& recording : recordings) {
        if (!recording.enabled || recording.reminder_id.empty()) continue;

        CareReminder reminder;
        if (CareManager::GetInstance().GetReminder(recording.reminder_id, reminder) != CareError::OK) {
            continue;
        }
        if (!reminder.enabled || reminder.time.empty()) continue;
        if (!ReminderOccursOn(reminder, today)) continue;

        int due_hour = 0;
        int due_minute = 0;
        if (!ParseTime(reminder.time, due_hour, due_minute)) {
            ESP_LOGW(kTag,
                     "Invalid reminder time ignored: reminder=%s time=%s",
                     reminder.id.c_str(),
                     reminder.time.c_str());
            continue;
        }

        const int due_seconds = due_hour * 3600 + due_minute * 60;
        const int delta_seconds = now_seconds - due_seconds;
        if (delta_seconds < 0) continue;

        const std::string occurrence = today + "|" + reminder.time;
        const ReminderAckState ack =
            GetAcknowledgementState(reminder.id, occurrence);

        if (!ack.found) {
            // Conservamos la ventana original para el PRIMER aviso. Los reintentos
            // posteriores pertenecen a DP044B y pueden ocurrir fuera de esos 120 s.
            if (delta_seconds > kGraceSeconds) continue;
        } else {
            if (ack.acknowledged || ack.attempts >= kAckMaxAttempts) continue;
            if (!IsRetryDue(reminder.id, occurrence, now)) continue;
        }

        std::string audio;
        VoiceRecordingInfo loaded;
        if (!store.LoadAudio(recording.id, audio, &loaded) || audio.empty()) {
            ESP_LOGW(kTag,
                     "Unable to load assigned audio: reminder=%s recording=%s",
                     reminder.id.c_str(),
                     recording.id.c_str());
            continue;
        }

        const std::string message = reminder.title.empty() ? "Recordatorio" : reminder.title;
        const bool accepted = ::xiaozhi_care_reminder_audio_notify(
            reminder.id.c_str(),
            message.c_str(),
            reinterpret_cast<const uint8_t*>(audio.data()),
            audio.size());

        if (!accepted) {
            // XiaoZhi puede estar escuchando/hablando/notificando. No marcamos como
            // ejecutado para poder reintentar durante la ventana de gracia.
            ESP_LOGD(kTag,
                     "Reminder audio deferred: reminder=%s recording=%s",
                     reminder.id.c_str(),
                     recording.id.c_str());
            continue;
        }

        RegisterPresentation({reminder.id}, occurrence, message, now);
        ESP_LOGI(kTag,
                 "Reminder audio accepted: reminder=%s recording=%s bytes=%u occurrence=%s",
                 reminder.id.c_str(),
                 recording.id.c_str(),
                 static_cast<unsigned>(audio.size()),
                 occurrence.c_str());

        // DP040C_FASE1B_REMINDER_VISUAL
        // El LED acompaña únicamente una presentación realmente aceptada.
        // No cambia estados del recordatorio ni crea ejecuciones.
        const bool visual_handled = ::xiaozhi_care_alarm_visual_notify(
            reminder.id.c_str(),
            kReminderVisualPattern,
            kReminderVisualColor,
            message.c_str());

        ESP_LOGI(kTag,
                 "DP-040C reminder visual requested: reminder=%s pattern=%d color=%d handled=%d",
                 reminder.id.c_str(),
                 kReminderVisualPattern,
                 kReminderVisualColor,
                 visual_handled ? 1 : 0);

        // Present at most one reminder per tick. Besides being friendlier, this
        // guarantees that Application's binary cache is not replaced twice in
        // the same scheduled main-task batch. Other reminders due in the same
        // minute are picked up on subsequent ticks within the grace window.
        break;
    }
}

}  // namespace xiaozhi_care::voice

// DP044B_UNIVERSAL_ACK
// DP044B2_CONFIG_ACK_AND_VOICE_LED
extern "C" uint32_t xiaozhi_care_ack_prompt_delay_seconds(void) {
    return static_cast<uint32_t>(CareAckPromptDelaySeconds());
}

extern "C" uint8_t xiaozhi_care_ack_max_presentations(void) {
    return CareAckMaxPresentations();
}

extern "C" bool xiaozhi_care_ack_settings_set(uint32_t delay_seconds,
                                               uint8_t max_presentations) {
    if (delay_seconds > 60) {
        return false;
    }
    return SaveCareAckSettings(static_cast<uint8_t>(delay_seconds),
                               max_presentations);
}


extern "C" bool xiaozhi_care_ack_pending_for_target(const char* target_id) {
    if (target_id == nullptr || target_id[0] == '\0') return false;
    return xiaozhi_care::voice::ReminderVoiceRuntime::GetInstance()
        .HasPendingTarget(target_id);
}

extern "C" bool xiaozhi_care_acknowledge_target(const char* target_id) {
    if (target_id == nullptr || target_id[0] == '\0') return false;
    return xiaozhi_care::voice::ReminderVoiceRuntime::GetInstance()
        .AcknowledgeTarget(target_id)
        .acknowledged;
}


