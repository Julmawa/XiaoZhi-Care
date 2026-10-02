#include "application.h"
#include "care_radio/radio_service.h"
#include "assets.h"
#include "assets/lang_config.h"
#include "audio_codec.h"
#include "board.h"
#include "care_listening/listening_settings.h"
#include "care_web_server.h"
#include "display.h"
#include "mcp_server.h"
#include "mqtt_protocol.h"
#include "settings.h"
#include "system_info.h"
#include "text_glyph_payload.h"
#include "websocket_protocol.h"

#include <driver/gpio.h>
#include <esp_log.h>
#include <arpa/inet.h>
#include <cJSON.h>
#include <cstring>
#include <limits>

// DP044B_UNIVERSAL_ACK
extern "C" bool xiaozhi_care_ack_pending_for_target(const char* target_id);
extern "C" bool xiaozhi_care_acknowledge_target(const char* target_id);
extern "C" uint32_t xiaozhi_care_ack_prompt_delay_seconds(void);
extern "C" uint8_t xiaozhi_care_ack_max_presentations(void);

// DP044B1_3_V2_BRIDGE
// Mantiene main desacoplado de care-voice, siguiendo el patrón de puentes C
// que ya usa XiaoZhi Care para ACK y reproducción de recordatorios.
extern "C" bool xiaozhi_care_ack_prompt_load(std::string* audio_bytes,
                                              uint32_t* duration_ms);

#define TAG "Application"

// DP039_DIAGNOSTICO_LIMPIO: sólo se retiraron trazas de medición.
// DP039_CARE_SLOW_SPEECH
// DP039_FASE2_CONFIGURABLE
// Endpoint CARE basado en actividad PCM real, no en WebRTC VAD.
// fast=1000 ms, medium=1500 ms, slow=2500 ms.
namespace {
constexpr uint64_t kCareSlowSpeechInitialWaitUs = 8000000ULL;
constexpr uint64_t kCareSlowSpeechHardLimitUs = 30000000ULL;
// DP040C_OLED_AUTO_RETURN
// Mantener el mensaje del recordatorio visible un tiempo breve y luego
// devolver el OLED al estado normal. Coincide con el timeout visual WS2812B.
constexpr uint64_t kCareReminderDisplayTimeoutUs = 12ULL * 1000ULL * 1000ULL;

// DP044A_REMINDER_AUDIO_PRIORITY
// Breve colchón para que el PCM de la radio se vacíe antes del recordatorio
// y para no reanudarla inmediatamente al terminar la última muestra de audio.
constexpr uint64_t kCareReminderAudioPreRollUs = 250ULL * 1000ULL;
constexpr uint64_t kCareReminderAudioPostRollUs = 350ULL * 1000ULL;
constexpr int kCareReminderMinVolume = 70;

// DP044B1_4_EXTENDED_ACK_WINDOW
// La persona dispone de 30 segundos para confirmar el aviso. Dentro de esa
// ventana aceptamos hasta tres respuestas STT y, si el canal de voz se cierra,
// XiaoZhi Care puede reabrir automáticamente hasta tres ciclos de escucha.
constexpr int64_t kCareAckResponseWindowUs = 30LL * 1000LL * 1000LL;
constexpr uint8_t kCareAckMaxResponseAttempts = 3;
constexpr uint8_t kCareAckMaxListeningCycles = 3;

// DP044B1_4B_DELAYED_ACK_PROMPT
// La confirmación no debe interrumpir inmediatamente a la persona.
// En recordatorios con audio local, el minuto se cuenta desde que termina
// por completo el aviso (incluido el post-roll de DP044A).
constexpr int64_t kCareAckPromptDelayUs = 60LL * 1000LL * 1000LL;

std::string NormalizeCareAckText(const std::string& text) {
    std::string normalized;
    normalized.reserve(text.size());

    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == 0xC3 && i + 1 < text.size()) {
            const unsigned char n = static_cast<unsigned char>(text[i + 1]);
            char replacement = 0;
            switch (n) {
                case 0x81: case 0xA1: replacement = 'a'; break;  // Á á
                case 0x89: case 0xA9: replacement = 'e'; break;  // É é
                case 0x8D: case 0xAD: replacement = 'i'; break;  // Í í
                case 0x93: case 0xB3: replacement = 'o'; break;  // Ó ó
                case 0x9A: case 0xBA: replacement = 'u'; break;  // Ú ú
                case 0x91: case 0xB1: replacement = 'n'; break;  // Ñ ñ
                default: break;
            }
            if (replacement != 0) {
                normalized.push_back(replacement);
                i += 2;
                continue;
            }
        }

        char out = static_cast<char>(c);
        if (out >= 'A' && out <= 'Z') out = static_cast<char>(out - 'A' + 'a');
        const bool alnum = (out >= 'a' && out <= 'z') || (out >= '0' && out <= '9');
        if (alnum) {
            normalized.push_back(out);
        } else if (!normalized.empty() && normalized.back() != ' ') {
            normalized.push_back(' ');
        }
        ++i;
    }

    while (!normalized.empty() && normalized.front() == ' ') normalized.erase(normalized.begin());
    while (!normalized.empty() && normalized.back() == ' ') normalized.pop_back();
    return normalized;
}

bool IsCareAckAffirmativeText(const std::string& text) {
    const std::string value = NormalizeCareAckText(text);

    // DP044B1_7_CONSERVATIVE_ACK_MATCHER
    // DP044B1_7A_PLURAL_ACK_VARIANTS
    // Sólo coincidencias EXACTAS. No usamos aceptación por prefijo de "si":
    // una frase contaminada por TV/ruido como "si la culpa gracias" debe
    // seguir siendo rechazada.
    static constexpr const char* kAffirmative[] = {
        "si",
        "si gracias",

        // Respuestas naturales con pronombre, singular.
        "si lo escuche",
        "si la escuche",
        "si ya lo escuche",
        "si ya la escuche",
        "lo escuche",
        "la escuche",
        "ya lo escuche",
        "ya la escuche",

        // Variantes plurales observadas en STT, p. ej. "Sí, ya lo escuchamos".
        "si lo escuchamos",
        "si la escuchamos",
        "si ya lo escuchamos",
        "si ya la escuchamos",
        "lo escuchamos",
        "la escuchamos",
        "ya lo escuchamos",
        "ya la escuchamos",

        // Respuestas que nombran explícitamente qué se escuchó.
        "si escuche el recordatorio",
        "si ya escuche el recordatorio",
        "escuche el recordatorio",
        "ya escuche el recordatorio",
        "si escuchamos el recordatorio",
        "si ya escuchamos el recordatorio",
        "escuchamos el recordatorio",
        "ya escuchamos el recordatorio",

        "si escuche la alarma",
        "si ya escuche la alarma",
        "escuche la alarma",
        "ya escuche la alarma",
        "si escuchamos la alarma",
        "si ya escuchamos la alarma",
        "escuchamos la alarma",
        "ya escuchamos la alarma",

        "si escuche el aviso",
        "si ya escuche el aviso",
        "escuche el aviso",
        "ya escuche el aviso",
        "si escuchamos el aviso",
        "si ya escuchamos el aviso",
        "escuchamos el aviso",
        "ya escuchamos el aviso",

        // Confirmaciones cortas ya validadas.
        "entendido",
        "recibido",
        "esta bien",
        "de acuerdo",
        "ok",
        "okay",

        "si se le escucha",
        "se le escucha",
        "se escucha la alarma",
        "si se escucha la alarma",
        "si le escuche",
        "si ya le escuche",
        "si le escuche gracias",
        "si ya le escuche gracias",
        "si la escuchamos gracias",
        "si ya la escuchamos gracias",
        "oi la alarma",
        "ya oi la alarma",
        "si oi la alarma",
        "si ya oi la alarma",
        "lo confirmo",
        "si lo confirmo",
        // "gracias" solo no confirma recepción: puede pertenecer a una
        // conversación causada por un reconocimiento STT incorrecto.
    };
    for (const char* candidate : kAffirmative) {
        if (value == candidate) return true;
    }
    return false;
}
}  // namespace


Application::Application() : notify_player_(audio_service_) {
    event_group_ = xEventGroupCreate();

    xiaozhi_care::listening::ListeningSettings::GetInstance().Init();

#if CONFIG_USE_DEVICE_AEC && CONFIG_USE_SERVER_AEC
#error "CONFIG_USE_DEVICE_AEC and CONFIG_USE_SERVER_AEC cannot be enabled at the same time"
#elif CONFIG_USE_DEVICE_AEC
    aec_mode_ = kAecOnDeviceSide;
#elif CONFIG_USE_SERVER_AEC
    aec_mode_ = kAecOnServerSide;
#else
    aec_mode_ = kAecOff;
#endif

    esp_timer_create_args_t clock_timer_args = {.callback =
                                                    [](void* arg) {
                                                        Application* app = (Application*)arg;
                                                        xEventGroupSetBits(app->event_group_,
                                                                           MAIN_EVENT_CLOCK_TICK);
                                                    },
                                                .arg = this,
                                                .dispatch_method = ESP_TIMER_TASK,
                                                .name = "clock_timer",
                                                .skip_unhandled_events = true};
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);

    // DP-039: one-shot local end-of-turn timer.
    esp_timer_create_args_t slow_speech_timer_args = {
        .callback =
            [](void* arg) {
                Application* app = static_cast<Application*>(arg);
                xEventGroupSetBits(app->event_group_, MAIN_EVENT_SLOW_SPEECH_TIMEOUT);
            },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "care_slow_speech",
        .skip_unhandled_events = true,
    };

    const esp_err_t slow_timer_err =
        esp_timer_create(&slow_speech_timer_args, &slow_speech_timer_handle_);
    if (slow_timer_err != ESP_OK) {
        slow_speech_timer_handle_ = nullptr;
        ESP_LOGE(TAG,
                 "DP-039 failed to create slow speech timer: err=%d",
                 static_cast<int>(slow_timer_err));
    }

    // DP044B1_4_EXTENDED_ACK_WINDOW
    esp_timer_create_args_t care_ack_response_timer_args = {
        .callback =
            [](void* arg) {
                auto* app = static_cast<Application*>(arg);
                app->Schedule([app]() { app->HandleCareAckResponseTimeout(); });
            },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "care_ack_window",
        .skip_unhandled_events = true,
    };
    const esp_err_t care_ack_timer_err =
        esp_timer_create(&care_ack_response_timer_args,
                         &care_ack_response_timer_handle_);
    if (care_ack_timer_err != ESP_OK) {
        care_ack_response_timer_handle_ = nullptr;
        ESP_LOGE(TAG,
                 "DP044B1.4 failed to create ACK response timer: err=%d",
                 static_cast<int>(care_ack_timer_err));
    }

    // DP044B1_4B_DELAYED_ACK_PROMPT
    esp_timer_create_args_t care_ack_prompt_delay_timer_args = {
        .callback =
            [](void* arg) {
                auto* app = static_cast<Application*>(arg);
                app->Schedule([app]() { app->HandleCareAckPromptDelayElapsed(); });
            },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "care_ack_delay",
        .skip_unhandled_events = true,
    };
    const esp_err_t care_ack_delay_timer_err =
        esp_timer_create(&care_ack_prompt_delay_timer_args,
                         &care_ack_prompt_delay_timer_handle_);
    if (care_ack_delay_timer_err != ESP_OK) {
        care_ack_prompt_delay_timer_handle_ = nullptr;
        ESP_LOGE(TAG,
                 "DP044B1.4b failed to create ACK prompt delay timer: err=%d",
                 static_cast<int>(care_ack_delay_timer_err));
    }
}

Application::~Application() {
    notify_player_.Stop();

    // DP040C_OLED_AUTO_RETURN
    if (care_reminder_display_timer_handle_ != nullptr) {
        esp_timer_stop(care_reminder_display_timer_handle_);
        esp_timer_delete(care_reminder_display_timer_handle_);
        care_reminder_display_timer_handle_ = nullptr;
    }

    // DP044A_REMINDER_AUDIO_PRIORITY
    if (care_reminder_audio_pre_timer_handle_ != nullptr) {
        esp_timer_stop(care_reminder_audio_pre_timer_handle_);
        esp_timer_delete(care_reminder_audio_pre_timer_handle_);
        care_reminder_audio_pre_timer_handle_ = nullptr;
    }
    if (care_reminder_audio_post_timer_handle_ != nullptr) {
        esp_timer_stop(care_reminder_audio_post_timer_handle_);
        esp_timer_delete(care_reminder_audio_post_timer_handle_);
        care_reminder_audio_post_timer_handle_ = nullptr;
    }

    // DP044B1_4_EXTENDED_ACK_WINDOW
    if (care_ack_response_timer_handle_ != nullptr) {
        esp_timer_stop(care_ack_response_timer_handle_);
        esp_timer_delete(care_ack_response_timer_handle_);
        care_ack_response_timer_handle_ = nullptr;
    }

    // DP044B1_4B_DELAYED_ACK_PROMPT
    if (care_ack_prompt_delay_timer_handle_ != nullptr) {
        esp_timer_stop(care_ack_prompt_delay_timer_handle_);
        esp_timer_delete(care_ack_prompt_delay_timer_handle_);
        care_ack_prompt_delay_timer_handle_ = nullptr;
    }

    CancelSlowSpeechTimer();
    if (slow_speech_timer_handle_ != nullptr) {
        esp_timer_delete(slow_speech_timer_handle_);
        slow_speech_timer_handle_ = nullptr;
    }

    if (clock_timer_handle_ != nullptr) {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }
    vEventGroupDelete(event_group_);
}

