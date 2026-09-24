#include "care_daily/care_alarm_runtime.h"

#include <ctime>
#include <string>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "care_daily/care_alarm_scheduler.h"
#include "care_daily/care_alarm_notifier.h"
#include "care_daily/nvs_routine_execution_repository.h"
#include "care_daily/nvs_routine_repository.h"
#include "care_voice/voice_recording_store.h"

// DP040B_FASE2_GROUPED_AUDIO_RUNTIME
// Reutiliza el mismo puente de audio local ya validado por DP-018.
// La implementación fuerte está en main/application.cc.
extern "C" bool xiaozhi_care_reminder_audio_notify(
    const char* reminder_id,
    const char* message,
    const uint8_t* audio_data,
    size_t audio_size);

namespace xiaozhi_care::daily {
namespace {

constexpr char kTag[] = "CARE_ALARM_RT";

// DP038_PILLBOX_GROUPING
//
// Agrupa solamente la PRESENTACION fisica de alarmas del pastillero.
// Los eventos internos del scheduler siguen siendo individuales por routine_id.
// Clave del grupo: casillero normalizado + horario programado.
std::string Dp038NormalizeAscii(std::string value) {
    for (char& ch : value) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }

    while (!value.empty() && value.front() == ' ') {
        value.erase(value.begin());
    }
    while (!value.empty() && value.back() == ' ') {
        value.pop_back();
    }
    return value;
}

bool Dp038IsGroupablePillboxRoutine(const CareRoutine& routine) {
    return routine.type == RoutineType::Medication &&
           routine.placement.type == PlacementType::Pillbox &&
           !routine.placement.compartment.empty();
}

std::string Dp038CompartmentPhrase(const std::string& compartment) {
    const std::string c = Dp038NormalizeAscii(compartment);

    if (c == "primero" || c == "primer" || c == "1" || c == "uno") {
        return "primer casillero";
    }
    if (c == "segundo" || c == "2" || c == "dos") {
        return "segundo casillero";
    }
    if (c == "tercero" || c == "tercer" || c == "3" || c == "tres") {
        return "tercer casillero";
    }
    if (c == "cuarto" || c == "4" || c == "cuatro") {
        return "cuarto casillero";
    }
    if (c == "quinto" || c == "5" || c == "cinco") {
        return "quinto casillero";
    }
    if (c == "sexto" || c == "6" || c == "seis") {
        return "sexto casillero";
    }

    if (c.find("casillero") != std::string::npos) {
        return c;
    }

    return c + " casillero";
}

std::string Dp038PillboxGroupKey(const CareRoutine& routine) {
    return Dp038NormalizeAscii(routine.placement.compartment) + "|" +
           std::to_string(static_cast<unsigned>(routine.schedule.time.hour)) + ":" +
           std::to_string(static_cast<unsigned>(routine.schedule.time.minute));
}

// DP040B_FASE2_GROUPED_AUDIO_RUNTIME
// La UI guarda una clave estable con HH:MM, por ejemplo:
//     pillbox:segundo|11:00
// No modificamos la clave interna de DP-038 para no alterar su agrupamiento ya validado.
std::string Dp040bPillboxAudioTargetKey(const CareRoutine& routine) {
    std::string hour = std::to_string(static_cast<unsigned>(routine.schedule.time.hour));
    std::string minute = std::to_string(static_cast<unsigned>(routine.schedule.time.minute));

    if (hour.size() < 2) hour.insert(hour.begin(), '0');
    if (minute.size() < 2) minute.insert(minute.begin(), '0');

    return "pillbox:" +
           Dp038NormalizeAscii(routine.placement.compartment) +
           "|" + hour + ":" + minute;
}

std::string Dp038PillboxMessage(const CareRoutine& routine) {
    return "Ahora te toca el " +
           Dp038CompartmentPhrase(routine.placement.compartment) + ".";
}


bool BuildCurrentContext(uint16_t minimum_valid_year,
                         RoutineEvaluationContext& context,
                         std::string& iso_date) {
    const std::time_t now = std::time(nullptr);
    if (now <= 0) {
        return false;
    }

    std::tm local_tm{};
    if (localtime_r(&now, &local_tm) == nullptr) {
        return false;
    }

    const int year = local_tm.tm_year + 1900;
    if (year < static_cast<int>(minimum_valid_year)) {
        return false;
    }

    const uint8_t iso_weekday = local_tm.tm_wday == 0
                                    ? 7
                                    : static_cast<uint8_t>(local_tm.tm_wday);

    const int safe_hour = (local_tm.tm_hour >= 0 && local_tm.tm_hour <= 23)
                              ? local_tm.tm_hour
                              : 0;
    const int safe_minute = (local_tm.tm_min >= 0 && local_tm.tm_min <= 59)
                                ? local_tm.tm_min
                                : 0;

    context.iso_weekday = iso_weekday;
    context.now = DailyTime(static_cast<uint8_t>(safe_hour),
                            static_cast<uint8_t>(safe_minute));

    char date_buffer[32] = {};
    const size_t written = std::strftime(date_buffer,
                                         sizeof(date_buffer),
                                         "%Y-%m-%d",
                                         &local_tm);
    if (written != 10) {
        return false;
    }

    iso_date = date_buffer;

    return context.IsValid() && iso_date.size() == 10;
}

}  // namespace

