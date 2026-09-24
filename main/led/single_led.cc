#include "single_led.h"
#include "application.h"
#include <esp_log.h> 

#define TAG "SingleLed"
#define LED_COUNT 12

#define DEFAULT_BRIGHTNESS 4
#define HIGH_BRIGHTNESS 16
#define LOW_BRIGHTNESS 2

#define BLINK_INFINITE -1

// DP040C_FASE1_CARE_VISUAL_OVERLAY
namespace {
SingleLed* g_xiaozhi_care_led = nullptr;
constexpr uint64_t CARE_ALERT_DURATION_US = 12ULL * 1000ULL * 1000ULL;
}


SingleLed::SingleLed(gpio_num_t gpio) {
    if (gpio == GPIO_NUM_NC) {
        ESP_LOGW(TAG, "SingleLed initialized with GPIO_NUM_NC, LED will not function");
        return;
    }

    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = gpio;
    strip_config.max_leds = LED_COUNT;
    strip_config.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
    strip_config.led_model = LED_MODEL_WS2812;

    led_strip_rmt_config_t rmt_config = {};
    rmt_config.resolution_hz = 10 * 1000 * 1000; // 10MHz

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip_));
    led_strip_clear(led_strip_);

    esp_timer_create_args_t blink_timer_args = {
        .callback = [](void *arg) {
            auto led = static_cast<SingleLed*>(arg);
            led->OnBlinkTimer();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "blink_timer",
        .skip_unhandled_events = false,
    };
    ESP_ERROR_CHECK(esp_timer_create(&blink_timer_args, &blink_timer_));

    esp_timer_create_args_t care_alert_timer_args = {
        .callback = [](void *arg) {
            auto led = static_cast<SingleLed*>(arg);
            led->OnCareAlertTimeout();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "care_led_alert",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&care_alert_timer_args, &care_alert_timer_));

    g_xiaozhi_care_led = this;
    ESP_LOGI(TAG, "DP-040C unified Care LED bridge ready: leds=%d", LED_COUNT);
}

SingleLed::~SingleLed() {
    if (care_alert_timer_ != nullptr) {
        esp_timer_stop(care_alert_timer_);
        esp_timer_delete(care_alert_timer_);
        care_alert_timer_ = nullptr;
    }
    if (blink_timer_ != nullptr) {
        esp_timer_stop(blink_timer_);
    }
    if (g_xiaozhi_care_led == this) {
        g_xiaozhi_care_led = nullptr;
    }
    if (led_strip_ != nullptr) {
        led_strip_del(led_strip_);
    }
}

void SingleLed::SetColor(uint8_t r, uint8_t g, uint8_t b) {
    r_ = r;
    g_ = g;
    b_ = b;
}