bool Application::SetDeviceState(DeviceState state) { return state_machine_.TransitionTo(state); }

void Application::Initialize() {
    auto& board = Board::GetInstance();
    SetDeviceState(kDeviceStateStarting);

    // Setup the display
    auto display = board.GetDisplay();
    display->SetupUI();
    // Print board name/version info
    display->SetChatMessage("system", SystemInfo::GetUserAgent().c_str());

    // Setup the audio service
    auto codec = board.GetAudioCodec();
    audio_service_.Initialize(codec);
    audio_service_.Start();

    // DP042A_RADIO_MP3_BASE
    auto& care_radio =
        xiaozhi_care::radio::RadioService::GetInstance();
    care_radio.Init(
        [this](std::vector<int16_t>& pcm,
               int sample_rate,
               int channels) -> bool {
            return audio_service_.OutputExternalPcm(
                pcm, sample_rate, channels);
        });
    care_radio.SetSystemPaused(
        GetDeviceState() != kDeviceStateIdle);

    // DP043C_RADIO_FINISHING
    care_radio.SetNetworkActivityCallback([this](bool active) {
        Schedule([this, active]() {
            auto& b = Board::GetInstance();

            if (active) {
                b.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
                ESP_LOGI(TAG, "Radio network active: WiFi PERFORMANCE");
            } else if (GetDeviceState() == kDeviceStateIdle) {
                b.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
                ESP_LOGI(TAG, "Radio stopped in idle: WiFi LOW_POWER");
            }
        });
    });

    // DP039_AUDIO_VOLUME_WEB
    xiaozhi_care::CareWebServer::GetInstance().SetAudioVolumeCallbacks(
        [codec]() -> int {
            return codec->output_volume();
        },
        [codec](int volume) -> bool {
            if (volume < 10 || volume > 100) {
                return false;
            }
            codec->SetOutputVolume(volume);
            return true;
        });
    ESP_LOGI(TAG, "After board/audio init");
    SystemInfo::PrintHeapStats();

    AudioServiceCallbacks callbacks;
    callbacks.on_send_queue_available = [this]() {
        xEventGroupSetBits(event_group_, MAIN_EVENT_SEND_AUDIO);
    };
    callbacks.on_wake_word_detected = [this](const std::string& wake_word) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_WAKE_WORD_DETECTED);
    };
    callbacks.on_vad_change = [this](bool speaking) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_VAD_CHANGE);
    };
    callbacks.on_input_activity = [this](bool active, uint32_t level, uint32_t noise_floor) {
        (void)active;
        (void)level;
        (void)noise_floor;
        xEventGroupSetBits(event_group_, MAIN_EVENT_INPUT_ACTIVITY);
    };
    callbacks.on_playback_drained = [this]() {
        xEventGroupSetBits(event_group_, MAIN_EVENT_PLAYBACK_DRAINED);
    };
    callbacks.on_playback_progress = [this](uint32_t playback_id, uint32_t media_position_ms) {
        notify_player_.OnPlaybackProgress(playback_id, media_position_ms);
    };
    audio_service_.SetCallbacks(callbacks);

    // Add state change listeners
    state_machine_.AddStateChangeListener([this](DeviceState old_state, DeviceState new_state) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_STATE_CHANGED);
    });

    // Start the clock timer to update the status bar
    esp_timer_start_periodic(clock_timer_handle_, 1000000);

    // Add MCP common tools (only once during initialization)
    auto& mcp_server = McpServer::GetInstance();
    mcp_server.AddCommonTools();
    mcp_server.AddUserOnlyTools();

    // Set network event callback for UI updates and network state handling
    board.SetNetworkEventCallback([this](NetworkEvent event, const std::string& data) {
        auto display = Board::GetInstance().GetDisplay();

        switch (event) {
            case NetworkEvent::Scanning:
                display->ShowNotification(Lang::Strings::SCANNING_WIFI, 30000);
                xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_DISCONNECTED);
                break;
            case NetworkEvent::Connecting: {
                if (data.empty()) {
                    // Cellular network - registering without carrier info yet
                    display->SetStatus(Lang::Strings::REGISTERING_NETWORK);
                } else {
                    // WiFi or cellular with carrier info
                    std::string msg = Lang::Strings::CONNECT_TO;
                    msg += data;
                    msg += "...";
                    display->ShowNotification(msg.c_str(), 30000);
                }
                break;
            }
            case NetworkEvent::Connected: {
                std::string msg = Lang::Strings::CONNECTED_TO;
                msg += data;
                display->ShowNotification(msg.c_str(), 30000);
                xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_CONNECTED);
                break;
            }
            case NetworkEvent::Disconnected:
                xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_DISCONNECTED);
                break;
            case NetworkEvent::WifiConfigModeEnter:
                // WiFi config mode enter is handled by WifiBoard internally
                break;
            case NetworkEvent::WifiConfigModeExit:
                // WiFi config mode exit is handled by WifiBoard internally
                break;
            // Cellular modem specific events
            case NetworkEvent::ModemDetecting:
                display->SetStatus(Lang::Strings::DETECTING_MODULE);
                break;
            case NetworkEvent::ModemErrorNoSim:
                Alert(Lang::Strings::ERROR, Lang::Strings::PIN_ERROR, "warning",
                      Lang::Sounds::OGG_ERR_PIN);
                break;
            case NetworkEvent::ModemErrorRegDenied:
                Alert(Lang::Strings::ERROR, Lang::Strings::REG_ERROR, "warning",
                      Lang::Sounds::OGG_ERR_REG);
                break;
            case NetworkEvent::ModemErrorInitFailed:
                Alert(Lang::Strings::ERROR, Lang::Strings::MODEM_INIT_ERROR, "warning",
                      Lang::Sounds::OGG_EXCLAMATION);
                break;
            case NetworkEvent::ModemErrorTimeout:
                display->SetStatus(Lang::Strings::REGISTERING_NETWORK);
                break;
        }
    });

    // Start network asynchronously
    board.StartNetwork();

    // Update the status bar immediately to show the network state
    display->UpdateStatusBar(true);
}

void Application::Run() {
    // Set the priority of the main task to 10
    vTaskPrioritySet(nullptr, 10);

    const EventBits_t ALL_EVENTS =
        MAIN_EVENT_SCHEDULE | MAIN_EVENT_SEND_AUDIO | MAIN_EVENT_WAKE_WORD_DETECTED |
        MAIN_EVENT_VAD_CHANGE | MAIN_EVENT_CLOCK_TICK | MAIN_EVENT_ERROR |
        MAIN_EVENT_NETWORK_CONNECTED | MAIN_EVENT_NETWORK_DISCONNECTED | MAIN_EVENT_TOGGLE_CHAT |
        MAIN_EVENT_START_LISTENING | MAIN_EVENT_STOP_LISTENING | MAIN_EVENT_ACTIVATION_DONE |
        MAIN_EVENT_STATE_CHANGED | MAIN_EVENT_PLAYBACK_DRAINED | MAIN_EVENT_SLOW_SPEECH_TIMEOUT |
        MAIN_EVENT_INPUT_ACTIVITY;

    while (true) {
        auto bits = xEventGroupWaitBits(event_group_, ALL_EVENTS, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & MAIN_EVENT_ERROR) {
            if (GetDeviceState() == kDeviceStateNotifying) {
                StopNotification();
            }
            SetDeviceState(kDeviceStateIdle);
            Alert(Lang::Strings::ERROR, last_error_message_.c_str(), "cancel",
                  Lang::Sounds::OGG_EXCLAMATION);
        }

        if (bits & MAIN_EVENT_NETWORK_CONNECTED) {
            HandleNetworkConnectedEvent();
        }

        if (bits & MAIN_EVENT_NETWORK_DISCONNECTED) {
            HandleNetworkDisconnectedEvent();
        }

        if (bits & MAIN_EVENT_ACTIVATION_DONE) {
            HandleActivationDoneEvent();
        }

        if (bits & MAIN_EVENT_STATE_CHANGED) {
            HandleStateChangedEvent();
        }

        if (bits & MAIN_EVENT_PLAYBACK_DRAINED) {
            if (audio_service_.IsPlaybackIdle()) {
                notify_player_.OnPlaybackDrained();

                // DP044A_REMINDER_AUDIO_PRIORITY
                // El drenado de la cola es la señal autoritativa de que el OGG
                // local terminó. La radio permanece pausada durante un pequeño
                // post-roll y sólo entonces se libera.
                if (care_reminder_audio_active_) {
                    BeginCareReminderAudioPostRoll();
                } else if (care_reminder_audio_pending_) {
                    MaybeStartCareReminderAudio();
                }
            }
            // DP044B_UNIVERSAL_ACK
            // Un aviso sin OGG propio puede estar esperando que termine un beep/TTS
            // antes de abrir la escucha de confirmación.
            MaybeStartCareAcknowledgementListening();

            // Deferred listening start (auto mode): the playback queue has
            // drained, so it is now safe to enable voice processing.
            if (pending_listening_start_ && GetDeviceState() == kDeviceStateListening &&
                audio_service_.IsPlaybackIdle()) {
                pending_listening_start_ = false;
                StartListeningAudio();
            }
        }

        if (bits & MAIN_EVENT_TOGGLE_CHAT) {
            HandleToggleChatEvent();
        }

        if (bits & MAIN_EVENT_START_LISTENING) {
            HandleStartListeningEvent();
        }

        if (bits & MAIN_EVENT_STOP_LISTENING) {
            HandleStopListeningEvent();
        }

        if (bits & MAIN_EVENT_SEND_AUDIO) {
            while (auto packet = audio_service_.PopPacketFromSendQueue()) {
                if (protocol_ && !protocol_->SendAudio(std::move(packet))) {
                    // Drop the remaining packets. Leaving them in the queue would
                    // stall the Opus codec task (it waits for queue space), which in
                    // turn deadlocks the whole audio input pipeline, as no new
                    // MAIN_EVENT_SEND_AUDIO event would ever be triggered again.
                    while (audio_service_.PopPacketFromSendQueue())
                        ;
                    break;
                }
            }
        }

        if (bits & MAIN_EVENT_WAKE_WORD_DETECTED) {
            HandleWakeWordDetectedEvent();
        }

        if (bits & MAIN_EVENT_VAD_CHANGE) {
            if (GetDeviceState() == kDeviceStateListening) {
                auto led = Board::GetInstance().GetLed();
                led->OnStateChanged();
            }
        }

        if (bits & MAIN_EVENT_INPUT_ACTIVITY) {
            if (GetDeviceState() == kDeviceStateListening &&
                listening_mode_ == kListeningModeCareSlow) {
                HandleSlowSpeechInputActivity();
            }
        }

        if (bits & MAIN_EVENT_SLOW_SPEECH_TIMEOUT) {
            HandleSlowSpeechTimeout();
        }

        if (bits & MAIN_EVENT_SCHEDULE) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto tasks = std::move(main_tasks_);
            lock.unlock();
            for (auto& task : tasks) {
                task();
            }
        }

        if (bits & MAIN_EVENT_CLOCK_TICK) {
            clock_ticks_++;
            auto display = Board::GetInstance().GetDisplay();
            display->UpdateStatusBar();

            // Print debug info every 10 seconds
            if (clock_ticks_ % 10 == 0) {
                SystemInfo::PrintHeapStats();
                // SystemInfo::PrintTaskList();
                // SystemInfo::PrintTaskCpuUsage(pdMS_TO_TICKS(1000));
            }
        }
    }
}

void Application::HandleNetworkConnectedEvent() {
    ESP_LOGI(TAG, "Network connected");
    auto state = GetDeviceState();

    if (state == kDeviceStateStarting || state == kDeviceStateWifiConfiguring) {
        // Network is ready, start activation
        SetDeviceState(kDeviceStateActivating);
        if (activation_task_handle_ != nullptr) {
            ESP_LOGW(TAG, "Activation task already running");
            return;
        }

        xTaskCreate(
            [](void* arg) {
                Application* app = static_cast<Application*>(arg);
                app->ActivationTask();
                app->activation_task_handle_ = nullptr;
                vTaskDelete(NULL);
            },
            "activation", 4096 * 2, this, 2, &activation_task_handle_);
    }

    // Update the status bar immediately to show the network state
    auto display = Board::GetInstance().GetDisplay();
    display->UpdateStatusBar(true);
}

void Application::HandleNetworkDisconnectedEvent() {
    // Close current conversation when network disconnected
    auto state = GetDeviceState();
    if (state == kDeviceStateNotifying) {
        StopNotification();
    }
    if (state == kDeviceStateConnecting || state == kDeviceStateListening ||
        state == kDeviceStateSpeaking) {
        ESP_LOGI(TAG, "Closing audio channel due to network disconnection");
        protocol_->CloseAudioChannel();
    }

    // Update the status bar immediately to show the network state
    auto display = Board::GetInstance().GetDisplay();
    display->UpdateStatusBar(true);
}

