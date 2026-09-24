#pragma once

#include <cstdint>
#include <string>

#include "care_daily/care_alarm_scheduler.h"

namespace xiaozhi_care::daily {

/**
 * @brief Abstract hardware notification layer for care alarms.
 *
 * The scheduler decides *when* an alert is due. The notifier translates that
 * action into presentation requests: visual, sound, or both.
 *
 * DP-013-r3 intentionally keeps this layer board-safe: it does not assume a
 * concrete LED strip, buzzer, display, or audio pipeline. Instead it exposes
 * weak C hooks that a board-specific file can implement later. If no hook is
 * provided, it still logs the requested visual/sound action so runtime testing
 * is visible and deterministic.
 */
class CareAlarmNotifier {
public:
    struct Stats {
        uint32_t actions{0};
        uint32_t visual_requests{0};
        uint32_t sound_requests{0};
        uint32_t voice_requests{0};
        uint32_t visual_handled{0};
        uint32_t sound_handled{0};
        uint32_t voice_handled{0};
    };

    CareAlarmNotifier() = default;

    /**
     * @brief Present one scheduler action through available hardware hooks.
     *
     * @return true when at least one requested channel was accepted by a hook.
     *         If no board hook exists, returns false but logs the request.
     */
    bool Notify(const CareAlarmAction& action);

    [[nodiscard]] const Stats& GetStats() const { return stats_; }

private:
    Stats stats_{};
};

}  // namespace xiaozhi_care::daily

extern "C" {

/**
 * @brief Optional board hook for visual care alerts.
 *
 * A board-specific implementation may drive WS2812/SK6812 LEDs, screen, or a
 * status lamp. Return true if the request was accepted.
 *
 * Default weak implementation returns false.
 */
bool xiaozhi_care_alarm_visual_notify(const char* routine_id,
                                      int visual_pattern,
                                      int visual_color,
                                      const char* message);

/**
 * @brief Optional board hook for sound care alerts.
 *
 * A board-specific implementation may play a beep, chime, or enqueue a spoken
 * reminder. Return true if the request was accepted.
 *
 * Default weak implementation returns false.
 */
bool xiaozhi_care_alarm_sound_notify(const char* routine_id,
                                     int sound_pattern,
                                     const char* message);

/**
 * @brief Optional board hook for spoken care alerts.
 *
 * A board-specific implementation may enqueue a short XiaoZhi TTS message.
 * The message must remain safe: it can remind what is pending, but must not
 * claim that a medication was taken or mention drug/dose names by default.
 *
 * Default weak implementation returns false.
 */
bool xiaozhi_care_alarm_voice_notify(const char* routine_id,
                                     const char* message);

}
