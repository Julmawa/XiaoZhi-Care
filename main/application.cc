#include "care_daily/care_ack_journal.h"
#include "care_daily/care_ack_latch.h"
#include "care_restore.h"
#include "care_voice/reminder_audio_bridge.h"
#if __has_include("care_telegram_private.h")
#include "care_telegram_private.h"
#endif
#ifndef CARE_HELP_DEVICE_LABEL
#define CARE_HELP_DEVICE_LABEL "CARE"
#endif
#include "nvs.h"
#include "care_telegram_pair.h"
#include "sdkconfig.h"
#include "care_telegram_probe.h"
#include "care_telegram_tts_config.h"
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
#include <ctime>
#include <limits>

#define TAG "Application"


// DP-CF-007_HELP_TO_HA
// Matcher V1 deliberadamente conservador: solo frases exactas.
// Incluye "necesito ayudarme" porque fue una transcripción STT real observada.
bool IsCareHelpRequestText(const std::string& text) {
    // DP-CF-007_R3_HELP_NORMALIZATION
    std::string value;
    value.reserve(text.size());

    bool previous_space = true;

    auto append_char = [&](char c) {
        if (c == ' ') {
            if (!previous_space && !value.empty()) {
                value.push_back(' ');
                previous_space = true;
            }
            return;
        }

        value.push_back(c);
        previous_space = false;
    };

    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);

        if (c < 0x80) {
            char out = static_cast<char>(c);

            if (out >= 'A' && out <= 'Z') {
                out = static_cast<char>(out - 'A' + 'a');
            }

            const bool is_letter_or_digit =
                (out >= 'a' && out <= 'z') ||
                (out >= '0' && out <= '9');

            if (is_letter_or_digit) {
                append_char(out);
            } else {
                append_char(' ');
            }

            ++i;
            continue;
        }

        if (c == 0xC3 && i + 1 < text.size()) {
            const unsigned char n = static_cast<unsigned char>(text[i + 1]);
            char replacement = 0;

            switch (n) {
                case 0x81: case 0xA1: replacement = 'a'; break;
                case 0x89: case 0xA9: replacement = 'e'; break;
                case 0x8D: case 0xAD: replacement = 'i'; break;
                case 0x93: case 0xB3: replacement = 'o'; break;
                case 0x9A: case 0xBA: replacement = 'u'; break;
                case 0x9C: case 0xBC: replacement = 'u'; break;
                case 0x91: case 0xB1: replacement = 'n'; break;
                default: break;
            }

            if (replacement != 0) {
                append_char(replacement);
            } else {
                append_char(' ');
            }

            i += 2;
            continue;
        }

        append_char(' ');
        ++i;
    }

    while (!value.empty() && value.back() == ' ') {
        value.pop_back();
    }

    static constexpr const char* kHelpRequests[] = {
        "necesito ayuda",
        "necesito ayudarme",
        "ayudame",
        "por favor ayudame",
        "no me siento bien",
        "me siento mal",
        "llama a alguien",
        "por favor llama a alguien",
    };

    for (const char* candidate : kHelpRequests) {
        if (value == candidate) {
            return true;
        }
    }

    return false;
}
#include "care_homeassistant.h"

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
}