void Application::HandleActivationDoneEvent() {
    ESP_LOGI(TAG, "Activation done");

    SystemInfo::PrintHeapStats();
    SetDeviceState(kDeviceStateIdle);

    has_server_time_ = ota_->HasServerTime();

    // Protocol start may have already raised MAIN_EVENT_ERROR. Do not replace
    // that alert with the "ready" UI/sound — the main loop can process both
    // events back-to-back because the activation task is lower priority.
    const bool has_error = !last_error_message_.empty();
    if (!has_error) {
        auto display = Board::GetInstance().GetDisplay();
        std::string message = std::string(Lang::Strings::VERSION) + ota_->GetCurrentVersion();
        display->ShowNotification(message.c_str());
        display->SetChatMessage("system", "");
    }

    // Release OTA object after activation is complete
    ota_.reset();
    auto& board = Board::GetInstance();
    board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);

    if (!has_error) {
        Schedule([this]() {
            // Play the success sound to indicate the device is ready
            audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS);
        });
    }
}

void Application::ActivationTask() {
    // Create OTA object for activation process
    ota_ = std::make_unique<Ota>();

    // Check for new assets version
    CheckAssetsVersion();

    // Check for new firmware version
    CheckNewVersion();

    // Initialize the protocol
    InitializeProtocol();

    // Signal completion to main loop
    xEventGroupSetBits(event_group_, MAIN_EVENT_ACTIVATION_DONE);
}

void Application::CheckAssetsVersion() {
    // Only allow CheckAssetsVersion to be called once
    if (assets_version_checked_) {
        return;
    }
    assets_version_checked_ = true;

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto& assets = Assets::GetInstance();

    if (!assets.partition_valid()) {
        ESP_LOGW(TAG, "Assets partition is disabled for board %s", BOARD_NAME);
        return;
    }

    Settings settings("assets", true);
    // Check if there is a new assets need to be downloaded
    std::string download_url = settings.GetString("download_url");

    if (!download_url.empty()) {
        settings.EraseKey("download_url");

        char message[256];
        snprintf(message, sizeof(message), Lang::Strings::FOUND_NEW_ASSETS, download_url.c_str());
        Alert(Lang::Strings::LOADING_ASSETS, message, "cloud_download", Lang::Sounds::OGG_UPGRADE);

        // Wait for the audio service to be idle for 3 seconds
        vTaskDelay(pdMS_TO_TICKS(3000));
        SetDeviceState(kDeviceStateUpgrading);
        board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
        display->SetChatMessage("system", Lang::Strings::PLEASE_WAIT);

        bool success =
            assets.Download(download_url, [this, display](int progress, size_t speed) -> void {
                char buffer[32];
                snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
                Schedule([display, message = std::string(buffer)]() {
                    display->SetChatMessage("system", message.c_str());
                });
            });

        board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (!success) {
            Alert(Lang::Strings::ERROR, Lang::Strings::DOWNLOAD_ASSETS_FAILED, "cancel",
                  Lang::Sounds::OGG_EXCLAMATION);
            vTaskDelay(pdMS_TO_TICKS(2000));
            SetDeviceState(kDeviceStateActivating);
            return;
        }
    }

    // Apply assets
    assets.Apply();
    display->SetChatMessage("system", "");
    display->SetEmotion("robot_2");
}

void Application::CheckNewVersion() {
    const int MAX_RETRY = 10;
    int retry_count = 0;
    int retry_delay = 10;  // Initial retry delay in seconds

    auto& board = Board::GetInstance();
    while (true) {
        auto display = board.GetDisplay();
        display->SetStatus(Lang::Strings::CHECKING_NEW_VERSION);

        auto check = ota_->CheckVersion();
        if (!check) {
            retry_count++;
            if (retry_count >= MAX_RETRY) {
                ESP_LOGE(TAG, "Too many retries, exit version check");
                return;
            }

            const auto& err = check.error();
            char error_message[160];
            int error_message_length =
                snprintf(error_message, sizeof(error_message), "%s", err.ToString().c_str());
            if (error_message_length < 0 ||
                error_message_length >= static_cast<int>(sizeof(error_message))) {
                snprintf(error_message, sizeof(error_message), "%s", err.Message());
            }

            char buffer[320];
            int alert_message_length =
                snprintf(buffer, sizeof(buffer), Lang::Strings::CHECK_NEW_VERSION_FAILED,
                         retry_delay, error_message);
            if (alert_message_length < 0 ||
                alert_message_length >= static_cast<int>(sizeof(buffer))) {
                snprintf(buffer, sizeof(buffer), "%s", err.Message());
            }
            Alert(Lang::Strings::ERROR, buffer, "cloud_off", Lang::Sounds::OGG_EXCLAMATION);

            ESP_LOGW(TAG, "Check new version failed, retry in %d seconds (%d/%d)", retry_delay,
                     retry_count, MAX_RETRY);
            for (int i = 0; i < retry_delay; i++) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                if (GetDeviceState() == kDeviceStateIdle) {
                    break;
                }
            }
            retry_delay *= 2;  // Double the retry delay
            continue;
        }
        retry_count = 0;
        retry_delay = 10;  // Reset retry delay

        if (ota_->HasNewVersion()) {
            if (UpgradeFirmware(ota_->GetFirmwareUrl(), ota_->GetFirmwareVersion())) {
                return;  // This line will never be reached after reboot
            }
            // If upgrade failed, continue to normal operation
        }

        // No new version, mark the current version as valid
        ota_->MarkCurrentVersionValid();
        if (!ota_->HasActivationCode() && !ota_->HasActivationChallenge()) {
            // Exit the loop if done checking new version
            break;
        }

        display->SetStatus(Lang::Strings::ACTIVATION);
        // Activation code is shown to the user and waiting for the user to input
        if (ota_->HasActivationCode()) {
            ShowActivationCode(ota_->GetActivationCode(), ota_->GetActivationMessage());
        }

        // This will block the loop until the activation is done or timeout
        for (int i = 0; i < 10; ++i) {
            ESP_LOGI(TAG, "Activating... %d/%d", i + 1, 10);
            esp_err_t err = ota_->Activate();
            if (err == ESP_OK) {
                break;
            } else if (err == ESP_ERR_TIMEOUT) {
                vTaskDelay(pdMS_TO_TICKS(3000));
            } else {
                vTaskDelay(pdMS_TO_TICKS(10000));
            }
            if (GetDeviceState() == kDeviceStateIdle) {
                break;
            }
        }
    }
}

void Application::InitializeProtocol() {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto codec = board.GetAudioCodec();

    display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

    if (ota_->HasMqttConfig()) {
        protocol_ = std::make_unique<MqttProtocol>();
    } else if (ota_->HasWebsocketConfig()) {
        protocol_ = std::make_unique<WebsocketProtocol>();
    } else {
        ESP_LOGW(TAG, "No protocol specified in the OTA config, using MQTT");
        protocol_ = std::make_unique<MqttProtocol>();
    }

    protocol_->OnConnected([this]() { DismissAlert(); });

    protocol_->OnNetworkError([this](const std::string& message) {
        last_error_message_ = message;
        xEventGroupSetBits(event_group_, MAIN_EVENT_ERROR);
    });

    protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
        if (GetDeviceState() == kDeviceStateSpeaking) {
            // DP044B1_8_ACK_RUNTIME_COHERENCE
            // No reproducir TTS remoto mientras el aviso siga pendiente.
            // No abortamos al servidor: todavía puede ejecutar MCP como fallback.
            const int64_t now_us = esp_timer_get_time();
            const bool suppress_ack_tts =
                !care_ack_response_target_.empty() &&
                care_ack_response_deadline_us_ > 0 &&
                now_us <= care_ack_response_deadline_us_ &&
                ::xiaozhi_care_ack_pending_for_target(
                    care_ack_response_target_.c_str());
            if (suppress_ack_tts) {
                return;
            }

            audio_service_.PushPacketToDecodeQueue(std::move(packet));
        }
    });

    protocol_->OnAudioChannelOpened([this, codec, &board]() {
        board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
        if (protocol_->server_sample_rate() != codec->output_sample_rate()) {
            ESP_LOGW(TAG,
                     "Server sample rate %d does not match device output sample rate %d, "
                     "resampling may cause distortion",
                     protocol_->server_sample_rate(), codec->output_sample_rate());
        }
    });

    protocol_->OnAudioChannelClosed([this, &board]() {
        // DP042C_RADIO_WIFI_PERFORMANCE
        // La radio sigue usando WiFi aunque el canal MQTT de voz se cierre.
        // Si bajamos a MAX_MODEM durante el streaming aparecen underruns.
        auto& care_radio =
            xiaozhi_care::radio::RadioService::GetInstance();

        if (care_radio.WantsPlaying() &&
            !care_radio.IsUserPaused()) {
            board.SetPowerSaveLevel(
                PowerSaveLevel::PERFORMANCE);
            ESP_LOGI(
                TAG,
                "Radio active: keeping WiFi power save at PERFORMANCE");
        } else {
            board.SetPowerSaveLevel(
                PowerSaveLevel::LOW_POWER);
        }

        Schedule([this]() {
            auto display = Board::GetInstance().GetDisplay();
            display->SetChatMessage("system", "");

            // DP044B1_4_EXTENDED_ACK_WINDOW
            // Si el servidor cerró el canal antes de que la persona pudiera
            // confirmar, reabrimos la escucha sin repetir la pregunta grabada.
            const int64_t now_us = esp_timer_get_time();
            if (!care_ack_response_target_.empty() &&
                care_ack_response_deadline_us_ > 0 &&
                now_us <= care_ack_response_deadline_us_ &&
                care_ack_response_attempts_ < kCareAckMaxResponseAttempts &&
                care_ack_listening_cycles_ < kCareAckMaxListeningCycles &&
                ::xiaozhi_care_ack_pending_for_target(
                    care_ack_response_target_.c_str())) {
                care_ack_listening_requested_ = true;
                care_ack_listening_target_ = care_ack_response_target_;
                care_ack_retry_listening_only_ = true;
                ESP_LOGI(TAG,
                         "DP044B1.4 ACK listen retry queued: target=%s cycle=%u/%u attempts=%u/%u",
                         care_ack_response_target_.c_str(),
                         static_cast<unsigned>(care_ack_listening_cycles_ + 1),
                         static_cast<unsigned>(kCareAckMaxListeningCycles),
                         static_cast<unsigned>(care_ack_response_attempts_),
                         static_cast<unsigned>(kCareAckMaxResponseAttempts));
            }

            SetDeviceState(kDeviceStateIdle);
        });
    });

    protocol_->OnIncomingJson([this, display](const cJSON* root) {
        // Parse JSON data
        auto type = cJSON_GetObjectItem(root, "type");
        if (!cJSON_IsString(type)) {
            ESP_LOGW(TAG, "Incoming JSON message has no type");
            return;
        }
        if (strcmp(type->valuestring, "notify") == 0) {
            auto audio_url = cJSON_GetObjectItem(root, "audio_url");
            if (!cJSON_IsString(audio_url) || audio_url->valuestring[0] == '\0') {
                ESP_LOGW(TAG, "Notify message requires audio_url");
                return;
            }

            std::vector<NotifySubtitle> subtitles;
            auto subtitles_json = cJSON_GetObjectItem(root, "subtitles");
            if (subtitles_json != nullptr && !cJSON_IsArray(subtitles_json)) {
                ESP_LOGW(TAG, "Notify subtitles must be an array");
                return;
            }
            if (cJSON_IsArray(subtitles_json)) {
                cJSON* item = nullptr;
                cJSON_ArrayForEach (item, subtitles_json) {
                    auto start_ms = cJSON_GetObjectItem(item, "start_ms");
                    auto text = cJSON_GetObjectItem(item, "text");
                    if (!cJSON_IsNumber(start_ms) || start_ms->valuedouble < 0 ||
                        start_ms->valuedouble > std::numeric_limits<uint32_t>::max() ||
                        !cJSON_IsString(text)) {
                        ESP_LOGW(TAG, "Ignoring invalid notify subtitle");
                        continue;
                    }
                    subtitles.push_back({.start_ms = static_cast<uint32_t>(start_ms->valuedouble),
                                         .text = text->valuestring});
                }
            }

            Schedule([this, url = std::string(audio_url->valuestring),
                      subtitles = std::move(subtitles)]() mutable {
                StartNotification(std::move(url), std::move(subtitles));
            });
        } else if (strcmp(type->valuestring, "tts") == 0) {
            auto state = cJSON_GetObjectItem(root, "state");
            if (!cJSON_IsString(state)) {
                return;
            }
            if (strcmp(state->valuestring, "start") == 0) {
                Schedule([this]() {
                    aborted_ = false;
                    SetDeviceState(kDeviceStateSpeaking);
                });
            } else if (strcmp(state->valuestring, "stop") == 0) {
                Schedule([this]() {
                    if (GetDeviceState() == kDeviceStateSpeaking) {
                        if (listening_mode_ == kListeningModeManualStop) {
                            SetDeviceState(kDeviceStateIdle);
                        } else {
                            SetDeviceState(kDeviceStateListening);
                        }
                    }
                });
            } else if (strcmp(state->valuestring, "sentence_start") == 0) {
                auto text = cJSON_GetObjectItem(root, "text");
                if (cJSON_IsString(text)) {
                    // DP044B1_8_ACK_ASSISTANT_GUARD
                    // Una frase remota no puede presentarse como confirmación
                    // mientras XiaoZhi Care todavía considere pendiente el ACK.
                    const int64_t now_us = esp_timer_get_time();
                    const bool suppress_ack_sentence =
                        !care_ack_response_target_.empty() &&
                        care_ack_response_deadline_us_ > 0 &&
                        now_us <= care_ack_response_deadline_us_ &&
                        ::xiaozhi_care_ack_pending_for_target(
                            care_ack_response_target_.c_str());
                    if (suppress_ack_sentence) {
                        ESP_LOGI(
                            TAG,
                            "DP044B1.8 assistant sentence suppressed while ACK pending: target=%s text=%s",
                            care_ack_response_target_.c_str(),
                            text->valuestring);
                        return;
                    }

                    std::vector<TextGlyph> glyphs;
                    uint8_t bpp = 0;
                    if (!TextGlyphPayload::Parse(root, glyphs, bpp)) {
                        glyphs.clear();
                    }
                    ESP_LOGI(TAG, "<< %s", text->valuestring);
                    Schedule([display, message = std::string(text->valuestring),
                              glyphs = std::move(glyphs), bpp]() {
                        display->AddTextGlyphs(glyphs, bpp);
                        display->SetChatMessage("assistant", message.c_str());
                    });
                }
            }
        } else if (strcmp(type->valuestring, "stt") == 0) {
            auto text = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(text)) {
                std::vector<TextGlyph> glyphs;
                uint8_t bpp = 0;
                if (!TextGlyphPayload::Parse(root, glyphs, bpp)) {
                    glyphs.clear();
                }
                ESP_LOGI(TAG, ">> %s", text->valuestring);

                // DP044B1_4_EXTENDED_ACK_WINDOW
                // La pregunta se reproduce localmente. Conservamos el contexto
                // durante 30 s y aceptamos hasta tres respuestas STT. Un error
                // de reconocimiento ya no consume toda la oportunidad de ACK.
                Schedule([this, message = std::string(text->valuestring)]() {
                    if (care_ack_response_target_.empty()) return;

                    const int64_t now_us = esp_timer_get_time();
                    const bool in_window = care_ack_response_deadline_us_ > 0 &&
                                           now_us <= care_ack_response_deadline_us_;
                    const std::string target = care_ack_response_target_;

                    if (!in_window) {
                        ESP_LOGI(TAG,
                                 "DP044B1.4 ACK response ignored after window: target=%s text=%s",
                                 target.c_str(),
                                 message.c_str());
                        ClearCareAckResponseWindow();
                        return;
                    }

                    ++care_ack_response_attempts_;

                    if (IsCareAckAffirmativeText(message)) {
                        ClearCareAckResponseWindow();
                        const bool acknowledged =
                            ::xiaozhi_care_acknowledge_target(target.c_str());
                        ESP_LOGI(TAG,
                                 "DP044B1 local STT acknowledgement: target=%s acknowledged=%d text=%s",
                                 target.c_str(),
                                 acknowledged ? 1 : 0,
                                 message.c_str());

                        // DP044B2_CONFIG_ACK_AND_VOICE_LED
                        // Solo mostrar blanco si el runtime realmente aceptó el ACK.
                        if (acknowledged) {
                            bool visual_ok = false;
                            auto* led = Board::GetInstance().GetLed();
                            if (led != nullptr) {
                                visual_ok = led->ShowCareAckConfirmation();
                            }
                            ESP_LOGI(TAG,
                                     "DP044B2 voice ACK visual: target=%s visual=%d",
                                     target.c_str(),
                                     visual_ok ? 1 : 0);
                        }
                        return;
                    }

                    if (care_ack_response_attempts_ >= kCareAckMaxResponseAttempts) {
                        ESP_LOGI(TAG,
                                 "DP044B1.4 ACK responses exhausted: target=%s attempts=%u/%u text=%s",
                                 target.c_str(),
                                 static_cast<unsigned>(care_ack_response_attempts_),
                                 static_cast<unsigned>(kCareAckMaxResponseAttempts),
                                 message.c_str());
                        ClearCareAckResponseWindow();
                        return;
                    }

                    const int64_t remaining_ms =
                        (care_ack_response_deadline_us_ - now_us) / 1000LL;
                    ESP_LOGI(TAG,
                             "DP044B1 local STT did not acknowledge: target=%s attempt=%u/%u remaining_ms=%lld text=%s",
                             target.c_str(),
                             static_cast<unsigned>(care_ack_response_attempts_),
                             static_cast<unsigned>(kCareAckMaxResponseAttempts),
                             static_cast<long long>(remaining_ms),
                             message.c_str());
                });

                Schedule([display, message = std::string(text->valuestring),
                          glyphs = std::move(glyphs), bpp]() {
                    display->AddTextGlyphs(glyphs, bpp);
                    display->SetChatMessage("user", message.c_str());
                });
            }
        } else if (strcmp(type->valuestring, "llm") == 0) {
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(emotion)) {
                Schedule([display, emotion_str = std::string(emotion->valuestring)]() {
                    display->SetEmotion(emotion_str.c_str());
                });
            }
        } else if (strcmp(type->valuestring, "mcp") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            if (cJSON_IsObject(payload)) {
                McpServer::GetInstance().ParseMessage(payload);
            }
        } else if (strcmp(type->valuestring, "system") == 0) {
            auto command = cJSON_GetObjectItem(root, "command");
            if (cJSON_IsString(command)) {
                ESP_LOGI(TAG, "System command: %s", command->valuestring);
                if (strcmp(command->valuestring, "reboot") == 0) {
                    // Do a reboot if user requests a OTA update
                    Schedule([this]() { Reboot(); });
                } else {
                    ESP_LOGW(TAG, "Unknown system command: %s", command->valuestring);
                }
            }
        } else if (strcmp(type->valuestring, "alert") == 0) {
            auto status = cJSON_GetObjectItem(root, "status");
            auto message = cJSON_GetObjectItem(root, "message");
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(status) && cJSON_IsString(message) && cJSON_IsString(emotion)) {
                Alert(status->valuestring, message->valuestring, emotion->valuestring,
                      Lang::Sounds::OGG_VIBRATION);
            } else {
                ESP_LOGW(TAG, "Alert command requires status, message and emotion");
            }