CareAlarmRuntime& CareAlarmRuntime::GetInstance() {
    static CareAlarmRuntime instance;
    return instance;
}

bool CareAlarmRuntime::Start(const CareAlarmRuntimeConfig& config) {
    if (!config.enabled) {
        ESP_LOGI(kTag, "Alarm runtime disabled by config");
        return true;
    }

    if (!config.IsValid()) {
        ESP_LOGE(kTag, "Invalid alarm runtime config");
        return false;
    }

    if (task_handle_ != nullptr) {
        ESP_LOGW(kTag, "Alarm runtime already running");
        return true;
    }

    config_ = config;
    stop_requested_ = false;

    const BaseType_t result = xTaskCreate(&CareAlarmRuntime::TaskEntry,
                                          "care_alarm_rt",
                                          config_.task_stack_size,
                                          this,
                                          config_.task_priority,
                                          &task_handle_);

    if (result != pdPASS) {
        task_handle_ = nullptr;
        ESP_LOGE(kTag, "Failed to create alarm runtime task");
        return false;
    }

    ESP_LOGI(kTag,
             "Alarm runtime started interval_ms=%lu startup_delay_ms=%lu",
             static_cast<unsigned long>(config_.tick_interval_ms),
             static_cast<unsigned long>(config_.startup_delay_ms));
    return true;
}

void CareAlarmRuntime::Stop() {
    stop_requested_ = true;
}

bool CareAlarmRuntime::IsRunning() const {
    return task_handle_ != nullptr;
}

void CareAlarmRuntime::TaskEntry(void* arg) {
    auto* runtime = static_cast<CareAlarmRuntime*>(arg);
    if (runtime != nullptr) {
        runtime->TaskLoop();
    }
    vTaskDelete(nullptr);
}

