#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <esp_err.h>
#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>

namespace xiaozhi_care {

struct CareWebHomeAssistantStatus {
    bool ready = false;
    bool network_ready = false;
    bool connected = false;

    bool indoor_available = false;
    float indoor_temperature = 0.0f;
    float indoor_humidity = 0.0f;

    bool weather_available = false;
    float outdoor_temperature = 0.0f;
    int outdoor_humidity = 0;
    std::string outdoor_condition;
};

struct CareWebHomeAssistantEntity {
    std::string entity_id;
    std::string domain;
    std::string friendly_name;
    std::string state;
};


class CareWebServer {
public:
    static CareWebServer& GetInstance();

    CareWebServer(const CareWebServer&) = delete;
    CareWebServer& operator=(const CareWebServer&) = delete;

    esp_err_t Start();
    esp_err_t Stop();
    bool IsRunning() const;

    // DP039_AUDIO_VOLUME_WEB
    void SetAudioVolumeCallbacks(std::function<int()> getter,
                                 std::function<bool(int)> setter);

    // DP-HA-009
    void SetHomeAssistantStatusCallback(
        std::function<bool(CareWebHomeAssistantStatus&)> callback);

    // DP-HA-010A
    void SetHomeAssistantEntitiesCallback(
        std::function<bool(std::vector<CareWebHomeAssistantEntity>&)> callback);

    // DP-HA-010B
    void SetHomeAssistantAllowlistCallbacks(
        std::function<bool(std::vector<std::string>&)> getter,
        std::function<bool(const std::vector<std::string>&)> setter);

    // DP-CF-001 — mensaje familiar hacia CARE.
    void SetFamilyMessageCallback(
        std::function<bool(const std::string&, const std::string&)> callback);

private:
    CareWebServer() = default;

    struct Session {
        bool active = false;
        std::string token;
        std::string csrf;
        TickType_t last_seen = 0;
    };

    static esp_err_t HandleIndex(httpd_req_t* req);
    static esp_err_t HandleAuthState(httpd_req_t* req);
    static esp_err_t HandleAuthSetup(httpd_req_t* req);
    static esp_err_t HandleAuthLogin(httpd_req_t* req);
    static esp_err_t HandleAuthLogout(httpd_req_t* req);
    static esp_err_t HandleAuthPassword(httpd_req_t* req);
    static esp_err_t HandleStatus(httpd_req_t* req);
    static esp_err_t HandleFamilyMessage(httpd_req_t* req);
    static esp_err_t HandleHomeAssistantStatus(httpd_req_t* req);
    static esp_err_t HandleHomeAssistantEntities(httpd_req_t* req);
    static esp_err_t HandleHomeAssistantAllowedGet(httpd_req_t* req);
    static esp_err_t HandleHomeAssistantAllowedPut(httpd_req_t* req);
    static esp_err_t HandleHomeAssistantNamesGet(httpd_req_t* req);
    static esp_err_t HandleHomeAssistantNamesPut(httpd_req_t* req);
    static esp_err_t HandleMaintenanceStatus(httpd_req_t* req);
    static esp_err_t HandleMaintenanceAudioVolume(httpd_req_t* req);
    static esp_err_t HandleMaintenanceNewsSettings(httpd_req_t* req);
    static esp_err_t HandleMaintenanceRadioStationsGet(httpd_req_t* req);
    static esp_err_t HandleMaintenanceRadioStationsPut(httpd_req_t* req);
    static esp_err_t HandleMaintenanceListeningProfile(httpd_req_t* req);
    static esp_err_t HandleMaintenanceClearExecutions(httpd_req_t* req);
    static esp_err_t HandleMaintenanceClearLegacyPillbox(httpd_req_t* req);

    static esp_err_t HandleVoiceRecordingsList(httpd_req_t* req);
    static esp_err_t HandleVoiceRecordingsCreate(httpd_req_t* req);
    static esp_err_t HandleVoiceRecordingUpdate(httpd_req_t* req);
    static esp_err_t HandleVoiceRecordingAudio(httpd_req_t* req);
    static esp_err_t HandleVoiceRecordingDelete(httpd_req_t* req);

    static esp_err_t HandleProfileGet(httpd_req_t* req);
    static esp_err_t HandleProfilePut(httpd_req_t* req);