#if CONFIG_RECEIVE_CUSTOM_MESSAGE
        } else if (strcmp(type->valuestring, "custom") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            ESP_LOGI(TAG, "Received custom message: %s", cJSON_PrintUnformatted(root));
            if (cJSON_IsObject(payload)) {
                Schedule(
                    [this, display, payload_str = std::string(cJSON_PrintUnformatted(payload))]() {
                        display->SetChatMessage("system", payload_str.c_str());
                    });
            } else {
                ESP_LOGW(TAG, "Invalid custom message format: missing payload");
            }
#endif
        } else {
            ESP_LOGW(TAG, "Unknown message type: %s", type->valuestring);
        }
    });

    protocol_->Start();
}

void Application::ShowActivationCode(const std::string& code, const std::string& message) {
    struct digit_sound {
        char digit;
        const std::string_view& sound;
    };
    static const std::array<digit_sound, 10> digit_sounds{
        {digit_sound{'0', Lang::Sounds::OGG_0}, digit_sound{'1', Lang::Sounds::OGG_1},
         digit_sound{'2', Lang::Sounds::OGG_2}, digit_sound{'3', Lang::Sounds::OGG_3},
         digit_sound{'4', Lang::Sounds::OGG_4}, digit_sound{'5', Lang::Sounds::OGG_5},
         digit_sound{'6', Lang::Sounds::OGG_6}, digit_sound{'7', Lang::Sounds::OGG_7},
         digit_sound{'8', Lang::Sounds::OGG_8}, digit_sound{'9', Lang::Sounds::OGG_9}}};

    // This sentence uses 9KB of SRAM, so we need to wait for it to finish
    Alert(Lang::Strings::ACTIVATION, message.c_str(), "link", Lang::Sounds::OGG_ACTIVATION);

    for (const auto& digit : code) {
        auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
                               [digit](const digit_sound& ds) { return ds.digit == digit; });
        if (it != digit_sounds.end()) {
            audio_service_.PlaySound(it->sound);
        }
    }
}

void Application::Alert(const char* status, const char* message, const char* emotion,
                        const std::string_view& sound) {
    ESP_LOGW(TAG, "Alert [%s] %s: %s", emotion, status, message);
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(status);
    display->SetEmotion(emotion);
    display->SetChatMessage("system", message);
    if (!sound.empty()) {
        audio_service_.PlaySound(sound);
    }
}

void Application::DismissAlert() {
    last_error_message_.clear();
    if (GetDeviceState() == kDeviceStateIdle) {
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        display->SetChatMessage("system", "");
    }
}

void Application::ToggleChatState() { xEventGroupSetBits(event_group_, MAIN_EVENT_TOGGLE_CHAT); }

void Application::StartListening() { xEventGroupSetBits(event_group_, MAIN_EVENT_START_LISTENING); }

void Application::StopListening() { xEventGroupSetBits(event_group_, MAIN_EVENT_STOP_LISTENING); }


// DP044B3_PHYSICAL_ACK
bool Application::IsCareAlertPendingForPhysicalAck() {
    std::string target;
    {
        std::lock_guard<std::mutex> lock(care_ack_physical_mutex_);
        target = care_ack_physical_target_;
    }

    if (target.empty()) {
        return false;
    }

    return ::xiaozhi_care_ack_pending_for_target(target.c_str());
}

void Application::ConfirmPendingCareAlertFromTouch() {
    Schedule([this]() {
        std::string target;

        if (!care_ack_response_target_.empty()) {
            target = care_ack_response_target_;
        } else if (!care_ack_listening_target_.empty()) {
            target = care_ack_listening_target_;
        } else if (!care_ack_prompt_delay_target_.empty()) {
            target = care_ack_prompt_delay_target_;
        } else {
            std::lock_guard<std::mutex> lock(care_ack_physical_mutex_);
            target = care_ack_physical_target_;
        }

        if (target.empty()) {
            ESP_LOGI(TAG, "DP044B3 touch ACK ignored: no Care target available");
            return;
        }

        if (!::xiaozhi_care_ack_pending_for_target(target.c_str())) {
            {
                std::lock_guard<std::mutex> lock(care_ack_physical_mutex_);
                if (care_ack_physical_target_ == target) {
                    care_ack_physical_target_.clear();
                }
            }
            ESP_LOGI(TAG,
                     "DP044B3 touch ACK ignored: target=%s no longer pending",
                     target.c_str());
            return;
        }

        const bool acknowledged =
            ::xiaozhi_care_acknowledge_target(target.c_str());

        if (!acknowledged) {
            ESP_LOGW(TAG, "DP044B3 touch ACK failed: target=%s", target.c_str());
            return;
        }

        if (care_ack_listening_target_ == target) {
            care_ack_listening_requested_ = false;
            care_ack_listening_target_.clear();
        }

        {
            std::lock_guard<std::mutex> lock(care_ack_physical_mutex_);
            if (care_ack_physical_target_ == target) {
                care_ack_physical_target_.clear();
            }
        }

        care_ack_prompt_active_ = false;
        care_ack_prompt_audio_cache_.clear();
        CancelCareAckPromptDelay();
        ClearCareAckResponseWindow();

        const DeviceState state = GetDeviceState();

        if (state == kDeviceStateSpeaking) {
            AbortSpeaking(kAbortReasonNone);
        }

        // DP044B2_1_TOUCH_TEXT_TTS_CONFIRMATION
        // El ACK ya quedó registrado localmente. Para que la experiencia sea
        // igual a confirmar por voz, mantenemos o abrimos el canal y enviamos
        // una entrada textual al agente. No dependemos de esta respuesta para
        // considerar confirmado el recordatorio.
        bool agent_reply_requested = false;

        if (protocol_ != nullptr) {
            if (state == kDeviceStateListening &&
                protocol_->IsAudioChannelOpened()) {
                protocol_->SendStopListening();
            }

            if (state == kDeviceStateListening ||
                state == kDeviceStateSpeaking ||
                state == kDeviceStateConnecting) {
                SetDeviceState(kDeviceStateIdle);
            }

            if (!protocol_->IsAudioChannelOpened()) {
                if (SetDeviceState(kDeviceStateConnecting) &&
                    protocol_->OpenAudioChannel()) {
                    SetDeviceState(kDeviceStateIdle);
                } else {
                    if (GetDeviceState() == kDeviceStateConnecting) {
                        SetDeviceState(kDeviceStateIdle);
                    }
                }
            }

            if (protocol_->IsAudioChannelOpened()) {
                // Hace que tts/stop vuelva a idle en lugar de abrir otra ronda
                // automática de escucha.
                listening_mode_ = kListeningModeManualStop;

                // DP044B2_1_R3_SHORT_TOUCH_CONFIRMATION
                // Debe ser una frase breve: el servidor limita listen/detect
                // a entradas cortas. Replicamos la confirmación verbal real.
                agent_reply_requested = protocol_->SendTextInput(
                    "Sí, ya lo escuché.");

                ESP_LOGI(TAG,
                         "DP044B2.1 touch ACK text sent to XiaoZhi: target=%s sent=%d",
                         target.c_str(),
                         agent_reply_requested ? 1 : 0);
            } else {
                ESP_LOGW(TAG,
                         "DP044B2.1 touch ACK confirmed locally but XiaoZhi channel unavailable: target=%s",
                         target.c_str());
            }
        }

        bool visual_ok = false;
        auto* led = Board::GetInstance().GetLed();
        if (led != nullptr) {
            visual_ok = led->ShowCareAckConfirmation();
        }

        if (!agent_reply_requested &&
            GetDeviceState() == kDeviceStateIdle &&
            !care_reminder_audio_reserved_.load()) {
            xiaozhi_care::radio::RadioService::GetInstance().SetSystemPaused(false);
        }

        ESP_LOGI(TAG,
                 "DP044B3 touch ACK confirmed: target=%s acknowledged=1 visual=%d agent_reply=%d",
                 target.c_str(),
                 visual_ok ? 1 : 0,
                 agent_reply_requested ? 1 : 0);
    });
}

