#pragma once

#include <array>
#include <cstddef>
#include <mutex>
#include <string>

#include <esp_err.h>
#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>

namespace xiaozhi_care {

class CareWebServer {
public:
    static CareWebServer& GetInstance();

    CareWebServer(const CareWebServer&) = delete;
    CareWebServer& operator=(const CareWebServer&) = delete;

    esp_err_t Start();
    esp_err_t Stop();
    bool IsRunning() const;

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
    static esp_err_t HandleMaintenanceStatus(httpd_req_t* req);
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

    httpd_handle_t server_ = nullptr;
    mutable std::mutex session_mutex_;
    std::array<Session, 4> sessions_{};
    uint32_t failed_login_attempts_ = 0;
    TickType_t lockout_until_ = 0;
};

}  // namespace xiaozhi_care
