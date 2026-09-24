#include "care_daily/care_ws2812_16_led_bar.h"

#include <esp_log.h>

namespace {
constexpr char kTag[] = "CARE_LED_BAR";
}

extern "C" bool xiaozhi_unified_led_bar_start(void);
extern "C" void xiaozhi_unified_led_bar_clear(void);
extern "C" bool xiaozhi_unified_led_bar_care_alert(const char* routine_id,
                                                    int visual_pattern,
                                                    int visual_color,
                                                    const char* message);

extern "C" bool xiaozhi_care_led_bar_start(void) {
#if XIAOZHI_CARE_LED_BAR_ENABLED
    const bool ready = xiaozhi_unified_led_bar_start();
    ESP_LOGI(kTag,
             "Unified WS2812B 16 LED bar start requested gpio=%d leds=%d ready=%d",
             XIAOZHI_CARE_LED_BAR_GPIO,
             XIAOZHI_CARE_LED_BAR_COUNT,
             ready ? 1 : 0);
    return ready;
#else
    ESP_LOGW(kTag, "WS2812B LED bar disabled at compile time");
    return false;
#endif
}

extern "C" void xiaozhi_care_led_bar_clear(void) {
#if XIAOZHI_CARE_LED_BAR_ENABLED
    xiaozhi_unified_led_bar_clear();
#endif
}

extern "C" bool xiaozhi_care_alarm_visual_notify(const char* routine_id,
                                                  int visual_pattern,
                                                  int visual_color,
                                                  const char* message) {
#if XIAOZHI_CARE_LED_BAR_ENABLED
    const bool handled = xiaozhi_unified_led_bar_care_alert(routine_id,
                                                            visual_pattern,
                                                            visual_color,
                                                            message);
    ESP_LOGI(kTag,
             "Care visual alert routed to unified WS2812B bar: routine=%s pattern=%d color=%d handled=%d",
             routine_id ? routine_id : "",
             visual_pattern,
             visual_color,
             handled ? 1 : 0);
    return handled;
#else
    (void)routine_id;
    (void)visual_pattern;
    (void)visual_color;
    (void)message;
    ESP_LOGW(kTag, "WS2812B LED bar disabled at compile time");
    return false;
#endif
}