void Application::HandleToggleChatEvent() {
    auto state = GetDeviceState();

    if (state == kDeviceStateNotifying) {
        StopNotification();
        state = kDeviceStateIdle;
    }

    if (state == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (state == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    } else if (state == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (state == kDeviceStateIdle) {
        ListeningMode mode = GetDefaultListeningMode();
        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            // Schedule to let the state change be processed first (UI update)
            Schedule([this, mode]() { ContinueOpenAudioChannel(mode); });
            return;
        }
        SetListeningMode(mode);
    } else if (state == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonNone);
    } else if (state == kDeviceStateListening) {
        protocol_->CloseAudioChannel();
    }
}

void Application::ContinueOpenAudioChannel(ListeningMode mode) {
    // Check state again in case it was changed during scheduling
    if (GetDeviceState() != kDeviceStateConnecting) {
        return;
    }

    // Switch to performance mode before connecting to reduce latency
    auto& board = Board::GetInstance();
    board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);

    if (!protocol_->IsAudioChannelOpened()) {
        if (!protocol_->OpenAudioChannel()) {
            // Return to idle so the device is not stuck in the connecting
            // state (not every failure path reports a network error)
            SetDeviceState(kDeviceStateIdle);
            return;
        }
    }

    SetListeningMode(mode);
}

void Application::HandleStartListeningEvent() {
    auto state = GetDeviceState();

    if (state == kDeviceStateNotifying) {
        StopNotification();
        state = kDeviceStateIdle;
    }

    if (state == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (state == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (state == kDeviceStateIdle) {
        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            // Schedule to let the state change be processed first (UI update)
            Schedule([this]() { ContinueOpenAudioChannel(kListeningModeManualStop); });
            return;
        }
        SetListeningMode(kListeningModeManualStop);
    } else if (state == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonNone);
        SetListeningMode(kListeningModeManualStop);
    }
}

void Application::HandleStopListeningEvent() {
    auto state = GetDeviceState();

    if (state == kDeviceStateNotifying) {
        StopNotification();
    } else if (state == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    } else if (state == kDeviceStateListening) {
        if (protocol_) {
            protocol_->SendStopListening();
        }
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::HandleWakeWordDetectedEvent() {
    if (!protocol_) {
        return;
    }

    auto state = GetDeviceState();
    auto wake_word = audio_service_.GetLastWakeWord();
    ESP_LOGI(TAG, "Wake word detected: %s (state: %d)", wake_word.c_str(), (int)state);

    if (state == kDeviceStateIdle) {
        BeginWakeWordInvoke(wake_word);
    } else if (state == kDeviceStateNotifying) {
        StopNotification();
        BeginWakeWordInvoke(wake_word);
    } else if (state == kDeviceStateSpeaking || state == kDeviceStateListening) {
        AbortSpeaking(kAbortReasonWakeWordDetected);
        // Clear send queue to avoid sending residues to server
        while (audio_service_.PopPacketFromSendQueue())
            ;

        if (state == kDeviceStateListening) {
            protocol_->SendStartListening(GetDefaultListeningMode());
            audio_service_.ResetDecoder();
            audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
            // Re-enable wake word detection as it was stopped by the detection itself
            audio_service_.EnableWakeWordDetection(true);
        } else {
            // Play popup sound and start listening again
            play_popup_on_listening_ = true;
            SetListeningMode(GetDefaultListeningMode());
        }
    } else if (state == kDeviceStateActivating) {
        // Restart the activation check if the wake word is detected during activation
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::BeginWakeWordInvoke(const std::string& wake_word) {
    // Must run in the main task with the device in idle state
    audio_service_.EncodeWakeWord();

    // Always pass through the connecting state, even if the audio channel is
    // already opened. ContinueWakeWordInvoke() rejects any other state, so
    // skipping this transition would silently drop the wake word invocation.
    if (!SetDeviceState(kDeviceStateConnecting)) {
        // Wake word detection was stopped by the detection itself; restore it
        // so the device does not become unresponsive to wake words.
        audio_service_.EnableWakeWordDetection(true);
        return;
    }

    if (!protocol_->IsAudioChannelOpened()) {
        // Schedule to let the state change be processed first (UI update),
        // then continue with OpenAudioChannel which may block for ~1 second
        Schedule([this, wake_word]() { ContinueWakeWordInvoke(wake_word); });
        return;
    }
    // Channel already opened, continue directly
    ContinueWakeWordInvoke(wake_word);
}

void Application::ContinueWakeWordInvoke(const std::string& wake_word) {
    // Check state again in case it was changed during scheduling
    if (GetDeviceState() != kDeviceStateConnecting) {
        return;
    }

    // Switch to performance mode before connecting to reduce latency
    auto& board = Board::GetInstance();
    board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);

    if (!protocol_->IsAudioChannelOpened()) {
        if (!protocol_->OpenAudioChannel()) {
            // Return to idle so the device is not stuck in the connecting
            // state (not every failure path reports a network error), and
            // wake word detection is re-enabled by the idle state handler.
            SetDeviceState(kDeviceStateIdle);
            return;
        }
    }

    ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_SEND_WAKE_WORD_DATA
    // Encode and send the wake word data to the server
    while (auto packet = audio_service_.PopWakeWordPacket()) {
        protocol_->SendAudio(std::move(packet));
    }
    // Set the chat state to wake word detected
    protocol_->SendWakeWordDetected(wake_word);
    SetListeningMode(GetDefaultListeningMode());
#else
    // Set flag to play popup sound after state changes to listening
    // (PlaySound here would be cleared by ResetDecoder in EnableVoiceProcessing)
    play_popup_on_listening_ = true;
    SetListeningMode(GetDefaultListeningMode());
#endif
}

void Application::HandleStateChangedEvent() {
    DeviceState new_state = state_machine_.GetState();
    clock_ticks_ = 0;

    // DP044B1_6_ACK_RUNTIME_SYNC
    // El ACK puede llegar por MCP aunque el matcher STT local no lo haya
    // aceptado. Si el runtime ya dejó de tener el aviso pendiente, cerramos
    // también la ventana local y liberamos el estado de escucha asociado.
    if (!care_ack_response_target_.empty() &&
        !::xiaozhi_care_ack_pending_for_target(
            care_ack_response_target_.c_str())) {
        const std::string acknowledged_target = care_ack_response_target_;
        if (care_ack_listening_target_ == acknowledged_target) {
            care_ack_listening_requested_ = false;
            care_ack_listening_target_.clear();
        }
        care_ack_prompt_active_ = false;
        care_ack_prompt_audio_cache_.clear();
        CancelCareAckPromptDelay();
        ClearCareAckResponseWindow();
        ESP_LOGI(TAG,
                 "DP044B1.6 local ACK window closed: target=%s already acknowledged by runtime",
                 acknowledged_target.c_str());
    }

    // DP042A_RADIO_MP3_BASE + DP044A_REMINDER_AUDIO_PRIORITY
    // La radio sólo suena en idle, salvo que XiaoZhi Care haya reservado el
    // canal de audio para un recordatorio local. Así evitamos un pequeño
    // "blip" de radio al volver a Idle mientras el recordatorio está pendiente.
    const bool care_holds_audio =
        care_reminder_audio_reserved_.load() ||
        (care_ack_listening_requested_ && !care_ack_prompt_delay_pending_) ||
        !care_ack_response_target_.empty();
    xiaozhi_care::radio::RadioService::GetInstance()
        .SetSystemPaused(new_state != kDeviceStateIdle || care_holds_audio);
    // Any state change invalidates a pending deferred listening start;
    // the Listening case below re-arms it when needed.
    pending_listening_start_ = false;

    // DP-039: a silence timer belongs only to the current listening turn.
    if (new_state != kDeviceStateListening) {
        CancelSlowSpeechTimer();
        slow_speech_heard_voice_ = false;
        slow_speech_hard_deadline_us_ = 0;
    }

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto led = board.GetLed();
    led->OnStateChanged();

    switch (new_state) {
        case kDeviceStateUnknown:
        case kDeviceStateIdle:
            // Keep a just-raised network error visible. SetDeviceState(idle)
            // queues STATE_CHANGED after Alert(), and the idle handler would
            // otherwise wipe the status, emotion, and chat message.
            if (last_error_message_.empty()) {
                display->SetStatus(Lang::Strings::STANDBY);
                display->ClearChatMessages();  // Clear messages first
                display->SetEmotion(
                    "neutral");  // Then set emotion (wechat mode checks child count)
            }
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(true);

            // DP044A_REMINDER_AUDIO_PRIORITY
            // Si una solicitud fue aceptada y durante el Schedule() el equipo
            // quedó ocupado, no la perdemos: se presenta al volver a Idle.
            MaybeStartCareReminderAudio();
            // DP044B_UNIVERSAL_ACK
            MaybeStartCareAcknowledgementListening();
            break;
        case kDeviceStateConnecting:
            display->SetStatus(Lang::Strings::CONNECTING);
            display->SetEmotion("neutral");
            display->SetChatMessage("system", "");
            break;
        case kDeviceStateListening:
            display->SetStatus(Lang::Strings::LISTENING);
            display->SetEmotion("neutral");

            // DP044B1_2_NATIVE_ACK_CUE
            // Mientras esperamos la respuesta inmediata al recordatorio,
            // mantenemos la pregunta visible sin introducir una voz distinta.
            if (!care_ack_response_target_.empty() &&
                esp_timer_get_time() <= care_ack_response_deadline_us_) {
                display->SetChatMessage("system", "¿Escuchaste el recordatorio?");
            }

            // Make sure the audio processor is running
            if (play_popup_on_listening_ || !audio_service_.IsAudioProcessorRunning()) {
                // For auto mode, wait for the playback queue to drain before enabling
                // voice processing. This prevents audio truncation when STOP arrives
                // late due to network jitter. Instead of blocking the main loop here,
                // defer the start until MAIN_EVENT_PLAYBACK_DRAINED arrives.
                if ((listening_mode_ == kListeningModeAutoStop ||
                     listening_mode_ == kListeningModeCareSlow) &&
                    !audio_service_.IsPlaybackIdle()) {
                    pending_listening_start_ = true;
                } else {
                    StartListeningAudio();
                }
            } else {
                ConfigureWakeWordForListening();
            }
            break;
        case kDeviceStateSpeaking:
            display->SetStatus(Lang::Strings::SPEAKING);

            if (listening_mode_ != kListeningModeRealtime) {
                audio_service_.EnableVoiceProcessing(false);
                // Only AFE wake word can be detected in speaking mode
                audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
            }
            audio_service_.ResetDecoder();
            break;
        case kDeviceStateNotifying:
            display->SetStatus(Lang::Strings::SPEAKING);
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
            break;
        case kDeviceStateWifiConfiguring:
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(false);
            break;
        default:
            // Do nothing
            break;
    }
}

void Application::StartListeningAudio() {
    // Runs in the main loop, either directly from HandleStateChangedEvent or
    // deferred via MAIN_EVENT_PLAYBACK_DRAINED once the playback queue drains.
    if (GetDeviceState() != kDeviceStateListening) {
        return;
    }

    // Send the start listening command
    protocol_->SendStartListening(listening_mode_);
    audio_service_.EnableVoiceProcessing(true);

    // DP-039 Fase 2: el perfil controla el silencio DESPUES de la ultima actividad PCM.
    if (listening_mode_ == kListeningModeCareSlow) {
        slow_speech_heard_voice_ = false;
        slow_speech_hard_deadline_us_ = 0;
        ArmSlowSpeechTimer(kCareSlowSpeechInitialWaitUs);
    } else {
        CancelSlowSpeechTimer();
        slow_speech_heard_voice_ = false;
        slow_speech_hard_deadline_us_ = 0;
    }

    ConfigureWakeWordForListening();

    // Play popup sound after ResetDecoder (in EnableVoiceProcessing) has been called
    if (play_popup_on_listening_) {
        play_popup_on_listening_ = false;
        audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
    }
}

void Application::ConfigureWakeWordForListening() {
#ifdef CONFIG_WAKE_WORD_DETECTION_IN_LISTENING
    // Enable wake word detection in listening mode (configured via Kconfig)
    audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
#else
    // Disable wake word detection in listening mode
    audio_service_.EnableWakeWordDetection(false);
#endif
}

void Application::StartNotification(std::string audio_url, std::vector<NotifySubtitle> subtitles) {
    if (GetDeviceState() != kDeviceStateIdle || notify_player_.IsBusy()) {
        ESP_LOGW(TAG, "Ignoring notify message while device is busy");
        return;
    }

    auto& board = Board::GetInstance();
    board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
    audio_service_.EnableVoiceProcessing(false);
    audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
    audio_service_.ReleaseWakeWordResources();
    while (audio_service_.PopPacketFromSendQueue()) {
        // Discard microphone audio left over from a previous conversation.
    }

    if (!SetDeviceState(kDeviceStateNotifying)) {
        board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        return;
    }

    audio_service_.ResetDecoder();
    uint32_t playback_id = ++notification_playback_id_;
    if (playback_id == 0) {
        playback_id = ++notification_playback_id_;
    }
    audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);

    bool started = notify_player_.Start(
        std::move(audio_url), std::move(subtitles), playback_id,
        [this](uint32_t id, const std::string& text) {
            Schedule([this, id, text]() {
                if (GetDeviceState() == kDeviceStateNotifying && notification_playback_id_ == id) {
                    Board::GetInstance().GetDisplay()->SetChatMessage("assistant", text.c_str());
                }
            });
        },
        [this](uint32_t id, bool success) {
            Schedule([this, id, success]() { HandleNotificationFinished(id, success); });
        });

    if (!started) {
        ESP_LOGE(TAG, "Failed to start notification playback");
        StopNotification();
    }
}

void Application::StopNotification() {
    notify_player_.Stop();
    audio_service_.ResetDecoder();
    auto& board = Board::GetInstance();
    board.GetDisplay()->SetChatMessage("assistant", "");

    // DP042C_RADIO_WIFI_PERFORMANCE
    // Una notificación no debe degradar la conexión de una radio activa.
    auto& care_radio =
        xiaozhi_care::radio::RadioService::GetInstance();

    if (care_radio.WantsPlaying() &&
        !care_radio.IsUserPaused()) {
        board.SetPowerSaveLevel(
            PowerSaveLevel::PERFORMANCE);
    } else {
        board.SetPowerSaveLevel(
            PowerSaveLevel::LOW_POWER);
    }

    if (GetDeviceState() == kDeviceStateNotifying) {
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::HandleNotificationFinished(uint32_t playback_id, bool success) {
    if (GetDeviceState() != kDeviceStateNotifying || notification_playback_id_ != playback_id) {
        return;
    }
    ESP_LOGI(TAG, "Notification playback %lu %s", static_cast<unsigned long>(playback_id),
             success ? "completed" : "failed");
    StopNotification();
}

void Application::Schedule(std::function<void()>&& callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, MAIN_EVENT_SCHEDULE);
}

void Application::AbortSpeaking(AbortReason reason) {
    ESP_LOGI(TAG, "Abort speaking");
    aborted_ = true;
    if (protocol_) {
        protocol_->SendAbortSpeaking(reason);
    }
}

// -----------------------------------------------------------------------------
// DP-039 - CARE slow / paused speech endpointing
// -----------------------------------------------------------------------------

void Application::ArmSlowSpeechTimer(uint64_t timeout_us) {
    if (slow_speech_timer_handle_ == nullptr || timeout_us == 0) {
        return;
    }

    if (esp_timer_is_active(slow_speech_timer_handle_)) {
        esp_timer_stop(slow_speech_timer_handle_);
    }

    const esp_err_t err =
        esp_timer_start_once(slow_speech_timer_handle_, timeout_us);

    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "DP-039 failed to arm slow speech timer: err=%d",
                 static_cast<int>(err));
    }
}

void Application::CancelSlowSpeechTimer() {
    if (slow_speech_timer_handle_ != nullptr &&
        esp_timer_is_active(slow_speech_timer_handle_)) {
        esp_timer_stop(slow_speech_timer_handle_);
    }
}

void Application::HandleSlowSpeechInputActivity() {
    if (GetDeviceState() != kDeviceStateListening ||
        listening_mode_ != kListeningModeCareSlow) {
        return;
    }

    const bool active = audio_service_.IsInputActivityDetected();
    const int64_t now_us = esp_timer_get_time();

    if (active) {
        const bool first_activity = !slow_speech_heard_voice_;
        slow_speech_heard_voice_ = true;

        if (first_activity || slow_speech_hard_deadline_us_ <= 0) {
            slow_speech_hard_deadline_us_ =
                now_us + static_cast<int64_t>(kCareSlowSpeechHardLimitUs);
        }

        int64_t remaining_us = slow_speech_hard_deadline_us_ - now_us;
        if (remaining_us < 1000) remaining_us = 1000;
        ArmSlowSpeechTimer(static_cast<uint64_t>(remaining_us));
        return;
    }

    if (!slow_speech_heard_voice_) {
        return;
    }

    auto& settings = xiaozhi_care::listening::ListeningSettings::GetInstance();
    settings.Init();
    uint64_t endpoint_us = static_cast<uint64_t>(settings.GetSilenceMs()) * 1000ULL;

    const int64_t remaining_hard_us = slow_speech_hard_deadline_us_ - now_us;
    if (remaining_hard_us > 0 &&
        remaining_hard_us < static_cast<int64_t>(endpoint_us)) {
        endpoint_us = static_cast<uint64_t>(remaining_hard_us);
    }

    ArmSlowSpeechTimer(endpoint_us);
}

void Application::HandleSlowSpeechTimeout() {
    if (GetDeviceState() != kDeviceStateListening ||
        listening_mode_ != kListeningModeCareSlow) {
        return;
    }

    if (!protocol_) {
        ESP_LOGW(TAG, "DP-039 timeout ignored because protocol is unavailable");
        return;
    }

    const bool activity_active = audio_service_.IsInputActivityDetected();
    const int64_t now_us = esp_timer_get_time();

    if (slow_speech_heard_voice_ && activity_active) {
        if (slow_speech_hard_deadline_us_ > now_us) {
            const uint64_t remaining_us =
                static_cast<uint64_t>(slow_speech_hard_deadline_us_ - now_us);
            ArmSlowSpeechTimer(remaining_us);
            return;
        }

        ESP_LOGW(TAG,
                 "DP-039 absolute safety limit reached after %llu ms; forcing end of turn",
                 static_cast<unsigned long long>(kCareSlowSpeechHardLimitUs / 1000ULL));
    }

    protocol_->SendStopListening();
    slow_speech_heard_voice_ = false;
    slow_speech_hard_deadline_us_ = 0;
    SetDeviceState(kDeviceStateIdle);
}

void Application::SetListeningMode(ListeningMode mode) {
    listening_mode_ = mode;
    SetDeviceState(kDeviceStateListening);
}

ListeningMode Application::GetDefaultListeningMode() const {
    // DP-039: without AEC, CARE uses server manual mode plus local VAD
    // endpointing. This tolerates natural pauses much better than server auto.
    return aec_mode_ == kAecOff ? kListeningModeCareSlow : kListeningModeRealtime;
}

void Application::Reboot() {
    ESP_LOGI(TAG, "Rebooting...");
    if (GetDeviceState() == kDeviceStateNotifying) {
        StopNotification();
    }
    // Disconnect the audio channel
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        protocol_->CloseAudioChannel();
    }
    protocol_.reset();
    audio_service_.Stop();

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

bool Application::UpgradeFirmware(const std::string& url, const std::string& version) {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();

    std::string upgrade_url = url;
    std::string version_info = version.empty() ? "(Manual upgrade)" : version;

    if (GetDeviceState() == kDeviceStateNotifying) {
        StopNotification();
    }

    // Close audio channel if it's open
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        ESP_LOGI(TAG, "Closing audio channel before firmware upgrade");
        protocol_->CloseAudioChannel();
    }
    ESP_LOGI(TAG, "Starting firmware upgrade from URL: %s", upgrade_url.c_str());

    Alert(Lang::Strings::OTA_UPGRADE, Lang::Strings::UPGRADING, "download",
          Lang::Sounds::OGG_UPGRADE);
    vTaskDelay(pdMS_TO_TICKS(3000));

    SetDeviceState(kDeviceStateUpgrading);

    std::string message = std::string(Lang::Strings::NEW_VERSION) + version_info;
    display->SetChatMessage("system", message.c_str());

    board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
    audio_service_.Stop();
    vTaskDelay(pdMS_TO_TICKS(1000));

    bool upgrade_success = Ota::Upgrade(upgrade_url, [this, display](int progress, size_t speed) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
        Schedule([display, message = std::string(buffer)]() {
            display->SetChatMessage("system", message.c_str());
        });
    });

    if (!upgrade_success) {
        // Upgrade failed, restart audio service and continue running
        ESP_LOGE(TAG,
                 "Firmware upgrade failed, restarting audio service and continuing operation...");
        audio_service_.Start();                              // Restart audio service
        board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);  // Restore power save level
        Alert(Lang::Strings::ERROR, Lang::Strings::UPGRADE_FAILED, "cancel",
              Lang::Sounds::OGG_EXCLAMATION);
        vTaskDelay(pdMS_TO_TICKS(3000));
        return false;
    } else {
        // Upgrade success, reboot immediately
        ESP_LOGI(TAG, "Firmware upgrade successful, rebooting...");
        display->SetChatMessage("system", "Upgrade successful, rebooting...");
        vTaskDelay(pdMS_TO_TICKS(1000));  // Brief pause to show message
        Reboot();
        return true;
    }
}