void CareAlarmRuntime::TaskLoop() {
    if (config_.startup_delay_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(config_.startup_delay_ms));
    }

    NvsRoutineRepository routine_repository;
    NvsRoutineExecutionRepository execution_repository;

    if (!routine_repository.Init()) {
        ESP_LOGE(kTag, "Routine repository init failed; alarm runtime stopped");
        task_handle_ = nullptr;
        return;
    }

    if (!execution_repository.Init()) {
        ESP_LOGE(kTag, "Execution repository init failed; alarm runtime stopped");
        task_handle_ = nullptr;
        return;
    }

    CareAlarmScheduler scheduler(routine_repository, execution_repository);
    CareAlarmNotifier notifier;
    bool time_warning_logged = false;

    while (!stop_requested_) {
        RoutineEvaluationContext context;
        std::string iso_date;

        if (!BuildCurrentContext(config_.minimum_valid_year, context, iso_date)) {
            if (!time_warning_logged) {
                ESP_LOGW(kTag, "System time not ready; alarm runtime waiting");
                time_warning_logged = true;
            }
            vTaskDelay(pdMS_TO_TICKS(config_.tick_interval_ms));
            continue;
        }
        time_warning_logged = false;

        const CareAlarmSchedulerResult result = scheduler.Tick(context, iso_date);

        if (result.due_count > 0 || result.notified_count > 0 || result.storage_error_count > 0) {
            ESP_LOGI(kTag,
                     "Alarm tick date=%s weekday=%u time=%02u:%02u due=%u notified=%u suppressed=%u storage_errors=%u",
                     iso_date.c_str(),
                     static_cast<unsigned>(context.iso_weekday),
                     static_cast<unsigned>(context.now.hour),
                     static_cast<unsigned>(context.now.minute),
                     static_cast<unsigned>(result.due_count),
                     static_cast<unsigned>(result.notified_count),
                     static_cast<unsigned>(result.suppressed_count),
                     static_cast<unsigned>(result.storage_error_count));
        }

        // DP-038 Fase 1:
        // El scheduler ya registro cada rutina individualmente. Desde este punto
        // agrupamos solamente la salida fisica para evitar repetir voz/sonido/LED
        // cuando varias rutinas Medication pertenecen al mismo casillero y horario.
        std::vector<CareAlarmAction> presentation_actions;
        std::vector<std::string> presentation_group_keys;
        std::vector<std::string> presentation_audio_targets;  // DP-040B Fase 2
        std::vector<unsigned> presentation_group_members;

        presentation_actions.reserve(result.actions.size());
        presentation_group_keys.reserve(result.actions.size());
        presentation_audio_targets.reserve(result.actions.size());
        presentation_group_members.reserve(result.actions.size());

        unsigned notifiable_actions = 0;

        NvsRoutineRepository grouping_repository;
        const bool grouping_repository_ready =
            result.actions.empty() ? false : grouping_repository.Init();

        if (!grouping_repository_ready && !result.actions.empty()) {
            ESP_LOGW(kTag,
                     "DP-038 grouping repository unavailable; presenting alarms individually");
        }

        for (const CareAlarmAction& source_action : result.actions) {
            if (!source_action.should_notify) {
                continue;
            }

            ++notifiable_actions;

            std::optional<CareRoutine> routine;
            if (grouping_repository_ready) {
                routine = grouping_repository.FindById(source_action.routine_id);
            }

            if (!routine.has_value() ||
                !Dp038IsGroupablePillboxRoutine(routine.value())) {
                presentation_actions.push_back(source_action);
                presentation_group_keys.emplace_back();
                presentation_audio_targets.emplace_back();
                presentation_group_members.push_back(1);
                continue;
            }

            const std::string key = Dp038PillboxGroupKey(routine.value());

            size_t existing_index = presentation_actions.size();
            for (size_t i = 0; i < presentation_group_keys.size(); ++i) {
                if (!presentation_group_keys[i].empty() &&
                    presentation_group_keys[i] == key) {
                    existing_index = i;
                    break;
                }
            }

            if (existing_index == presentation_actions.size()) {
                CareAlarmAction grouped = source_action;
                grouped.safe_message = Dp038PillboxMessage(routine.value());

                presentation_actions.push_back(grouped);
                presentation_group_keys.push_back(key);
                presentation_audio_targets.push_back(
                    Dp040bPillboxAudioTargetKey(routine.value()));
                presentation_group_members.push_back(1);
                continue;
            }

            CareAlarmAction& grouped = presentation_actions[existing_index];

            if (!grouped.visual && source_action.visual) {
                grouped.visual_pattern = source_action.visual_pattern;
                grouped.visual_color = source_action.visual_color;
            }
            if (!grouped.sound && source_action.sound) {
                grouped.sound_pattern = source_action.sound_pattern;
            }

            grouped.visual = grouped.visual || source_action.visual;
            grouped.sound = grouped.sound || source_action.sound;
            grouped.voice = grouped.voice || source_action.voice;

            grouped.execution_saved =
                grouped.execution_saved && source_action.execution_saved;

            ++presentation_group_members[existing_index];

            ESP_LOGI(kTag,
                     "DP-038 merged pillbox alarm: source=%s into=%s key=%s members=%u",
                     source_action.routine_id.Str().c_str(),
                     grouped.routine_id.Str().c_str(),
                     key.c_str(),
                     presentation_group_members[existing_index]);
        }

        if (notifiable_actions > 0) {
            ESP_LOGI(kTag,
                     "DP-038 presentation grouping: scheduler_actions=%u physical_actions=%u",
                     notifiable_actions,
                     static_cast<unsigned>(presentation_actions.size()));
        }

        for (size_t action_index = 0;
             action_index < presentation_actions.size();
             ++action_index) {
            const CareAlarmAction& action = presentation_actions[action_index];

            if (!presentation_group_keys[action_index].empty()) {
                ESP_LOGI(kTag,
                         "DP-038 pillbox group ready: representative=%s members=%u key=%s message=%s",
                         action.routine_id.Str().c_str(),
                         presentation_group_members[action_index],
                         presentation_group_keys[action_index].c_str(),
                         action.safe_message.c_str());
            }

            ESP_LOGI(kTag,
                     "Runtime alarm ready: routine=%s visual=%d sound=%d voice=%d saved=%d visual_pattern=%d visual_color=%d sound_pattern=%d message=%s",
                     action.routine_id.Str().c_str(),
                     action.visual ? 1 : 0,
                     action.sound ? 1 : 0,
                     action.voice ? 1 : 0,
                     action.execution_saved ? 1 : 0,
                     static_cast<int>(action.visual_pattern),
                     static_cast<int>(action.visual_color),
                     static_cast<int>(action.sound_pattern),
                     action.safe_message.c_str());

            // DP040B_FASE2_GROUPED_AUDIO_RUNTIME
            // Si este grupo tiene un OGG familiar asignado, intentamos reproducirlo
            // una sola vez para la alarma física agrupada. No cambia ejecuciones,
            // confirmaciones ni estados de medicación.
            bool grouped_audio_handled = false;
            const std::string& audio_target =
                presentation_audio_targets[action_index];

            if (!audio_target.empty() && (action.voice || action.sound)) {
                auto& voice_store =
                    xiaozhi_care::voice::VoiceRecordingStore::GetInstance();

                xiaozhi_care::voice::VoiceRecordingInfo recording;
                if (voice_store.FindByReminder(audio_target, recording)) {
                    std::string audio_bytes;
                    xiaozhi_care::voice::VoiceRecordingInfo loaded;

                    if (voice_store.LoadAudio(recording.id, audio_bytes, &loaded) &&
                        !audio_bytes.empty()) {
                        grouped_audio_handled =
                            ::xiaozhi_care_reminder_audio_notify(
                                audio_target.c_str(),
                                action.safe_message.c_str(),
                                reinterpret_cast<const uint8_t*>(audio_bytes.data()),
                                audio_bytes.size());

                        if (grouped_audio_handled) {
                            ESP_LOGI(
                                kTag,
                                "DP-040B grouped pillbox audio accepted: target=%s recording=%s bytes=%u",
                                audio_target.c_str(),
                                recording.id.c_str(),
                                static_cast<unsigned>(audio_bytes.size()));
                        } else {
                            ESP_LOGW(
                                kTag,
                                "DP-040B grouped pillbox audio deferred; using normal fallback: target=%s recording=%s",
                                audio_target.c_str(),
                                recording.id.c_str());
                        }
                    } else {
                        ESP_LOGW(
                            kTag,
                            "DP-040B assigned pillbox audio could not be loaded; using normal fallback: target=%s recording=%s",
                            audio_target.c_str(),
                            recording.id.c_str());
                    }
                }
            }

            CareAlarmAction notify_action = action;
            if (grouped_audio_handled) {
                // El OGG reemplaza la voz/sonido genéricos para evitar audio doble.
                // La presentación visual continúa normalmente.
                notify_action.voice = false;
                notify_action.sound = false;
            }

            const bool fallback_handled = notifier.Notify(notify_action);
            const bool hardware_handled =
                grouped_audio_handled || fallback_handled;

            ESP_LOGI(kTag,
                     "Runtime notifier result: routine=%s hardware_handled=%d visual_requests=%lu sound_requests=%lu voice_requests=%lu",
                     action.routine_id.Str().c_str(),
                     hardware_handled ? 1 : 0,
                     static_cast<unsigned long>(notifier.GetStats().visual_requests),
                     static_cast<unsigned long>(notifier.GetStats().sound_requests),
                     static_cast<unsigned long>(notifier.GetStats().voice_requests));
        }

        vTaskDelay(pdMS_TO_TICKS(config_.tick_interval_ms));
    }

    ESP_LOGI(kTag, "Alarm runtime stopped");
    task_handle_ = nullptr;
}

}  // namespace xiaozhi_care::daily