    static esp_err_t HandlePeopleList(httpd_req_t* req);
    static esp_err_t HandlePeopleCreate(httpd_req_t* req);
    static esp_err_t HandlePersonGet(httpd_req_t* req);
    static esp_err_t HandlePersonUpdate(httpd_req_t* req);
    static esp_err_t HandlePersonDelete(httpd_req_t* req);

    static esp_err_t HandlePreferencesList(httpd_req_t* req);
    static esp_err_t HandlePreferencesCreate(httpd_req_t* req);
    static esp_err_t HandlePreferenceGet(httpd_req_t* req);
    static esp_err_t HandlePreferenceUpdate(httpd_req_t* req);
    static esp_err_t HandlePreferenceDelete(httpd_req_t* req);

    static esp_err_t HandlePillboxList(httpd_req_t* req);
    static esp_err_t HandlePillboxCreate(httpd_req_t* req);
    static esp_err_t HandlePillboxGet(httpd_req_t* req);
    static esp_err_t HandlePillboxUpdate(httpd_req_t* req);
    static esp_err_t HandlePillboxDelete(httpd_req_t* req);
    static esp_err_t HandleBloodPressureList(httpd_req_t* req);
    static esp_err_t HandleBloodPressureCreate(httpd_req_t* req);
    static esp_err_t HandleBloodPressureGet(httpd_req_t* req);
    static esp_err_t HandleBloodPressureUpdate(httpd_req_t* req);
    static esp_err_t HandleBloodPressureDelete(httpd_req_t* req);



    static esp_err_t HandleRoutinesList(httpd_req_t* req);
    static esp_err_t HandleRoutinesCreate(httpd_req_t* req);
    static esp_err_t HandleRoutineGet(httpd_req_t* req);
    static esp_err_t HandleRoutineUpdate(httpd_req_t* req);
    static esp_err_t HandleRoutineDelete(httpd_req_t* req);

    static esp_err_t HandleFamilyList(httpd_req_t* req);
    static esp_err_t HandleFamilyCreate(httpd_req_t* req);
    static esp_err_t HandleFamilyGet(httpd_req_t* req);
    static esp_err_t HandleFamilyUpdate(httpd_req_t* req);
    static esp_err_t HandleFamilyDelete(httpd_req_t* req);

    static esp_err_t HandleRemindersList(httpd_req_t* req);
    static esp_err_t HandleRemindersCreate(httpd_req_t* req);
    static esp_err_t HandleReminderGet(httpd_req_t* req);
    static esp_err_t HandleReminderUpdate(httpd_req_t* req);
    static esp_err_t HandleReminderDelete(httpd_req_t* req);

    bool Authorize(httpd_req_t* req, bool require_csrf);
    bool FindSessionForRequest(httpd_req_t* req, size_t& index, bool touch);
    bool ExtractSessionToken(httpd_req_t* req, std::string& token) const;
    bool CheckCsrf(httpd_req_t* req, const Session& session) const;
    void PurgeExpiredSessionsLocked(TickType_t now);
    void InvalidateAllSessions();
    void InvalidateSessionToken(const std::string& token);
    void CreateSession(std::string& token_out, std::string& csrf_out);
    static std::string BuildSessionCookie(const std::string& token);
    static const char* ClearSessionCookieValue();

    bool LoginLocked(uint32_t& retry_seconds);
    void RecordFailedLogin();
    void ResetFailedLogins();

    // DP039_AUDIO_VOLUME_WEB
    std::function<int()> audio_volume_getter_;
    std::function<bool(int)> audio_volume_setter_;

    // DP-HA-009
    std::function<bool(CareWebHomeAssistantStatus&)>
        home_assistant_status_callback_;

    // DP-HA-010A
    std::function<bool(std::vector<CareWebHomeAssistantEntity>&)>
        home_assistant_entities_callback_;

    // DP-HA-010B
    std::function<bool(std::vector<std::string>&)>
        home_assistant_allowlist_getter_;
    std::function<bool(const std::vector<std::string>&)>
        home_assistant_allowlist_setter_;

    std::function<bool(const std::string&, const std::string&)>
        family_message_callback_;

    httpd_handle_t server_ = nullptr;
    mutable std::mutex session_mutex_;
    std::array<Session, 4> sessions_{};
    uint32_t failed_login_attempts_ = 0;
    TickType_t lockout_until_ = 0;
};

}  // namespace xiaozhi_care