void Application::WakeWordInvoke(const std::string& wake_word) {
    if (!protocol_) {
        return;
    }

    auto state = GetDeviceState();

    if (state == kDeviceStateIdle) {
        // May be called from outside the main task (e.g. board button
        // callbacks), so schedule the invocation instead of running it here
        Schedule([this, wake_word]() {
            if (GetDeviceState() == kDeviceStateIdle) {
                BeginWakeWordInvoke(wake_word);
            }
        });
    } else if (state == kDeviceStateNotifying) {
        Schedule([this, wake_word]() {
            if (GetDeviceState() == kDeviceStateNotifying) {
                StopNotification();
                BeginWakeWordInvoke(wake_word);
            }
        });
    } else if (state == kDeviceStateSpeaking) {
        Schedule([this]() { AbortSpeaking(kAbortReasonNone); });
    } else if (state == kDeviceStateListening) {
        Schedule([this]() {
            if (protocol_) {
                protocol_->CloseAudioChannel();
            }
        });
    }
}

bool Application::CanEnterSleepMode() {
    if (GetDeviceState() != kDeviceStateIdle) {
        return false;
    }

    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        return false;
    }

    if (!audio_service_.IsIdle()) {
        return false;
    }

    // Now it is safe to enter sleep mode
    return true;
}

void Application::RegisterMcpBroadcastCallback(std::function<void(const std::string&)> callback) {
    mcp_broadcast_callback_ = std::move(callback);
}

void Application::SendMcpMessage(const std::string& payload) {
    // Always schedule to run in main task for thread safety
    Schedule([this, payload]() {
        if (protocol_) {
            protocol_->SendMcpMessage(payload);
        }
        if (mcp_broadcast_callback_) {
            mcp_broadcast_callback_(payload);
        }
    });
}

void Application::SetAecMode(AecMode mode) {
    aec_mode_ = mode;
    Schedule([this]() {
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        switch (aec_mode_) {
            case kAecOff:
                audio_service_.EnableDeviceAec(false);
                display->ShowNotification(Lang::Strings::RTC_MODE_OFF);
                break;
            case kAecOnServerSide:
                audio_service_.EnableDeviceAec(false);
                display->ShowNotification(Lang::Strings::RTC_MODE_ON);
                break;
            case kAecOnDeviceSide:
                audio_service_.EnableDeviceAec(true);
                display->ShowNotification(Lang::Strings::RTC_MODE_ON);
                break;
        }

        // If the AEC mode is changed, close the audio channel
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
    });
}

void Application::PlaySound(const std::string_view& sound) { audio_service_.PlaySound(sound); }

// DP040B_FASE2B_STRONG_AUDIO_HOOK
//
// Implementación FUERTE del puente que care-voice declara como weak.
// El runtime de DP-040B llama a xiaozhi_care_reminder_audio_notify(); esta
// implementación copia el OGG y agenda la reproducción en el task principal.
bool Application::RequestCareReminderAudio(const std::string& target_id,
                                           const std::string& message,
                                           const uint8_t* audio_data,
                                           size_t audio_size) {
    if (audio_data == nullptr || audio_size == 0) {
        ESP_LOGW(TAG,
                 "Care reminder audio ignored: empty audio target=%s",
                 target_id.c_str());
        return false;
    }

    const DeviceState state = GetDeviceState();
    if (state != kDeviceStateIdle) {
        ESP_LOGW(TAG,
                 "Care reminder audio deferred because device is busy: state=%d target=%s",
                 static_cast<int>(state),
                 target_id.c_str());
        return false;
    }

    // DP044A_REMINDER_AUDIO_PRIORITY
    // Reservamos una sola presentación local a la vez. El true que devolvemos
    // significa "aceptado y retenido para presentación", no sólo que Schedule()
    // fue llamado. Si el estado cambia antes de reproducir, queda pendiente
    // hasta que el dispositivo vuelva a Idle.
    bool expected = false;
    if (!care_reminder_audio_reserved_.compare_exchange_strong(expected, true)) {
        ESP_LOGW(TAG,
                 "Care reminder audio deferred because another Care audio is reserved: target=%s",
                 target_id.c_str());
        return false;
    }

    std::string audio_copy(reinterpret_cast<const char*>(audio_data), audio_size);
    const std::string safe_message =
        message.empty() ? "Recordatorio" : message;

    Schedule([this,
              target_id,
              safe_message,
              audio = std::move(audio_copy)]() mutable {
        care_reminder_audio_target_ = target_id;
        care_reminder_audio_message_ = safe_message;
        care_reminder_audio_cache_ = std::move(audio);
        care_reminder_audio_pending_ = true;

        ESP_LOGI(TAG,
                 "DP044A Care reminder reserved: target=%s bytes=%u",
                 care_reminder_audio_target_.c_str(),
                 static_cast<unsigned>(care_reminder_audio_cache_.size()));

        MaybeStartCareReminderAudio();
    });

    return true;
}

