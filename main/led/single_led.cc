#include "single_led.h"
#include "application.h"
#include <esp_log.h>

#define TAG "SingleLed"

// Tira de 7 LEDs WS2812 en el GPIO48 (BUILTIN_LED_GPIO en config.h).
//
// Colores (pedido de Julio):
//   Azul    = escuchando
//   Verde   = hablando
//   Naranja = conectando (WiFi o servidor de XiaoZhi)
//   Rojo    = error
//   Apagado = en reposo
//
// El brillo es bajo a propósito: el equipo va en la mesita de luz.

#define LED_STRIP_COUNT 7

#define BRILLO_ALTO   16
#define BRILLO_MEDIO  8
#define BRILLO_BAJO   3

#define BLINK_INFINITE -1

// Naranja en WS2812: el verde ilumina mucho más que el rojo,
// por eso lleva sólo un tercio de verde.
#define NARANJA(b) (b), ((b) / 3), 0

SingleLed::SingleLed(gpio_num_t gpio) {
    if (gpio == GPIO_NUM_NC) {
        ESP_LOGW(TAG, "SingleLed initialized with GPIO_NUM_NC, LED will not function");
        return;
    }

    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = gpio;
    strip_config.max_leds = LED_STRIP_COUNT;
    strip_config.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
    strip_config.led_model = LED_MODEL_WS2812;

    led_strip_rmt_config_t rmt_config = {};
    rmt_config.resolution_hz = 10 * 1000 * 1000; // 10MHz

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip_));
    led_strip_clear(led_strip_);
    ESP_LOGI(TAG, "Tira WS2812 lista: %d LEDs en GPIO%d", LED_STRIP_COUNT, (int)gpio);

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
}

SingleLed::~SingleLed() {
    if (blink_timer_ != nullptr) {
        esp_timer_stop(blink_timer_);
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
    for (int i = 0; i < LED_STRIP_COUNT; ++i) {
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
        for (int i = 0; i < LED_STRIP_COUNT; ++i) {
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

// Se llama desde Application::Alert cuando hay un error
// (sin conexión al servidor, falla de descarga, etc.).
// El rojo queda fijo mientras el equipo está en reposo y se borra
// en cuanto se vuelve a usar (conectando, escuchando o hablando).
void SingleLed::ShowError() {
    error_active_ = true;
    SetColor(BRILLO_MEDIO, 0, 0);
    TurnOn();
}

void SingleLed::OnStateChanged() {
    auto& app = Application::GetInstance();
    auto device_state = app.GetDeviceState();
    switch (device_state) {
        case kDeviceStateStarting:
            // Arrancando y buscando WiFi: naranja parpadeando rápido
            SetColor(NARANJA(BRILLO_MEDIO));
            StartContinuousBlink(150);
            break;
        case kDeviceStateWifiConfiguring:
            // Sin WiFi configurado (modo de configuración): naranja lento
            SetColor(NARANJA(BRILLO_MEDIO));
            StartContinuousBlink(600);
            break;
        case kDeviceStateIdle:
            if (error_active_) {
                // Hubo un error: queda en rojo hasta el próximo uso
                SetColor(BRILLO_MEDIO, 0, 0);
                TurnOn();
            } else {
                TurnOff();
            }
            break;
        case kDeviceStateConnecting:
            // Conectando con el servidor de XiaoZhi: naranja fijo
            error_active_ = false;
            SetColor(NARANJA(BRILLO_MEDIO));
            TurnOn();
            break;
        case kDeviceStateListening:
        case kDeviceStateAudioTesting:
            // Escuchando: azul (más fuerte cuando detecta voz)
            error_active_ = false;
            if (app.IsVoiceDetected()) {
                SetColor(0, 0, BRILLO_ALTO);
            } else {
                SetColor(0, 0, BRILLO_BAJO + 3);
            }
            TurnOn();
            break;
        case kDeviceStateSpeaking:
        case kDeviceStateNotifying:
            // Hablando: verde
            error_active_ = false;
            SetColor(0, BRILLO_ALTO, 0);
            TurnOn();
            break;
        case kDeviceStateUpgrading:
            // Actualizando firmware o recursos: naranja muy rápido
            SetColor(NARANJA(BRILLO_MEDIO));
            StartContinuousBlink(100);
            break;
        case kDeviceStateActivating:
            // Activando / conectando al servidor por primera vez: naranja
            SetColor(NARANJA(BRILLO_MEDIO));
            StartContinuousBlink(500);
            break;
        case kDeviceStateFatalError:
            // Error grave: rojo parpadeando
            error_active_ = true;
            SetColor(BRILLO_ALTO, 0, 0);
            StartContinuousBlink(300);
            break;
        default:
            ESP_LOGW(TAG, "Unknown led strip event: %d", device_state);
            return;
    }
}
