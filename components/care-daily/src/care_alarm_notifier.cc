#include "care_daily/care_alarm_notifier.h"

#include <esp_log.h>

namespace {
constexpr char kTag[] = "CARE_ALARM_HW";

const char* SafeCStr(const std::string& value) {
    return value.empty() ? "" : value.c_str();
}
}

extern "C" bool __attribute__((weak)) xiaozhi_care_alarm_visual_notify(
    const char* routine_id,
    int visual_pattern,
    int visual_color,
    const char* message) {
    (void)routine_id;
    (void)visual_pattern;
    (void)visual_color;
    (void)message;
    return false;
}

extern "C" bool __attribute__((weak)) xiaozhi_care_alarm_sound_notify(
    const char* routine_id,
    int sound_pattern,
    const char* message) {
    (void)routine_id;
    (void)sound_pattern;
    (void)message;
    return false;
}

extern "C" bool __attribute__((weak)) xiaozhi_care_alarm_voice_notify(
    const char* routine_id,
    const char* message) {
    (void)routine_id;
    (void)message;
    return false;
}

namespace xiaozhi_care::daily {

bool CareAlarmNotifier::Notify(const CareAlarmAction& action) {
    if (!action.should_notify) {
        return false;
    }

    ++stats_.actions;

    bool any_handled = false;
    const char* routine_id = action.routine_id.Str().c_str();
    const char* message = SafeCStr(action.safe_message);

    if (action.visual) {
        ++stats_.visual_requests;
        ESP_LOGI(kTag,
                 "Visual alert requested: routine=%s pattern=%d color=%d message=%s",
                 routine_id,
                 static_cast<int>(action.visual_pattern),
                 static_cast<int>(action.visual_color),
                 message);

        const bool handled = xiaozhi_care_alarm_visual_notify(
            routine_id,
            static_cast<int>(action.visual_pattern),
            static_cast<int>(action.visual_color),
            message);

        if (handled) {
            ++stats_.visual_handled;
            any_handled = true;
            ESP_LOGI(kTag, "Visual alert handled by board hook: routine=%s", routine_id);
        } else {
            ESP_LOGW(kTag, "No visual hardware hook installed: routine=%s", routine_id);
        }
    }

    if (action.voice) {
        ++stats_.voice_requests;
        ESP_LOGI(kTag,
                 "Voice alert requested: routine=%s message=%s",
                 routine_id,
                 message);

        const bool handled = xiaozhi_care_alarm_voice_notify(routine_id, message);

        if (handled) {
            ++stats_.voice_handled;
            any_handled = true;
            ESP_LOGI(kTag, "Voice alert handled by board hook: routine=%s", routine_id);
        } else {
            ESP_LOGW(kTag, "No voice hardware hook installed: routine=%s", routine_id);
        }
    }

    if (action.sound) {
        ++stats_.sound_requests;
        ESP_LOGI(kTag,
                 "Sound alert requested: routine=%s pattern=%d message=%s",
                 routine_id,
                 static_cast<int>(action.sound_pattern),
                 message);

        const bool handled = xiaozhi_care_alarm_sound_notify(
            routine_id,
            static_cast<int>(action.sound_pattern),
            message);

        if (handled) {
            ++stats_.sound_handled;
            any_handled = true;
            ESP_LOGI(kTag, "Sound alert handled by board hook: routine=%s", routine_id);
        } else {
            ESP_LOGW(kTag, "No sound hardware hook installed: routine=%s", routine_id);
        }
    }

    if (!action.visual && !action.sound && !action.voice) {
        ESP_LOGW(kTag, "Alarm action has no presentation channel: routine=%s", routine_id);
    }

    return any_handled;
}

}  // namespace xiaozhi_care::daily