// DP044B_UNIVERSAL_ACK
bool Application::RequestCareAcknowledgementListening(const std::string& target_id) {
    if (target_id.empty()) return false;

    Schedule([this, target_id]() {
        // DP044B3_PHYSICAL_ACK
        {
            std::lock_guard<std::mutex> lock(care_ack_physical_mutex_);
            care_ack_physical_target_ = target_id;
        }
        care_ack_listening_target_ = target_id;
        care_ack_listening_requested_ = true;
        ClearCareAckResponseWindow();

        ESP_LOGI(TAG,
                 "DP044B acknowledgement listening reserved: target=%s",
                 care_ack_listening_target_.c_str());

        // DP044B1_4B_DELAYED_ACK_PROMPT
        // Con OGG propio esperamos a que FinishCareReminderAudio() marque
        // realmente el final del aviso. Sin OGG local usamos este momento como
        // referencia para conservar la misma UX de espera.
        if (!care_reminder_audio_reserved_.load()) {
            ArmCareAckPromptDelay(target_id);
        }
    });
    return true;
}

void Application::CancelCareAckPromptDelay() {
    if (care_ack_prompt_delay_timer_handle_ != nullptr) {
        esp_timer_stop(care_ack_prompt_delay_timer_handle_);
    }
    care_ack_prompt_delay_pending_ = false;
    care_ack_prompt_delay_target_.clear();
}

void Application::ArmCareAckPromptDelay(const std::string& target) {
    CancelCareAckPromptDelay();

    if (target.empty() ||
        !::xiaozhi_care_ack_pending_for_target(target.c_str())) {
        ESP_LOGI(TAG,
                 "DP044B1.4b ACK delay not armed: target=%s no longer pending",
                 target.c_str());
        return;
    }

    if (care_ack_prompt_delay_timer_handle_ == nullptr) {
        ESP_LOGW(TAG,
                 "DP044B1.4b ACK delay timer unavailable; starting confirmation immediately: target=%s",
                 target.c_str());
        MaybeStartCareAcknowledgementListening();
        return;
    }

    care_ack_prompt_delay_pending_ = true;
    care_ack_prompt_delay_target_ = target;

    // DP044B2_CONFIG_ACK_AND_VOICE_LED
    const uint32_t ack_delay_seconds =
        ::xiaozhi_care_ack_prompt_delay_seconds();
    const int64_t ack_delay_us =
        ack_delay_seconds <= 60
            ? static_cast<int64_t>(ack_delay_seconds) * 1000LL * 1000LL
            : kCareAckPromptDelayUs;

    if (ack_delay_seconds == 0) {
        care_ack_prompt_delay_pending_ = false;
        care_ack_prompt_delay_target_.clear();
        ESP_LOGI(TAG,
                 "DP044B2 ACK confirmation delay=0; starting immediately: target=%s",
                 target.c_str());
        MaybeStartCareAcknowledgementListening();
        return;
    }

    const esp_err_t err =
        esp_timer_start_once(care_ack_prompt_delay_timer_handle_,
                             ack_delay_us);
    if (err != ESP_OK) {
        care_ack_prompt_delay_pending_ = false;
        care_ack_prompt_delay_target_.clear();
        ESP_LOGE(TAG,
                 "DP044B1.4b failed to arm ACK delay: target=%s err=%d; starting immediately",
                 target.c_str(),
                 static_cast<int>(err));
        MaybeStartCareAcknowledgementListening();
        return;
    }

    ESP_LOGI(TAG,
             "DP044B1.4b ACK confirmation delayed: target=%s delay_ms=%u",
             target.c_str(),
             static_cast<unsigned>(ack_delay_seconds * 1000U));

    // Durante este minuto la radio puede volver a sonar si el usuario la tenía
    // activa. El prompt de confirmación volverá a pausarla antes de reproducirse.
    if (GetDeviceState() == kDeviceStateIdle &&
        !care_reminder_audio_reserved_.load()) {
        xiaozhi_care::radio::RadioService::GetInstance().SetSystemPaused(false);
    }
}

void Application::HandleCareAckPromptDelayElapsed() {
    if (!care_ack_prompt_delay_pending_) return;

    const std::string target = care_ack_prompt_delay_target_;
    care_ack_prompt_delay_pending_ = false;
    care_ack_prompt_delay_target_.clear();

    if (!care_ack_listening_requested_ ||
        care_ack_listening_target_ != target ||
        !::xiaozhi_care_ack_pending_for_target(target.c_str())) {
        ESP_LOGI(TAG,
                 "DP044B1.4b ACK confirmation delay cancelled: target=%s no longer pending",
                 target.c_str());
        if (GetDeviceState() == kDeviceStateIdle &&
            !care_reminder_audio_reserved_.load()) {
            xiaozhi_care::radio::RadioService::GetInstance().SetSystemPaused(false);
        }
        return;
    }

    ESP_LOGI(TAG,
             "DP044B1.4b ACK confirmation delay elapsed: target=%s",
             target.c_str());

    MaybeStartCareAcknowledgementListening();
}

void Application::ClearCareAckResponseWindow() {
    if (care_ack_response_timer_handle_ != nullptr) {
        esp_timer_stop(care_ack_response_timer_handle_);
    }
    care_ack_response_target_.clear();
    care_ack_response_deadline_us_ = 0;
    care_ack_response_attempts_ = 0;
    care_ack_listening_cycles_ = 0;
    care_ack_retry_listening_only_ = false;
}

void Application::ArmCareAckResponseWindow(const std::string& target) {
    ClearCareAckResponseWindow();

    care_ack_response_target_ = target;
    care_ack_response_deadline_us_ =
        esp_timer_get_time() + kCareAckResponseWindowUs;
    care_ack_response_attempts_ = 0;
    care_ack_listening_cycles_ = 1;

    if (care_ack_response_timer_handle_ != nullptr) {
        esp_timer_start_once(care_ack_response_timer_handle_,
                             kCareAckResponseWindowUs);
    }

    ESP_LOGI(TAG,
             "DP044B1.4 ACK window armed: target=%s window_ms=30000 cycles=1/%u",
             target.c_str(),
             static_cast<unsigned>(kCareAckMaxListeningCycles));
}

void Application::HandleCareAckResponseTimeout() {
    if (care_ack_response_target_.empty()) return;

    const std::string target = care_ack_response_target_;

    // DP044B1_6_ACK_RUNTIME_SYNC
    // Guardia final: si MCP confirmó entre el último cambio de estado y el
    // vencimiento del timer, no informar falsamente "ACK window expired".
    if (!::xiaozhi_care_ack_pending_for_target(target.c_str())) {
        if (care_ack_listening_target_ == target) {
            care_ack_listening_requested_ = false;
            care_ack_listening_target_.clear();
        }
        care_ack_prompt_active_ = false;
        care_ack_prompt_audio_cache_.clear();
        CancelCareAckPromptDelay();
        ClearCareAckResponseWindow();
        ESP_LOGI(TAG,
                 "DP044B1.6 ACK timer closed: target=%s already acknowledged by runtime",
                 target.c_str());
        return;
    }

    ESP_LOGI(TAG,
             "DP044B1.4 ACK window expired: target=%s attempts=%u/%u cycles=%u/%u",
             target.c_str(),
             static_cast<unsigned>(care_ack_response_attempts_),
             static_cast<unsigned>(kCareAckMaxResponseAttempts),
             static_cast<unsigned>(care_ack_listening_cycles_),
             static_cast<unsigned>(kCareAckMaxListeningCycles));

    if (care_ack_listening_target_ == target) {
        care_ack_listening_requested_ = false;
        care_ack_listening_target_.clear();
    }
    CancelCareAckPromptDelay();
    ClearCareAckResponseWindow();

    if (GetDeviceState() == kDeviceStateIdle &&
        !care_reminder_audio_reserved_.load()) {
        xiaozhi_care::radio::RadioService::GetInstance().SetSystemPaused(false);
    }
}

bool Application::MaybeStartCareAcknowledgementListening() {
    if (!care_ack_listening_requested_) return false;

    // DP044B1_4B_DELAYED_ACK_PROMPT
    if (care_ack_prompt_delay_pending_) return false;

    if (care_ack_listening_target_.empty() ||
        !::xiaozhi_care_ack_pending_for_target(care_ack_listening_target_.c_str())) {
        ESP_LOGI(TAG,
                 "DP044B acknowledgement listening cancelled: target=%s no longer pending",
                 care_ack_listening_target_.c_str());
        care_ack_listening_requested_ = false;
        care_ack_listening_target_.clear();
        care_ack_prompt_active_ = false;
        care_ack_prompt_audio_cache_.clear();
        CancelCareAckPromptDelay();
        ClearCareAckResponseWindow();
        if (GetDeviceState() == kDeviceStateIdle &&
            !care_reminder_audio_reserved_.load()) {
            xiaozhi_care::radio::RadioService::GetInstance().SetSystemPaused(false);
        }
        return false;
    }

    // Si el OGG de DP044A todavía está reservado, FinishCareReminderAudio()
    // volverá a intentarlo después del post-roll.
    if (care_reminder_audio_reserved_.load()) return false;
    if (GetDeviceState() != kDeviceStateIdle) return false;
    if (!audio_service_.IsPlaybackIdle()) return false;
    if (!protocol_) return false;

    const std::string target = care_ack_listening_target_;
    const ListeningMode mode = GetDefaultListeningMode();
    const bool retry_listening_only = care_ack_retry_listening_only_;

    // DP044B1_3_USER_ACK_PROMPT
    // Si la familia cargó una pregunta de confirmación desde Mantenimiento,
    // la reproducimos completa ANTES de abrir el micrófono. El archivo vive
    // fuera del índice de los 12 audios de recordatorios.
    xiaozhi_care::radio::RadioService::GetInstance().SetSystemPaused(true);

    if (retry_listening_only) {
        // DP044B1_4_EXTENDED_ACK_WINDOW
        // En reaperturas automáticas no repetimos la pregunta completa; usamos
        // sólo el cue nativo para indicar que el micrófono vuelve a escuchar.
        play_popup_on_listening_ = true;
        ESP_LOGI(TAG,
                 "DP044B1.4 reopening ACK listening: target=%s cycle=%u/%u",
                 target.c_str(),
                 static_cast<unsigned>(care_ack_listening_cycles_ + 1),
                 static_cast<unsigned>(kCareAckMaxListeningCycles));
    } else if (care_ack_prompt_active_) {
        // Llegamos aquí por MAIN_EVENT_PLAYBACK_DRAINED: la pregunta terminó.
        care_ack_prompt_active_ = false;
        care_ack_prompt_audio_cache_.clear();
        ESP_LOGI(TAG,
                 "DP044B1.3 acknowledgement prompt finished: target=%s",
                 target.c_str());
    } else {
        std::string prompt_audio;
        uint32_t prompt_duration_ms = 0;

        if (::xiaozhi_care_ack_prompt_load(&prompt_audio,
                                           &prompt_duration_ms) &&
            !prompt_audio.empty()) {
            care_ack_prompt_audio_cache_ = std::move(prompt_audio);
            care_ack_prompt_active_ = true;

            ESP_LOGI(TAG,
                     "DP044B1.3 acknowledgement prompt started: target=%s bytes=%u duration_ms=%u",
                     target.c_str(),
                     static_cast<unsigned>(care_ack_prompt_audio_cache_.size()),
                     static_cast<unsigned>(prompt_duration_ms));

            Alert("XiaoZhi Care",
                  "¿Escuchaste el recordatorio?",
                  "neutral",
                  std::string_view(care_ack_prompt_audio_cache_.data(),
                                   care_ack_prompt_audio_cache_.size()));

            if (!audio_service_.IsPlaybackIdle()) {
                return true;
            }

            // Un archivo que no haya producido paquetes no debe bloquear el ACK.
            ESP_LOGW(TAG,
                     "DP044B1.3 acknowledgement prompt produced no playback; using native cue: target=%s",
                     target.c_str());
            care_ack_prompt_active_ = false;
            care_ack_prompt_audio_cache_.clear();
            play_popup_on_listening_ = true;
        } else {
            // Sin audio configurado conservamos el fallback seguro de B1.2.
            play_popup_on_listening_ = true;
            ESP_LOGW(TAG,
                     "DP044B1.3 acknowledgement prompt not configured; using native cue: target=%s",
                     target.c_str());
        }
    }

    care_ack_listening_requested_ = false;
    care_ack_listening_target_.clear();
    care_ack_retry_listening_only_ = false;

    if (!protocol_->IsAudioChannelOpened()) {
        if (!SetDeviceState(kDeviceStateConnecting)) {
            play_popup_on_listening_ = false;
            care_ack_listening_requested_ = true;
            care_ack_listening_target_ = target;
            care_ack_retry_listening_only_ = retry_listening_only;
            return false;
        }
        Schedule([this, mode]() { ContinueOpenAudioChannel(mode); });
    } else {
        SetListeningMode(mode);
        if (GetDeviceState() != kDeviceStateListening) {
            play_popup_on_listening_ = false;
            care_ack_listening_requested_ = true;
            care_ack_listening_target_ = target;
            care_ack_retry_listening_only_ = retry_listening_only;
            return false;
        }
    }

    if (retry_listening_only) {
        if (!care_ack_response_target_.empty() &&
            care_ack_response_target_ == target &&
            care_ack_listening_cycles_ < kCareAckMaxListeningCycles) {
            ++care_ack_listening_cycles_;
        }
    } else {
        ArmCareAckResponseWindow(target);
    }

    ESP_LOGI(TAG,
             "DP044B acknowledgement listening started: target=%s cycle=%u/%u",
             target.c_str(),
             static_cast<unsigned>(care_ack_listening_cycles_),
             static_cast<unsigned>(kCareAckMaxListeningCycles));
    return true;
}