void SingleLed::TurnOn() {
    if (led_strip_ == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    esp_timer_stop(blink_timer_);

    for (int i = 0; i < LED_COUNT; i++) {
        led_strip_set_pixel(led_strip_, i, r_, g_, b_);
    }

    led_strip_refresh(led_strip_);
}

void SingleLed::TurnOff() {
    if (led_strip_ == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    esp_timer_stop(blink_timer_);
    led_strip_clear(led_strip_);
}

void SingleLed::BlinkOnce() {
    Blink(1, 100);
}

void SingleLed::Blink(int times, int interval_ms) {
    StartBlinkTask(times, interval_ms);
}

void SingleLed::StartContinuousBlink(int interval_ms) {
    StartBlinkTask(BLINK_INFINITE, interval_ms);
}

void SingleLed::StartBlinkTask(int times, int interval_ms) {
    if (led_strip_ == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    esp_timer_stop(blink_timer_);
    
    blink_counter_ = times * 2;
    blink_interval_ms_ = interval_ms;
    esp_timer_start_periodic(blink_timer_, interval_ms * 1000);
}

void SingleLed::OnBlinkTimer() {
    std::lock_guard<std::mutex> lock(mutex_);

    blink_counter_--;

    if (blink_counter_ & 1) {

        for (int i = 0; i < LED_COUNT; i++) {
            led_strip_set_pixel(led_strip_, i, r_, g_, b_);
        }

        led_strip_refresh(led_strip_);

    } else {

        led_strip_clear(led_strip_);

        if (blink_counter_ == 0) {
            esp_timer_stop(blink_timer_);
        }
    }
}


// DP040C_FASE1_CARE_VISUAL_OVERLAY
bool SingleLed::ShowCareAlert(int visual_pattern, int visual_color, const char* message) {
    if (led_strip_ == nullptr) {
        ESP_LOGW(TAG, "DP-040C Care visual ignored: LED strip is not initialized");
        return false;
    }

    // 0=None, 1=Solid, 2=Breathing, 3=SoftBlink, 4=ProgressBar, 5=PillboxSlot.
    if (visual_pattern == 0) {
        return true;
    }

    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    // 0=Auto, 1=Green, 2=Blue, 3=Yellow, 4=SoftRed, 5=Violet, 6=White.
    switch (visual_color) {
        case 1:
            g = HIGH_BRIGHTNESS;
            break;
        case 2:
            b = HIGH_BRIGHTNESS;
            break;
        case 3:
            r = HIGH_BRIGHTNESS;
            g = HIGH_BRIGHTNESS;
            break;
        case 4:
            r = HIGH_BRIGHTNESS;
            g = LOW_BRIGHTNESS;
            break;
        case 5:
            r = HIGH_BRIGHTNESS;
            b = HIGH_BRIGHTNESS;
            break;
        case 6:
            r = HIGH_BRIGHTNESS;
            g = HIGH_BRIGHTNESS;
            b = HIGH_BRIGHTNESS;
            break;
        case 0:
        default:
            if (visual_pattern == 5) {
                // Pastillero: ámbar cálido, distinto de verde/azul.
                r = HIGH_BRIGHTNESS;
                g = HIGH_BRIGHTNESS / 2;
            } else {
                r = HIGH_BRIGHTNESS;
                b = HIGH_BRIGHTNESS;
            }
            break;
    }

    care_alert_active_.store(true);
    SetColor(r, g, b);

    switch (visual_pattern) {
        case 1:
            TurnOn();
            break;
        case 2:
            StartContinuousBlink(900);
            break;
        case 3:
            StartContinuousBlink(550);
            break;
        case 4:
            StartContinuousBlink(350);
            break;
        case 5:
            StartContinuousBlink(450);
            break;
        default:
            StartContinuousBlink(700);
            break;
    }

    if (care_alert_timer_ != nullptr) {
        esp_timer_stop(care_alert_timer_);
        esp_timer_start_once(care_alert_timer_, CARE_ALERT_DURATION_US);
    }

    ESP_LOGI(TAG,
             "DP-040C Care visual active: pattern=%d color=%d duration_ms=12000 message=%s",
             visual_pattern,
             visual_color,
             message ? message : "");
    return true;
}

void SingleLed::ClearCareAlert() {
    const bool was_active = care_alert_active_.exchange(false);
    if (care_alert_timer_ != nullptr) {
        esp_timer_stop(care_alert_timer_);
    }

    if (was_active) {
        ESP_LOGI(TAG, "DP-040C Care visual cleared; restoring XiaoZhi state");
        OnStateChanged();
    }
}

void SingleLed::OnCareAlertTimeout() {
    if (!care_alert_active_.exchange(false)) {
        return;
    }

    ESP_LOGI(TAG, "DP-040C Care visual timeout; restoring XiaoZhi state");
    OnStateChanged();
}


void SingleLed::OnStateChanged() {
    auto& app = Application::GetInstance();
    auto device_state = app.GetDeviceState();

    // DP040C_FASE1_CARE_VISUAL_OVERLAY
    // La alarma visual conserva prioridad durante Notifying/Idle.
    // Si la persona empieza a interactuar, XiaoZhi recupera inmediatamente
    // sus colores normales (escucha=verde, habla=azul).
    if (care_alert_active_.load()) {
        if (device_state == kDeviceStateListening ||
            device_state == kDeviceStateSpeaking ||
            device_state == kDeviceStateConnecting ||
            device_state == kDeviceStateWifiConfiguring ||
            device_state == kDeviceStateUpgrading ||
            device_state == kDeviceStateActivating) {
            care_alert_active_.store(false);
            if (care_alert_timer_ != nullptr) {
                esp_timer_stop(care_alert_timer_);
            }
            ESP_LOGI(TAG,
                     "DP-040C Care visual interrupted by XiaoZhi state=%d",
                     static_cast<int>(device_state));
        } else {
            return;
        }
    }
    switch (device_state) {
        case kDeviceStateStarting:
            SetColor(0, 0, DEFAULT_BRIGHTNESS);
            StartContinuousBlink(100);
            break;
        case kDeviceStateWifiConfiguring:
            SetColor(0, 0, DEFAULT_BRIGHTNESS);
            StartContinuousBlink(500);
            break;
        case kDeviceStateIdle:
            TurnOff();
            break;
        case kDeviceStateConnecting:
            SetColor(0, 0, DEFAULT_BRIGHTNESS);
            TurnOn();
            break;
        case kDeviceStateListening:
    // Escuchando = VERDE
        SetColor(0, HIGH_BRIGHTNESS, 0);
        TurnOn();
        break;
        case kDeviceStateAudioTesting:
            if (app.IsVoiceDetected()) {
                SetColor(HIGH_BRIGHTNESS, 0, 0);
            } else {
                SetColor(LOW_BRIGHTNESS, 0, 0);
            }
            TurnOn();
            break;
        case kDeviceStateSpeaking:
         // Hablando / respondiendo = AZUL
    SetColor(0, 0, HIGH_BRIGHTNESS);
    TurnOn();
    break;
        case kDeviceStateNotifying:
            SetColor(0, DEFAULT_BRIGHTNESS, 0);
            TurnOn();
            break;
        case kDeviceStateUpgrading:
            SetColor(0, DEFAULT_BRIGHTNESS, 0);
            StartContinuousBlink(100);
            break;
        case kDeviceStateActivating:
            SetColor(0, DEFAULT_BRIGHTNESS, 0);
            StartContinuousBlink(500);
            break;
        default:
            ESP_LOGW(TAG, "Unknown led strip event: %d", device_state);
            return;
    }
}

// DP040C_FASE1_CARE_VISUAL_OVERLAY
// Implementación FUERTE del hook que care-daily declara weak.
extern "C" bool xiaozhi_care_alarm_visual_notify(const char* routine_id,
                                                  int visual_pattern,
                                                  int visual_color,
                                                  const char* message) {
    if (g_xiaozhi_care_led == nullptr) {
        ESP_LOGW(TAG,
                 "DP-040C Care visual hook called before SingleLed initialization");
        return false;
    }

    const bool handled =
        g_xiaozhi_care_led->ShowCareAlert(visual_pattern, visual_color, message);

    ESP_LOGI(TAG,
             "DP-040C Care visual hook: routine=%s pattern=%d color=%d handled=%d",
             routine_id ? routine_id : "",
             visual_pattern,
             visual_color,
             handled ? 1 : 0);
    return handled;
}

extern "C" void xiaozhi_care_alarm_visual_clear(void) {
    if (g_xiaozhi_care_led != nullptr) {
        g_xiaozhi_care_led->ClearCareAlert();
    }
}