Application::~Application() {
    notify_player_.Stop();

    // DP040C_OLED_AUTO_RETURN
    if (care_reminder_display_timer_handle_ != nullptr) {
        esp_timer_stop(care_reminder_display_timer_handle_);
        esp_timer_delete(care_reminder_display_timer_handle_);
        care_reminder_display_timer_handle_ = nullptr;
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

    // DP-CF-001 — Home Assistant / CARE Familia -> voz XiaoZhi.
    xiaozhi_care::CareWebServer::GetInstance().SetFamilyMessageCallback(
        [this](const std::string& sender, const std::string& message) -> bool {
            return RequestCareFamilyMessageNotification(sender, message);
        });

    // Telegram consumes the server-side queue only after the finite PCM job completes.
    xiaozhi_care::SetCareTelegramProbeReceivedCallback(
        [this](int64_t id, const std::string& account, const std::string& message) -> bool {
            static int64_t offered = -1;
            static std::shared_ptr<std::atomic<int>> receipt;
            if (receipt && offered == id) {
                const int state = receipt->load();
                if (state > 0) { receipt.reset(); return true; }
                if (state == 0) return false;
            }
            offered = id;
            receipt = std::make_shared<std::atomic<int>>(0);
            Schedule([this, account, message, done = receipt]() {
                auto& radio = xiaozhi_care::radio::RadioService::GetInstance();
                std::string sender;
                bool unavailable=false;
                if (!xiaozhi_care::ResolveTelegramSender(account,sender,&unavailable)) {
                    done->store(unavailable ? -1 : 1); // retry unavailable storage; never play a revoked account
                    return;
                }
                if (GetDeviceState() != kDeviceStateIdle || notify_player_.IsBusy() ||
                    radio.WantsPlaying() || radio.IsStreaming() || radio.OneShotActive() ||
                    pending_telegram_cue_.cue || care_recording_active_.load() || audio_service_.HasCarePriority() ||
                    !audio_service_.IsPlaybackIdle() ||
                    CARE_TG_VOICERSS_KEY[0] == '\0') {
                    done->store(-1);
                    return;
                }
                auto encode = [](const std::string& value) {
                    std::string out;
                    for (unsigned char c : value) {
                        if ((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||
                            c=='-'||c=='_'||c=='.'||c=='~') out += char(c);
                        else {
                            out += '%';
                            out += "0123456789ABCDEF"[c>>4];
                            out += "0123456789ABCDEF"[c&15];
                        }
                    }
                    return out;
                };
                const std::string url = std::string("https://api.voicerss.org/?key=") + encode(CARE_TG_VOICERSS_KEY) +
                    "&hl=es-mx&v=Silvia&c=MP3&f=16khz_16bit_mono&src=" +
                    encode("Tenés un mensaje de "+sender+". Dice: "+message);
                auto cue = audio_service_.PlayTrackedCue(Lang::Sounds::OGG_POPUP);
                if (!cue) { done->store(-1); return; }
                pending_telegram_cue_ = {account,url,done,std::move(cue),
                    esp_timer_get_time()+5000000,telegram_conversation_epoch_.load()};
            });
            return false;
        });

    // DP-TG-018: interfaz separada, preserva el flujo previo HA/Piper.
    xiaozhi_care::CareWebServer::GetInstance().SetFamilyAudioCallback(
        [this](const std::string& url) -> bool {
            return RequestCareFamilyAudioPlayback(url);
        });

    // DP042A_RADIO_MP3_BASE
    auto& care_radio =
        xiaozhi_care::radio::RadioService::GetInstance();
    care_radio.Init(
        [this](std::vector<int16_t>& pcm,
               int sample_rate,
               int channels) -> bool {
            return !care_recording_active_.load() && audio_service_.OutputExternalPcm(
                pcm, sample_rate, channels);
        });
    care_radio.SetSystemPaused(
        GetDeviceState() != kDeviceStateIdle);

    // DP043C_RADIO_FINISHING
    care_radio.SetNetworkActivityCallback([this](bool active) {
        Schedule([this, active]() {
            // DP-CF-006: clearing family orange on audio end
            if (!active) {
                auto* led = Board::GetInstance().GetLed();
                if (led != nullptr) {
                    led->ClearCareFamilyMessage();
                }
            }

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
        ++telegram_conversation_epoch_;
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
            }
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
            unsigned remaining=8;
            for (; remaining; --remaining) {
                auto packet=audio_service_.PopPacketFromSendQueue();
                if (!packet) break;
                if (protocol_ && !protocol_->SendAudio(std::move(packet))) {
                    audio_service_.DiscardPendingSendAudio();
                    break;
                }
            }
            if (!remaining) xEventGroupSetBits(event_group_,MAIN_EVENT_SEND_AUDIO);
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
            // Leave queued callbacks in FIFO order, but yield to input,
            // transport and care events between bounded dispatch batches.
            for (unsigned i=0; i<8 && !main_tasks_.empty(); ++i) {
                auto task=std::move(main_tasks_.front());
                main_tasks_.pop_front();
                lock.unlock();
                task();
                lock.lock();
            }
            const bool pending=!main_tasks_.empty();
            lock.unlock();
            if (pending) xEventGroupSetBits(event_group_,MAIN_EVENT_SCHEDULE);
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
        // Run after input/state events: never commit a start using the prior turn.
        AdvanceTelegramCue();
    }
}

void Application::AdvanceTelegramCue() {
    auto& job = pending_telegram_cue_;
    if (!job.cue) return;
    const auto status = audio_service_.CheckTrackedCue(*job.cue);
    if (status == LocalPlaybackResult::Failed ||
        esp_timer_get_time() >= job.deadline_us || GetDeviceState() != kDeviceStateIdle ||
        job.conversation_epoch != telegram_conversation_epoch_.load() || notify_player_.IsBusy()) {
        job.cue->Fail();
        job.delivery->store(-1);
        job = {};
        return;
    }
    if (status != LocalPlaybackResult::Complete || !audio_service_.IsPlaybackIdle()) return;
    auto ready = std::move(job);
    job = {};
    std::string name;
    bool unavailable = false;
    if (!xiaozhi_care::ResolveTelegramSender(ready.account,name,&unavailable)) {
        ready.delivery->store(unavailable ? -1 : 1);
        return;
    }
    auto& radio = xiaozhi_care::radio::RadioService::GetInstance();
    if (radio.WantsPlaying() || radio.IsStreaming() || radio.OneShotActive() ||
        care_recording_active_.load() || audio_service_.HasCarePriority() || !audio_service_.ResetExternalOutputResampler() ||
        audio_service_.CheckTrackedCue(*ready.cue) != LocalPlaybackResult::Complete ||
        !radio.PlayOneShotUrl(ready.audio_url,false,[done=ready.delivery](bool ok) {
            done->store(ok ? 1 : -1);
        }, [account=ready.account] {
            std::string ignored;
            return xiaozhi_care::ResolveTelegramSender(account,ignored);
        })) {
        ready.delivery->store(-1);
    }
}

void Application::HandleNetworkConnectedEvent() {
    ESP_LOGI(TAG, "Network connected");

    // DP-049 R4.1: Telegram debe comenzar HTTPS solo despues de tener red e IP.
    xiaozhi_care::SetCareTelegramNetworkReady(true);
    CareHomeAssistant::GetInstance().OnNetworkConnected();

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
    // Pausar consultas Telegram mientras no haya conexion.
    xiaozhi_care::SetCareTelegramNetworkReady(false);
    CareHomeAssistant::GetInstance().OnNetworkDisconnected();
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
#if CONFIG_CARE_OTA_PROTECTION
    if (!download_url.empty()) {
        ESP_LOGW(TAG, "CARE ASSETS lock: pending download ignored");
    }
    download_url.clear();  // Keep local assets; do not alter NVS
#endif

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
                    std::vector<TextGlyph> glyphs;
                    uint8_t bpp = 0;
                    if (!TextGlyphPayload::Parse(root, glyphs, bpp)) {
                        glyphs.clear();
                    }
                    ESP_LOGD(TAG, "Assistant text received");
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
                ESP_LOGD(TAG, "Speech transcription received");

                // DP-CF-007_HELP_TO_HA
                const std::string care_stt_message(text->valuestring);
                if (IsCareHelpRequestText(care_stt_message)) {
                    ESP_LOGI(TAG,"Care help phrase detected");

                    Schedule([message = care_stt_message]() {
                        const bool sent =
                            CareHomeAssistant::GetInstance().SendHelpRequest(
                                CARE_HELP_DEVICE_LABEL,
                                message);

                        ESP_LOGI(TAG,"Care help event sent=%d",sent ? 1 : 0);
                    });
                }
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
            ESP_LOGD(TAG, "Custom message received");
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
    ESP_LOGD(TAG, "Application alert rendered");
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

    // DP042A_RADIO_MP3_BASE
    // Sólo suena en idle. Al escuchar/hablar/notificar/conectar,
    // se descarta el PCM de radio; al volver a idle continúa en vivo.
    xiaozhi_care::radio::RadioService::GetInstance()
        .SetSystemPaused(new_state != kDeviceStateIdle);
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
            break;
        case kDeviceStateConnecting:
            display->SetStatus(Lang::Strings::CONNECTING);
            display->SetEmotion("neutral");
            display->SetChatMessage("system", "");
            break;
        case kDeviceStateListening:
            display->SetStatus(Lang::Strings::LISTENING);
            display->SetEmotion("neutral");

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


// DP-CF-002 — CARE Familia -> Home Assistant/Piper -> MP3 -> parlante.
bool Application::RequestCareFamilyMessageNotification(
    const std::string& sender,
    const std::string& message
) {
    if (sender.empty() || message.empty()) {
        ESP_LOGW(TAG, "DP-CF-002: mensaje familiar vacio");
        return false;
    }

    const std::string speech = std::string("Dice ") + sender + ": " + message;

    std::string audio_url;
    if (!CareHomeAssistant::GetInstance().GetTtsUrl(speech, audio_url)) {
        ESP_LOGE(TAG, "DP-CF-002: no se pudo generar TTS");
        return false;
    }

    ESP_LOGI(TAG, "DP-CF-002: mensaje familiar listo para reproducir");

    Schedule([this, audio_url = std::move(audio_url)]() mutable {
        if (GetDeviceState() != kDeviceStateIdle) {
            ESP_LOGW(TAG, "DP-CF-003: CARE ocupado; mensaje no reproducido");
            return;
        }

        auto& radio =
            xiaozhi_care::radio::RadioService::GetInstance();

        radio.SetSystemPaused(false);

        auto* led = Board::GetInstance().GetLed();
        if (led != nullptr) {
            led->ShowCareFamilyMessage();
        }
        ESP_LOGI(TAG, "DP-CF-006: family message visual started");

        if (!radio.PlayOneShotUrl(audio_url)) {
            if (led != nullptr) {
                led->ClearCareFamilyMessage();
            }
            ESP_LOGE(TAG, "DP-CF-003: no se pudo iniciar MP3 familiar");
            return;
        }

        ESP_LOGI(TAG, "DP-CF-003: MP3 familiar enviado al reproductor");
    });

    return true;
}


// DP-TG-018 - Solo ensayos manuales con CARE inactivo y radio apagada.
// Que se acepte una solicitud NO equivale a haber reproducido el MP3.
bool Application::RequestCareFamilyAudioPlayback(const std::string& audio_url) {
    auto& radio = xiaozhi_care::radio::RadioService::GetInstance();
    if (audio_url.empty() || GetDeviceState() != kDeviceStateIdle ||
        notify_player_.IsBusy() || radio.WantsPlaying() || radio.IsStreaming()) {
        ESP_LOGW(TAG, "DP-TG-018: CARE ocupado; solicitud rechazada");
        return false;
    }

    Schedule([this, audio_url]() {
        auto& radio = xiaozhi_care::radio::RadioService::GetInstance();
        // Revalidar: el estado pudo cambiar antes de ejecutar el trabajo.
        if (GetDeviceState() != kDeviceStateIdle || notify_player_.IsBusy() ||
            radio.WantsPlaying() || radio.IsStreaming()) {
            ESP_LOGW(TAG, "DP-TG-018: CARE se ocupo; audio no iniciado");
            return;
        }
        auto* led = Board::GetInstance().GetLed();
        if (led != nullptr) {
            led->ShowCareFamilyMessage();
        }
        if (!radio.PlayOneShotUrl(audio_url, false)) {
            if (led != nullptr) {
                led->ClearCareFamilyMessage();
            }
            ESP_LOGE(TAG, "DP-TG-018: fallo de inicio de MP3");
            return;
        }
        ESP_LOGI(TAG, "DP-TG-018: solicitud MP3 enviada al reproductor");
    });
    return true;
}

bool Application::RequestCareFamilyMessageTrigger() {
    Schedule([this]() {
        const DeviceState state = GetDeviceState();
        if (state == kDeviceStateSpeaking) AbortSpeaking(kAbortReasonNone);
        if (protocol_ == nullptr) {
            ESP_LOGW(TAG, "DP-CF-001: protocolo XiaoZhi no disponible");
            return;
        }
        if (state == kDeviceStateListening && protocol_->IsAudioChannelOpened()) {
            protocol_->SendStopListening();
        }
        if (state == kDeviceStateListening || state == kDeviceStateSpeaking ||
            state == kDeviceStateConnecting) {
            SetDeviceState(kDeviceStateIdle);
        }
        if (!protocol_->IsAudioChannelOpened()) {
            if (SetDeviceState(kDeviceStateConnecting) && protocol_->OpenAudioChannel()) {
                SetDeviceState(kDeviceStateIdle);
            } else if (GetDeviceState() == kDeviceStateConnecting) {
                SetDeviceState(kDeviceStateIdle);
            }
        }
        if (!protocol_->IsAudioChannelOpened()) {
            ESP_LOGW(TAG, "DP-CF-001: no se pudo abrir canal para mensaje familiar");
            return;
        }
        listening_mode_ = kListeningModeManualStop;
        const bool sent = protocol_->SendTextInput("Mensaje familiar.");
        ESP_LOGI(TAG, "DP-CF-001: trigger mensaje familiar enviado=%d", sent ? 1 : 0);
    });
    return true;
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
#if CONFIG_CARE_OTA_PROTECTION
    (void)url;
    (void)version;
    ESP_LOGW(TAG, "CARE OTA lock: firmware upgrade refused");
    return false;
#endif
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
    ESP_LOGI(TAG, "Starting firmware upgrade");

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

namespace {
using xiaozhi_care::daily::CareAckJournal;
xiaozhi_care::daily::CareAckLatch care_receipt;

bool ReadAckValue(nvs_handle_t h, const char* name, size_t limit, std::string& value) {
    value.clear();
    size_t size=0;
    auto err=nvs_get_str(h,name,nullptr,&size);
    if (err==ESP_ERR_NVS_NOT_FOUND) return true;
    if (err!=ESP_OK || !size || size>limit+1) return false;
    value.resize(size);
    if (nvs_get_str(h,name,value.data(),&size)!=ESP_OK || !size || value[size-1]!='\0') return false;
    value.resize(size-1);
    return true;
}

bool CheckOrSaveAck(const std::string& key, bool write) {
    const auto sep=key.find('|');
    if (sep==std::string::npos ||
        key.substr(0,sep)!=std::to_string(xiaozhi_care::restore::Active().data)) return false;
    const auto date=std::string_view(key).substr(sep+1,10);
    if (write) {
        const std::time_t now=std::time(nullptr);
        std::tm tm{};
        char current[11]{};
        if (!localtime_r(&now,&tm) || std::strftime(current,sizeof(current),"%Y-%m-%d",&tm)!=10 ||
            date!=current) return false;
    }
    nvs_handle_t h;
    const auto err=nvs_open("care_ack",write ? NVS_READWRITE : NVS_READONLY,&h);
    if (!write && err==ESP_ERR_NVS_NOT_FOUND) return CareAckJournal::CanAccept({},date,key);
    if (err!=ESP_OK) return false;
    std::string journal,legacy;
    bool ok=ReadAckValue(h,"v3_day",CareAckJournal::kMaxBytes,journal) &&
        ReadAckValue(h,"latest",CareAckJournal::kMaxKey,legacy) && legacy!=key &&
        CareAckJournal::CanAccept(journal,date,key);
    if (ok && write) {
        ok=CareAckJournal::Append(journal,date,key) &&
            nvs_set_str(h,"v3_day",journal.c_str())==ESP_OK && nvs_commit(h)==ESP_OK;
    }
    nvs_close(h);
    return ok;
}
}
extern "C" void xiaozhi_care_alarm_arm_receipt(const char* date, const char* routine) {
    std::string key;
    if (date && routine && *routine) {
        key = std::to_string(xiaozhi_care::restore::Active().data) + "|" + date + "|" + routine;
        if (!CheckOrSaveAck(key,false)) key.clear();
    }
    care_receipt.Arm(std::move(key),esp_timer_get_time(),date ? date : "");
}
uint64_t Application::CaptureCareReceipt() {
    return care_receipt.Capture(esp_timer_get_time());
}
void Application::ConfirmCareReceipt(uint64_t press) {
    // Keep flash writes out of the dispatcher. At most one write job exists.
    static std::atomic<bool> busy{false};
    if (!press || busy.exchange(true)) return;
    auto value=std::make_unique<uint64_t>(press);
    auto* payload=value.release();
    if (xTaskCreate([](void* arg) {
        {
            std::unique_ptr<uint64_t> press_id(static_cast<uint64_t*>(arg));
            if (care_receipt.Accept(*press_id,esp_timer_get_time(),[](const std::string& key) {
                return CheckOrSaveAck(key,true);
            })) ESP_LOGI(TAG,"Physical receipt saved");
        }
        busy.store(false);
        vTaskDelete(nullptr);
    },"care_ack",4096,payload,3,nullptr)!=pdPASS) {
        delete payload;
        busy.store(false);
        ESP_LOGW(TAG,"Physical receipt not saved: worker unavailable");
    }
}

bool Application::PlayCareSignal() {
    return audio_service_.PlayCareSignal();
}

extern "C" bool xiaozhi_care_alarm_render_signal() {
    return Application::GetInstance().PlayCareSignal();
}

void Application::PlaySound(const std::string_view& sound) { audio_service_.PlaySound(sound); }

// DP040B_FASE2B_STRONG_AUDIO_HOOK
//
// Implementación FUERTE del puente que care-voice declara como weak.
// El runtime de DP-040B llama a xiaozhi_care_reminder_audio_notify(); esta
// implementación copia el OGG y lo envía a un único worker de grabaciones.
bool Application::RequestCareReminderAudio(const std::string&,
                                           const std::string& message,
                                           const uint8_t* audio_data,
                                           size_t audio_size,
                                           std::shared_ptr<std::atomic<int>> result) {
    if (!audio_data || !audio_size || audio_size>32*1024 || GetDeviceState()!=kDeviceStateIdle ||
        care_recording_active_.exchange(true)) return false;

    struct Work {
        Application* app;
        uint32_t epoch;
        std::string audio;
        std::shared_ptr<std::atomic<int>> result;
    };
    // A standalone recording has no verified CareAlarm occurrence. It
    // must not leave a prior occurrence armed while presenting another one.
    if (result) care_receipt.Arm({},esp_timer_get_time());
    const auto epoch=telegram_conversation_epoch_.load();
    auto work=std::make_unique<Work>(Work{this,epoch,
        std::string(reinterpret_cast<const char*>(audio_data),audio_size),std::move(result)});
    auto* payload=work.release();
    if (xTaskCreate([](void* ptr) {
        {
            std::unique_ptr<Work> job(static_cast<Work*>(ptr));
            auto* app=job->app;
            const auto deadline=esp_timer_get_time()+120000000LL;
            const auto valid=[&] {
                return app->GetDeviceState()==kDeviceStateIdle &&
                    app->telegram_conversation_epoch_.load()==job->epoch &&
                    esp_timer_get_time()<deadline;
            };
            auto receipt=valid() ? app->audio_service_.PlayTrackedCue(job->audio,valid) : nullptr;
            bool complete=false;
            while (receipt && valid()) {
                const auto status=app->audio_service_.CheckTrackedCue(*receipt);
                if (status!=LocalPlaybackResult::Pending) {
                    complete=status==LocalPlaybackResult::Complete;
                    break;
                }
                vTaskDelay(1);
            }
            if (receipt && !complete) receipt->Fail();
            if (job->result) job->result->store(complete ? 1 : -1);
            app->care_recording_active_.store(false);
        }
        vTaskDelete(nullptr);
    },"care_ogg",4096,payload,4,nullptr)!=pdPASS) {
        delete payload;
        care_recording_active_.store(false);
        return false;
    }

    const std::string safe_message =
        message.empty() ? "Recordatorio" : message;

    Schedule([this,safe_message,epoch]() {
        if (GetDeviceState()!=kDeviceStateIdle || telegram_conversation_epoch_.load()!=epoch) return;
        Alert("XiaoZhi Care",safe_message.c_str(),"neutral");

        // DP040C_OLED_AUTO_RETURN
        // El Alert() deja el texto del recordatorio en pantalla. Lo mantenemos
        // 12 segundos y luego volvemos a STANDBY sólo si XiaoZhi sigue en Idle.
        // Si la persona empieza a hablar/interactuar, no pisamos esa pantalla.
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
    });

    return true;
}

extern "C" bool xiaozhi_care_reminder_audio_notify(
    const char* target_id,
    const char* message,
    const uint8_t* audio_data,
    size_t audio_size,
    std::shared_ptr<std::atomic<int>> result) {
    return Application::GetInstance().RequestCareReminderAudio(
        target_id ? target_id : "",message ? message : "",audio_data,audio_size,std::move(result));
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