void Application::MaybeStartCareReminderAudio() {
    if (!care_reminder_audio_reserved_.load() ||
        !care_reminder_audio_pending_ ||
        care_reminder_audio_preparing_ ||
        care_reminder_audio_active_ ||
        care_reminder_audio_finishing_) {
        return;
    }

    if (GetDeviceState() != kDeviceStateIdle) {
        ESP_LOGI(TAG,
                 "DP044A Care reminder waiting for Idle: target=%s state=%d",
                 care_reminder_audio_target_.c_str(),
                 static_cast<int>(GetDeviceState()));
        return;
    }

    if (!audio_service_.IsPlaybackIdle()) {
        ESP_LOGI(TAG,
                 "DP044A Care reminder waiting for playback drain: target=%s",
                 care_reminder_audio_target_.c_str());
        return;
    }

    auto& radio = xiaozhi_care::radio::RadioService::GetInstance();
    radio.SetSystemPaused(true);

    auto codec = Board::GetInstance().GetAudioCodec();
    care_reminder_previous_volume_ = -1;
    care_reminder_boosted_volume_ = -1;
    care_reminder_volume_boosted_ = false;

    if (codec != nullptr) {
        care_reminder_previous_volume_ = codec->output_volume();
        const int reminder_volume =
            care_reminder_previous_volume_ < kCareReminderMinVolume
                ? kCareReminderMinVolume
                : care_reminder_previous_volume_;

        if (reminder_volume != care_reminder_previous_volume_) {
            codec->SetOutputVolume(reminder_volume);
            care_reminder_boosted_volume_ = reminder_volume;
            care_reminder_volume_boosted_ = true;
            ESP_LOGI(TAG,
                     "DP044A reminder volume boosted: previous=%d reminder=%d",
                     care_reminder_previous_volume_,
                     reminder_volume);
        }
    }

    care_reminder_audio_preparing_ = true;

    if (care_reminder_audio_pre_timer_handle_ == nullptr) {
        esp_timer_create_args_t timer_args = {
            .callback = [](void* arg) {
                auto* app = static_cast<Application*>(arg);
                app->Schedule([app]() { app->StartCareReminderAudioNow(); });
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "care_audio_pre",
            .skip_unhandled_events = true,
        };

        const esp_err_t timer_err =
            esp_timer_create(&timer_args, &care_reminder_audio_pre_timer_handle_);
        if (timer_err != ESP_OK) {
            care_reminder_audio_pre_timer_handle_ = nullptr;
            ESP_LOGE(TAG,
                     "DP044A failed to create pre-roll timer: err=%d; starting immediately",
                     static_cast<int>(timer_err));
        }
    }

    if (care_reminder_audio_pre_timer_handle_ != nullptr) {
        esp_timer_stop(care_reminder_audio_pre_timer_handle_);
        const esp_err_t err =
            esp_timer_start_once(care_reminder_audio_pre_timer_handle_,
                                 kCareReminderAudioPreRollUs);
        if (err == ESP_OK) {
            ESP_LOGI(TAG,
                     "DP044A radio paused; reminder pre-roll armed: %llu ms target=%s",
                     static_cast<unsigned long long>(kCareReminderAudioPreRollUs / 1000ULL),
                     care_reminder_audio_target_.c_str());
            return;
        }

        ESP_LOGE(TAG,
                 "DP044A failed to arm pre-roll timer: err=%d; starting immediately",
                 static_cast<int>(err));
    }

    StartCareReminderAudioNow();
}

void Application::StartCareReminderAudioNow() {
    if (!care_reminder_audio_reserved_.load() ||
        !care_reminder_audio_pending_ ||
        !care_reminder_audio_preparing_) {
        return;
    }

    if (GetDeviceState() != kDeviceStateIdle) {
        ESP_LOGI(TAG,
                 "DP044A reminder pre-roll interrupted by device activity; keeping pending: target=%s state=%d",
                 care_reminder_audio_target_.c_str(),
                 static_cast<int>(GetDeviceState()));
        care_reminder_audio_preparing_ = false;
        RestoreCareReminderVolume();
        return;
    }

    if (!audio_service_.IsPlaybackIdle()) {
        ESP_LOGI(TAG,
                 "DP044A reminder pre-roll found audio busy; keeping pending: target=%s",
                 care_reminder_audio_target_.c_str());
        care_reminder_audio_preparing_ = false;
        RestoreCareReminderVolume();
        return;
    }

    care_reminder_audio_preparing_ = false;
    care_reminder_audio_pending_ = false;
    care_reminder_audio_active_ = true;

    ESP_LOGI(TAG,
             "DP044A Care reminder playback started: target=%s bytes=%u message=%s",
             care_reminder_audio_target_.c_str(),
             static_cast<unsigned>(care_reminder_audio_cache_.size()),
             care_reminder_audio_message_.c_str());

    Alert("XiaoZhi Care",
          care_reminder_audio_message_.c_str(),
          "neutral",
          std::string_view(care_reminder_audio_cache_.data(),
                           care_reminder_audio_cache_.size()));

    // DP040C_OLED_AUTO_RETURN
    // El Alert() deja el texto del recordatorio en pantalla. Lo mantenemos
    // 12 segundos y luego volvemos a STANDBY sólo si XiaoZhi sigue en Idle.
    if (care_reminder_display_timer_handle_ == nullptr) {
        esp_timer_create_args_t timer_args = {
            .callback = [](void* arg) {
                auto* app = static_cast<Application*>(arg);
                app->Schedule([app]() {
                    if (app->GetDeviceState() == kDeviceStateIdle) {
                        ESP_LOGI(TAG,
                                 "DP-040C OLED reminder timeout; restoring normal display");
                        app->DismissAlert();
                    } else {
                        ESP_LOGI(TAG,
                                 "DP-040C OLED reminder timeout ignored because device is busy");
                    }
                });
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "care_oled_reminder",
            .skip_unhandled_events = true,
        };

        const esp_err_t timer_err =
            esp_timer_create(&timer_args, &care_reminder_display_timer_handle_);

        if (timer_err != ESP_OK) {
            care_reminder_display_timer_handle_ = nullptr;
            ESP_LOGE(TAG,
                     "DP-040C failed to create OLED reminder timer: err=%d",
                     static_cast<int>(timer_err));
        }
    }

    if (care_reminder_display_timer_handle_ != nullptr) {
        esp_timer_stop(care_reminder_display_timer_handle_);
        const esp_err_t start_err =
            esp_timer_start_once(care_reminder_display_timer_handle_,
                                 kCareReminderDisplayTimeoutUs);

        if (start_err == ESP_OK) {
            ESP_LOGI(TAG,
                     "DP-040C OLED reminder timeout armed: %llu ms",
                     static_cast<unsigned long long>(
                         kCareReminderDisplayTimeoutUs / 1000ULL));
        } else {
            ESP_LOGE(TAG,
                     "DP-040C failed to arm OLED reminder timer: err=%d",
                     static_cast<int>(start_err));
        }
    }

    // Un OGG inválido podría no agregar ningún paquete y, por lo tanto, no
    // generar un nuevo evento PLAYBACK_DRAINED. Cerramos el ciclo igualmente.
    if (audio_service_.IsPlaybackIdle()) {
        ESP_LOGW(TAG,
                 "DP044A Care reminder produced no queued playback; finishing safely: target=%s",
                 care_reminder_audio_target_.c_str());
        BeginCareReminderAudioPostRoll();
    }
}

void Application::BeginCareReminderAudioPostRoll() {
    if (!care_reminder_audio_active_ || care_reminder_audio_finishing_) {
        return;
    }

    care_reminder_audio_active_ = false;
    care_reminder_audio_finishing_ = true;

    if (care_reminder_audio_post_timer_handle_ == nullptr) {
        esp_timer_create_args_t timer_args = {
            .callback = [](void* arg) {
                auto* app = static_cast<Application*>(arg);
                app->Schedule([app]() { app->FinishCareReminderAudio(); });
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "care_audio_post",
            .skip_unhandled_events = true,
        };

        const esp_err_t timer_err =
            esp_timer_create(&timer_args, &care_reminder_audio_post_timer_handle_);
        if (timer_err != ESP_OK) {
            care_reminder_audio_post_timer_handle_ = nullptr;
            ESP_LOGE(TAG,
                     "DP044A failed to create post-roll timer: err=%d; finishing immediately",
                     static_cast<int>(timer_err));
        }
    }

    if (care_reminder_audio_post_timer_handle_ != nullptr) {
        esp_timer_stop(care_reminder_audio_post_timer_handle_);
        const esp_err_t err =
            esp_timer_start_once(care_reminder_audio_post_timer_handle_,
                                 kCareReminderAudioPostRollUs);
        if (err == ESP_OK) {
            ESP_LOGI(TAG,
                     "DP044A reminder playback drained; post-roll armed: %llu ms target=%s",
                     static_cast<unsigned long long>(kCareReminderAudioPostRollUs / 1000ULL),
                     care_reminder_audio_target_.c_str());
            return;
        }

        ESP_LOGE(TAG,
                 "DP044A failed to arm post-roll timer: err=%d; finishing immediately",
                 static_cast<int>(err));
    }

    FinishCareReminderAudio();
}

void Application::RestoreCareReminderVolume() {
    if (!care_reminder_volume_boosted_) {
        care_reminder_previous_volume_ = -1;
        care_reminder_boosted_volume_ = -1;
        return;
    }

    auto codec = Board::GetInstance().GetAudioCodec();
    if (codec != nullptr &&
        care_reminder_previous_volume_ >= 0 &&
        codec->output_volume() == care_reminder_boosted_volume_) {
        codec->SetOutputVolume(care_reminder_previous_volume_);
        ESP_LOGI(TAG,
                 "DP044A reminder volume restored: volume=%d",
                 care_reminder_previous_volume_);
    } else if (codec != nullptr) {
        ESP_LOGI(TAG,
                 "DP044A reminder volume not restored because it changed externally: current=%d expected=%d",
                 codec->output_volume(),
                 care_reminder_boosted_volume_);
    }

    care_reminder_volume_boosted_ = false;
    care_reminder_previous_volume_ = -1;
    care_reminder_boosted_volume_ = -1;
}

void Application::FinishCareReminderAudio() {
    if (!care_reminder_audio_finishing_) {
        return;
    }

    care_reminder_audio_finishing_ = false;
    RestoreCareReminderVolume();

    ESP_LOGI(TAG,
             "DP044A Care reminder playback finished: target=%s state=%d",
             care_reminder_audio_target_.c_str(),
             static_cast<int>(GetDeviceState()));

    care_reminder_audio_cache_.clear();
    care_reminder_audio_target_.clear();
    care_reminder_audio_message_.clear();

    // DP044B_UNIVERSAL_ACK + DP044B1_4B_DELAYED_ACK_PROMPT
    // Liberamos el audio del recordatorio y dejamos pasar un minuto completo
    // antes de preguntar si fue escuchado. El conteo comienza aquí: el aviso
    // local ya terminó físicamente y también finalizó el post-roll de DP044A.
    care_reminder_audio_reserved_.store(false);

    if (care_ack_listening_requested_ &&
        !care_ack_listening_target_.empty()) {
        ArmCareAckPromptDelay(care_ack_listening_target_);
    } else if (GetDeviceState() == kDeviceStateIdle) {
        xiaozhi_care::radio::RadioService::GetInstance().SetSystemPaused(false);
    }
}

extern "C" bool xiaozhi_care_reminder_audio_notify(
    const char* target_id,
    const char* message,
    const uint8_t* audio_data,
    size_t audio_size) {
    const char* safe_target = target_id ? target_id : "";
    const char* safe_message = message ? message : "";

    ESP_LOGI(TAG,
             "Care reminder audio hook requested: target=%s bytes=%u message=%s",
             safe_target,
             static_cast<unsigned>(audio_size),
             safe_message);

    return Application::GetInstance().RequestCareReminderAudio(
        std::string(safe_target),
        std::string(safe_message),
        audio_data,
        audio_size);
}


// DP044B_UNIVERSAL_ACK
extern "C" bool xiaozhi_care_ack_listening_notify(const char* target_id) {
    if (target_id == nullptr || target_id[0] == '\0') return false;
    return Application::GetInstance().RequestCareAcknowledgementListening(target_id);
}


void Application::ResetProtocol() {
    Schedule([this]() {
        if (GetDeviceState() == kDeviceStateNotifying) {
            StopNotification();
        }
        // Close audio channel if opened
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
        // Reset protocol
        protocol_.reset();
    });
}
