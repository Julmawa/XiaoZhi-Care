#pragma once

/**
 * DP-014-r3 — Unified WS2812B 16 LED Bar
 *
 * XiaoZhi and XiaoZhi Care share one physical WS2812B/NeoPixel bar.
 * The strip is initialized by the board LED implementation (SingleLed),
 * and this component only routes Care visual alarms to that unified driver.
 *
 * Default wiring:
 *   WS2812B/NeoPixel DIN -> GPIO 38
 *   WS2812B/NeoPixel 5V  -> 5V external/board 5V
 *   WS2812B/NeoPixel GND -> common GND with ESP32-S3
 */

#ifndef XIAOZHI_CARE_LED_BAR_ENABLED
#define XIAOZHI_CARE_LED_BAR_ENABLED 1
#endif

#ifndef XIAOZHI_CARE_LED_BAR_GPIO
#define XIAOZHI_CARE_LED_BAR_GPIO 38
#endif

#ifndef XIAOZHI_CARE_LED_BAR_COUNT
#define XIAOZHI_CARE_LED_BAR_COUNT 16
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Check whether the unified WS2812B bar is ready. */
bool xiaozhi_care_led_bar_start(void);

/** @brief Clear the care visual alert overlay. */
void xiaozhi_care_led_bar_clear(void);

#ifdef __cplusplus
}
#endif
