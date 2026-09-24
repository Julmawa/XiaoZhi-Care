#include "care_web_server.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <cJSON.h>
#include <esp_log.h>
#include <esp_random.h>
#include <freertos/task.h>

#include "care_manager.h"
#include "care_mcp_service.h"
#include "care_models.h"
#include "care_web_page.h"
#include "care_voice/reminder_voice_runtime.h"

namespace xiaozhi_care {
namespace {

constexpr char kTag[] = "CARE_WEB";
constexpr uint16_t kServerPort = 8080;
constexpr uint16_t kControlPort = 32769;
constexpr size_t kMaxRequestBody = 65536;  // DP-018: JSON + base64 OGG up to 32 KB
constexpr char kApiPeoplePrefix[] = "/api/people/";
constexpr char kSessionCookieName[] = "care_session";
constexpr uint32_t kSessionTimeoutSeconds = 30U * 60U;
constexpr uint32_t kLoginLockoutSeconds = 60U;
constexpr uint32_t kMaxFailedLogins = 5;
constexpr bool kCareWebAuthEnabled = false;  // Development mode: disables web password/session checks.

TickType_t SecondsToTicks(uint32_t seconds) {
    return pdMS_TO_TICKS(seconds * 1000U);
}

bool TickDeadlinePending(TickType_t now, TickType_t deadline) {
    if (deadline == 0) return false;
    return static_cast<int32_t>(deadline - now) > 0;
}

void SetCommonHeaders(httpd_req_t* req) {
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
    httpd_resp_set_hdr(req, "Permissions-Policy", "camera=(), microphone=(), geolocation=()");
    httpd_resp_set_hdr(
        req, "Content-Security-Policy",
        "default-src 'self'; connect-src 'self'; img-src 'self' data:; "
        "style-src 'unsafe-inline'; script-src 'unsafe-inline'; object-src 'none'; "
        "frame-ancestors 'none'; base-uri 'none'; form-action 'self'");
}

void SetJson(httpd_req_t* req) {
    SetCommonHeaders(req);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
}

esp_err_t SendJsonText(httpd_req_t* req, const char* status, const char* body) {
    SetJson(req);
    if (status != nullptr) {
        httpd_resp_set_status(req, status);
    }
    return httpd_resp_sendstr(req, body);
}

esp_err_t SendNamedError(httpd_req_t* req, const char* status, const char* error) {
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) {
        return SendJsonText(req, "500 Internal Server Error",
                            R"({"success":false,"error":"INTERNAL_ERROR"})");
    }
    cJSON_AddBoolToObject(root, "success", false);
    cJSON_AddStringToObject(root, "error", error);
    char* raw = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (raw == nullptr) {
        return SendJsonText(req, "500 Internal Server Error",
                            R"({"success":false,"error":"INTERNAL_ERROR"})");
    }
    SetJson(req);
    httpd_resp_set_status(req, status);
    esp_err_t result = httpd_resp_sendstr(req, raw);
    cJSON_free(raw);
    return result;
}

esp_err_t SendError(httpd_req_t* req, const char* status, CareError error) {
    return SendNamedError(req, status, CareErrorToString(error));
}

const char* HttpStatusFor(CareError error) {
    switch (error) {
        case CareError::INVALID_ARGUMENT:
        case CareError::INVALID_ID:
        case CareError::INVALID_DATE:
        case CareError::INVALID_TIME:
        case CareError::TOO_LONG:
        case CareError::TOO_LARGE:
            return "400 Bad Request";
        case CareError::NOT_FOUND:
            return "404 Not Found";
        case CareError::IN_USE:
            return "409 Conflict";
        case CareError::LIMIT_REACHED:
        case CareError::STORAGE_FULL:
            return "507 Insufficient Storage";
        case CareError::NOT_INITIALIZED:
            return "503 Service Unavailable";
        default:
            return "500 Internal Server Error";
    }
}

esp_err_t ReadBody(httpd_req_t* req, std::string& body) {
    body.clear();
    if (req->content_len <= 0 || static_cast<size_t>(req->content_len) > kMaxRequestBody) {
        return ESP_ERR_INVALID_SIZE;
    }
    body.resize(static_cast<size_t>(req->content_len));
    size_t received = 0;
    while (received < body.size()) {
        int ret = httpd_req_recv(req, body.data() + received, body.size() - received);
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (ret <= 0) {
            body.clear();
            return ESP_FAIL;
        }
        received += static_cast<size_t>(ret);
    }
    return ESP_OK;
}

std::string ReadString(const cJSON* root, const char* name) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, name);
    return cJSON_IsString(item) && item->valuestring != nullptr ? item->valuestring : std::string();
}

bool ParseOneString(const std::string& body, const char* field, std::string& value) {
    value.clear();
    cJSON* root = cJSON_ParseWithLength(body.data(), body.size());
    if (root == nullptr || !cJSON_IsObject(root)) {
        if (root != nullptr) cJSON_Delete(root);
        return false;
    }
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, field);
    if (!cJSON_IsString(item) || item->valuestring == nullptr) {
        cJSON_Delete(root);
        return false;
    }
    value = item->valuestring;
    cJSON_Delete(root);
    return true;
}

bool ParseTwoStrings(const std::string& body, const char* first_name, std::string& first,
                     const char* second_name, std::string& second) {
    first.clear();
    second.clear();
    cJSON* root = cJSON_ParseWithLength(body.data(), body.size());
    if (root == nullptr || !cJSON_IsObject(root)) {
        if (root != nullptr) cJSON_Delete(root);
        return false;
    }
    const cJSON* first_item = cJSON_GetObjectItemCaseSensitive(root, first_name);
    const cJSON* second_item = cJSON_GetObjectItemCaseSensitive(root, second_name);
    if (!cJSON_IsString(first_item) || first_item->valuestring == nullptr ||
        !cJSON_IsString(second_item) || second_item->valuestring == nullptr) {
        cJSON_Delete(root);
        return false;
    }
    first = first_item->valuestring;
    second = second_item->valuestring;
    cJSON_Delete(root);
    return true;
}

CareError ParsePersonPayload(const std::string& body, CarePerson& person) {
    cJSON* root = cJSON_ParseWithLength(body.data(), body.size());
    if (root == nullptr || !cJSON_IsObject(root)) {
        if (root != nullptr) cJSON_Delete(root);
        return CareError::INVALID_ARGUMENT;
    }

    const cJSON* name = cJSON_GetObjectItemCaseSensitive(root, "name");
    if (!cJSON_IsString(name) || name->valuestring == nullptr || name->valuestring[0] == '\0') {
        cJSON_Delete(root);
        return CareError::INVALID_ARGUMENT;
    }

    person.name = name->valuestring;
    person.nickname = ReadString(root, "nickname");
    person.relationship = ReadString(root, "relationship");
    person.phone = ReadString(root, "phone");
    person.address = ReadString(root, "address");
    person.birthday = ReadString(root, "birthday");
    person.has_pet = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "has_pet"));
    person.pet_type = ReadString(root, "pet_type");
    person.pet_name = ReadString(root, "pet_name");
    person.notes = ReadString(root, "notes");

    const cJSON* enabled = cJSON_GetObjectItemCaseSensitive(root, "enabled");
    person.enabled = !cJSON_IsBool(enabled) || cJSON_IsTrue(enabled);

    person.aliases.clear();
    const cJSON* aliases = cJSON_GetObjectItemCaseSensitive(root, "aliases");
    if (aliases != nullptr) {
        if (!cJSON_IsArray(aliases)) {
            cJSON_Delete(root);
            return CareError::INVALID_ARGUMENT;
        }
        const cJSON* alias = nullptr;
        cJSON_ArrayForEach(alias, aliases) {
            if (!cJSON_IsString(alias) || alias->valuestring == nullptr) {
                cJSON_Delete(root);
                return CareError::INVALID_ARGUMENT;
            }
            person.aliases.emplace_back(alias->valuestring);
        }
    }

    cJSON_Delete(root);
    return CareError::OK;
}

cJSON* PersonToJson(const CarePerson& person) {
    cJSON* item = cJSON_CreateObject();
    if (item == nullptr) return nullptr;
    cJSON_AddStringToObject(item, "id", person.id.c_str());
    cJSON_AddStringToObject(item, "name", person.name.c_str());
    cJSON_AddStringToObject(item, "nickname", person.nickname.c_str());
    cJSON_AddStringToObject(item, "relationship", person.relationship.c_str());
    cJSON_AddStringToObject(item, "phone", person.phone.c_str());
    cJSON_AddStringToObject(item, "address", person.address.c_str());
    cJSON_AddStringToObject(item, "birthday", person.birthday.c_str());
    cJSON_AddBoolToObject(item, "has_pet", person.has_pet);
    cJSON_AddStringToObject(item, "pet_type", person.pet_type.c_str());
    cJSON_AddStringToObject(item, "pet_name", person.pet_name.c_str());
    cJSON* aliases = cJSON_AddArrayToObject(item, "aliases");
    if (aliases == nullptr) {
        cJSON_Delete(item);
        return nullptr;
    }
    for (const auto& alias : person.aliases) {
        cJSON* value = cJSON_CreateString(alias.c_str());
        if (value == nullptr) {
            cJSON_Delete(item);
            return nullptr;
        }
        cJSON_AddItemToArray(aliases, value);
    }
    cJSON_AddStringToObject(item, "notes", person.notes.c_str());
    cJSON_AddBoolToObject(item, "enabled", person.enabled);
    return item;
}

esp_err_t SendRoot(httpd_req_t* req, cJSON* root, const char* status = nullptr) {
    if (root == nullptr) {
        return SendJsonText(req, "500 Internal Server Error",
                            R"({"success":false,"error":"INTERNAL_ERROR"})");
    }
    char* raw = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (raw == nullptr) {
        return SendJsonText(req, "500 Internal Server Error",
                            R"({"success":false,"error":"INTERNAL_ERROR"})");
    }
    SetJson(req);
    if (status != nullptr) httpd_resp_set_status(req, status);
    esp_err_t result = httpd_resp_sendstr(req, raw);
    cJSON_free(raw);
    return result;
}

bool ExtractPersonId(httpd_req_t* req, std::string& id) {
    id.clear();
    const char* uri = req->uri;
    const size_t prefix_len = std::strlen(kApiPeoplePrefix);
    if (std::strncmp(uri, kApiPeoplePrefix, prefix_len) != 0) return false;
    const char* start = uri + prefix_len;
    if (*start == '\0') return false;
    const char* end = std::strchr(start, '?');
    id.assign(start, end == nullptr ? std::strlen(start) : static_cast<size_t>(end - start));
    return id.find('/') == std::string::npos;
}

esp_err_t Register(httpd_handle_t server, const char* uri, httpd_method_t method,
                   esp_err_t (*handler)(httpd_req_t*)) {
    httpd_uri_t entry{};
    entry.uri = uri;
    entry.method = method;
    entry.handler = handler;
    entry.user_ctx = nullptr;
    return httpd_register_uri_handler(server, &entry);
}

std::string RandomHex(size_t bytes) {
    std::vector<uint8_t> random(bytes);
    esp_fill_random(random.data(), random.size());
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.resize(bytes * 2);
    for (size_t i = 0; i < bytes; ++i) {
        out[i * 2] = kHex[(random[i] >> 4) & 0x0F];
        out[i * 2 + 1] = kHex[random[i] & 0x0F];
    }
    return out;
}

bool ConstantTimeStringEqual(const std::string& left, const std::string& right) {
    const size_t max_len = std::max(left.size(), right.size());
    uint8_t diff = static_cast<uint8_t>(left.size() ^ right.size());
    for (size_t i = 0; i < max_len; ++i) {
        const uint8_t a = i < left.size() ? static_cast<uint8_t>(left[i]) : 0;
        const uint8_t b = i < right.size() ? static_cast<uint8_t>(right[i]) : 0;
        diff |= static_cast<uint8_t>(a ^ b);
    }
    return diff == 0;
}

}  // namespace

CareWebServer& CareWebServer::GetInstance() {
    static CareWebServer instance;
    return instance;
}

bool CareWebServer::IsRunning() const {
    return server_ != nullptr;
}

esp_err_t CareWebServer::Start() {
    if (server_ != nullptr) {
        return ESP_OK;
    }
    if (!CareManager::GetInstance().IsReady()) {
        ESP_LOGE(kTag, "Care core is not ready; web server not started");
        return ESP_ERR_INVALID_STATE;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = kServerPort;
    config.ctrl_port = kControlPort;
    config.stack_size = 7168;
    config.max_uri_handlers = 48;

    // CARE_WEB_SOCKET_BUDGET
    // Preserve LWIP sockets for MQTT, UDP audio and transient network work.
    config.max_open_sockets = 3;
    config.lru_purge_enable = true;

    config.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t err = httpd_start(&server_, &config);
    if (err != ESP_OK) {
        server_ = nullptr;
        ESP_LOGE(kTag, "HTTP server start failed: %s", esp_err_to_name(err));
        return err;
    }

    const struct {
        const char* uri;
        httpd_method_t method;
        esp_err_t (*handler)(httpd_req_t*);
    } routes[] = {
        {"/", HTTP_GET, HandleIndex},
        {"/api/auth/state", HTTP_GET, HandleAuthState},
        {"/api/auth/setup", HTTP_POST, HandleAuthSetup},
        {"/api/auth/login", HTTP_POST, HandleAuthLogin},
        {"/api/auth/logout", HTTP_POST, HandleAuthLogout},
        {"/api/auth/password", HTTP_PUT, HandleAuthPassword},
        {"/api/status", HTTP_GET, HandleStatus},
        {"/api/maintenance/status", HTTP_GET, HandleMaintenanceStatus},
        {"/api/maintenance/clear-executions", HTTP_POST, HandleMaintenanceClearExecutions},
        {"/api/maintenance/clear-legacy-pillbox", HTTP_POST, HandleMaintenanceClearLegacyPillbox},
        {"/api/voice-recordings", HTTP_GET, HandleVoiceRecordingsList},
        {"/api/voice-recordings", HTTP_POST, HandleVoiceRecordingsCreate},
        {"/api/voice-recordings/*", HTTP_GET, HandleVoiceRecordingAudio},
        {"/api/voice-recordings/*", HTTP_PUT, HandleVoiceRecordingUpdate},
        {"/api/voice-recordings/*", HTTP_DELETE, HandleVoiceRecordingDelete},
        {"/api/profile", HTTP_GET, HandleProfileGet},
        {"/api/profile", HTTP_PUT, HandleProfilePut},
        {"/api/people", HTTP_GET, HandlePeopleList},
        {"/api/people", HTTP_POST, HandlePeopleCreate},
        {"/api/people/*", HTTP_GET, HandlePersonGet},
        {"/api/people/*", HTTP_PUT, HandlePersonUpdate},
        {"/api/people/*", HTTP_DELETE, HandlePersonDelete},
        {"/api/preferences", HTTP_GET, HandlePreferencesList},
        {"/api/preferences", HTTP_POST, HandlePreferencesCreate},
        {"/api/preferences/*", HTTP_GET, HandlePreferenceGet},
        {"/api/preferences/*", HTTP_PUT, HandlePreferenceUpdate},
        {"/api/preferences/*", HTTP_DELETE, HandlePreferenceDelete},
        {"/api/pillbox", HTTP_GET, HandlePillboxList},
        {"/api/pillbox", HTTP_POST, HandlePillboxCreate},
        {"/api/pillbox/*", HTTP_GET, HandlePillboxGet},
        {"/api/pillbox/*", HTTP_PUT, HandlePillboxUpdate},
        {"/api/pillbox/*", HTTP_DELETE, HandlePillboxDelete},
        {"/api/routines", HTTP_GET, HandleRoutinesList},
        {"/api/routines", HTTP_POST, HandleRoutinesCreate},
        {"/api/routines/*", HTTP_GET, HandleRoutineGet},
        {"/api/routines/*", HTTP_PUT, HandleRoutineUpdate},
        {"/api/routines/*", HTTP_DELETE, HandleRoutineDelete},
        {"/api/family", HTTP_GET, HandleFamilyList},
        {"/api/family", HTTP_POST, HandleFamilyCreate},
        {"/api/family/*", HTTP_GET, HandleFamilyGet},
        {"/api/family/*", HTTP_PUT, HandleFamilyUpdate},
        {"/api/family/*", HTTP_DELETE, HandleFamilyDelete},
        {"/api/reminders", HTTP_GET, HandleRemindersList},
        {"/api/reminders", HTTP_POST, HandleRemindersCreate},
        {"/api/reminders/*", HTTP_GET, HandleReminderGet},
        {"/api/reminders/*", HTTP_PUT, HandleReminderUpdate},
        {"/api/reminders/*", HTTP_DELETE, HandleReminderDelete},
    };

    for (const auto& route : routes) {
        err = Register(server_, route.uri, route.method, route.handler);
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "Unable to register %s: %s", route.uri, esp_err_to_name(err));
            httpd_stop(server_);
            server_ = nullptr;
            return err;
        }
    }

    ESP_LOGI(kTag, "XiaoZhi Care web panel started on port %u", kServerPort);
    ESP_LOGI(kTag, "Open http://<ESP32-IP>:%u/", kServerPort);
    if constexpr (kCareWebAuthEnabled) {
        bool configured = false;
        CareError auth_state = CareManager::GetInstance().IsAdminPasswordConfigured(configured);
        if (auth_state == CareError::OK && configured) {
            ESP_LOGI(kTag, "Administrator authentication is enabled");
        } else if (auth_state == CareError::OK) {
            ESP_LOGW(kTag, "Administrator password not configured yet; first browser must create it");
        } else {
            ESP_LOGE(kTag, "Unable to read authentication state: %s", CareErrorToString(auth_state));
        }
    } else {
        ESP_LOGW(kTag, "Administrator authentication is DISABLED for development/testing");
    }
    ESP_LOGW(kTag, "HTTP is not encrypted; keep port 8080 restricted to a trusted local network");

    // DP-018-r2: start the independent CareReminder audio evaluator only after
    // care-core and care-voice are known to be ready. Failure here must never
    // take down the already-working web panel.
    if (!voice::ReminderVoiceRuntime::GetInstance().Start()) {
        ESP_LOGW(kTag, "Reminder audio runtime could not start; web panel remains available");
    }

    return ESP_OK;
}

esp_err_t CareWebServer::Stop() {
    if (server_ == nullptr) {
        return ESP_OK;
    }
    InvalidateAllSessions();
    httpd_handle_t server = server_;
    server_ = nullptr;
    return httpd_stop(server);
}

void CareWebServer::PurgeExpiredSessionsLocked(TickType_t now) {
    const TickType_t timeout = SecondsToTicks(kSessionTimeoutSeconds);
    for (auto& session : sessions_) {
        if (session.active && static_cast<TickType_t>(now - session.last_seen) > timeout) {
            session = Session{};
        }
    }
}

bool CareWebServer::ExtractSessionToken(httpd_req_t* req, std::string& token) const {
    token.clear();
    size_t length = httpd_req_get_hdr_value_len(req, "Cookie");
    if (length == 0 || length > 512) {
        return false;
    }
    std::vector<char> buffer(length + 1);
    if (httpd_req_get_hdr_value_str(req, "Cookie", buffer.data(), buffer.size()) != ESP_OK) {
        return false;
    }

    const std::string cookies(buffer.data());
    const std::string marker = std::string(kSessionCookieName) + "=";
    size_t pos = 0;
    while (pos < cookies.size()) {
        while (pos < cookies.size() && (cookies[pos] == ' ' || cookies[pos] == ';')) ++pos;
        if (cookies.compare(pos, marker.size(), marker) == 0) {
            const size_t value_start = pos + marker.size();
            size_t value_end = cookies.find(';', value_start);
            if (value_end == std::string::npos) value_end = cookies.size();
            token = cookies.substr(value_start, value_end - value_start);
            return !token.empty();
        }
        size_t next = cookies.find(';', pos);
        if (next == std::string::npos) break;
        pos = next + 1;
    }
    return false;
}

bool CareWebServer::CheckCsrf(httpd_req_t* req, const Session& session) const {
    size_t length = httpd_req_get_hdr_value_len(req, "X-Care-CSRF");
    if (length == 0 || length > 128) {
        return false;
    }
    std::vector<char> buffer(length + 1);
    if (httpd_req_get_hdr_value_str(req, "X-Care-CSRF", buffer.data(), buffer.size()) != ESP_OK) {
        return false;
    }
    return ConstantTimeStringEqual(session.csrf, std::string(buffer.data()));
}

bool CareWebServer::FindSessionForRequest(httpd_req_t* req, size_t& index, bool touch) {
    std::string token;
    if (!ExtractSessionToken(req, token)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(session_mutex_);
    const TickType_t now = xTaskGetTickCount();
    PurgeExpiredSessionsLocked(now);
    for (size_t i = 0; i < sessions_.size(); ++i) {
        if (sessions_[i].active && ConstantTimeStringEqual(sessions_[i].token, token)) {
            if (touch) sessions_[i].last_seen = now;
            index = i;
            return true;
        }
    }
    return false;
}

bool CareWebServer::Authorize(httpd_req_t* req, bool require_csrf) {
    (void)req;
    (void)require_csrf;
    if constexpr (!kCareWebAuthEnabled) {
        return true;
    }

    size_t index = 0;
    if (!FindSessionForRequest(req, index, true)) {
        SendNamedError(req, "401 Unauthorized", "AUTH_REQUIRED");
        return false;
    }

    if (require_csrf) {
        std::lock_guard<std::mutex> lock(session_mutex_);
        if (index >= sessions_.size() || !sessions_[index].active ||
            !CheckCsrf(req, sessions_[index])) {
            SendNamedError(req, "403 Forbidden", "CSRF_INVALID");
            return false;
        }
    }
    return true;
}

std::string CareWebServer::BuildSessionCookie(const std::string& token) {
    return std::string(kSessionCookieName) + "=" + token +
           "; Path=/; HttpOnly; SameSite=Strict; Max-Age=" +
           std::to_string(kSessionTimeoutSeconds);
}

const char* CareWebServer::ClearSessionCookieValue() {
    return "care_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0";
}

void CareWebServer::CreateSession(std::string& token_out, std::string& csrf_out) {
    const std::string token = RandomHex(24);  // 192-bit session token.
    const std::string csrf = RandomHex(16);   // 128-bit CSRF token.
    const TickType_t now = xTaskGetTickCount();

    {
        std::lock_guard<std::mutex> lock(session_mutex_);
        PurgeExpiredSessionsLocked(now);

        size_t selected = sessions_.size();
        for (size_t i = 0; i < sessions_.size(); ++i) {
            if (!sessions_[i].active) {
                selected = i;
                break;
            }
        }
        if (selected == sessions_.size()) {
            selected = 0;
            for (size_t i = 1; i < sessions_.size(); ++i) {
                if (static_cast<TickType_t>(now - sessions_[i].last_seen) >
                    static_cast<TickType_t>(now - sessions_[selected].last_seen)) {
                    selected = i;
                }
            }
        }

        sessions_[selected].active = true;
        sessions_[selected].token = token;
        sessions_[selected].csrf = csrf;
        sessions_[selected].last_seen = now;
    }

    token_out = token;
    csrf_out = csrf;
}

void CareWebServer::InvalidateAllSessions() {
    std::lock_guard<std::mutex> lock(session_mutex_);
    for (auto& session : sessions_) session = Session{};
}

void CareWebServer::InvalidateSessionToken(const std::string& token) {
    std::lock_guard<std::mutex> lock(session_mutex_);
    for (auto& session : sessions_) {
        if (session.active && ConstantTimeStringEqual(session.token, token)) {
            session = Session{};
            return;
        }
    }
}

bool CareWebServer::LoginLocked(uint32_t& retry_seconds) {
    std::lock_guard<std::mutex> lock(session_mutex_);
    const TickType_t now = xTaskGetTickCount();
    if (!TickDeadlinePending(now, lockout_until_)) {
        lockout_until_ = 0;
        retry_seconds = 0;
        return false;
    }
    const TickType_t remaining = static_cast<TickType_t>(lockout_until_ - now);
    retry_seconds = static_cast<uint32_t>((remaining * portTICK_PERIOD_MS + 999U) / 1000U);
    if (retry_seconds == 0) retry_seconds = 1;
    return true;
}

void CareWebServer::RecordFailedLogin() {
    std::lock_guard<std::mutex> lock(session_mutex_);
    ++failed_login_attempts_;
    if (failed_login_attempts_ >= kMaxFailedLogins) {
        failed_login_attempts_ = 0;
        lockout_until_ = xTaskGetTickCount() + SecondsToTicks(kLoginLockoutSeconds);
        ESP_LOGW(kTag, "Too many failed administrator logins; temporary lockout enabled");
    }
}

void CareWebServer::ResetFailedLogins() {
    std::lock_guard<std::mutex> lock(session_mutex_);
    failed_login_attempts_ = 0;
    lockout_until_ = 0;
}

esp_err_t CareWebServer::HandleIndex(httpd_req_t* req) {
    SetCommonHeaders(req);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, kCareWebPage, HTTPD_RESP_USE_STRLEN);
}

esp_err_t CareWebServer::HandleAuthState(httpd_req_t* req) {
    if constexpr (!kCareWebAuthEnabled) {
        cJSON* root = cJSON_CreateObject();
        cJSON* data = cJSON_CreateObject();
        if (root == nullptr || data == nullptr) {
            if (root) cJSON_Delete(root);
            if (data) cJSON_Delete(data);
            return SendNamedError(req, "500 Internal Server Error", "INTERNAL_ERROR");
        }
        cJSON_AddBoolToObject(root, "success", true);
        cJSON_AddStringToObject(data, "version", "0.4.0-alpha.9-dev-no-auth");
        cJSON_AddBoolToObject(data, "configured", true);
        cJSON_AddBoolToObject(data, "authenticated", true);
        cJSON_AddBoolToObject(data, "authentication", false);
        cJSON_AddNumberToObject(data, "session_timeout_minutes", 0);
        cJSON_AddStringToObject(data, "csrf", "dev-no-auth");
        cJSON_AddItemToObject(root, "data", data);
        return SendRoot(req, root);
    }

    bool configured = false;
    CareError state = CareManager::GetInstance().IsAdminPasswordConfigured(configured);
    if (state != CareError::OK) {
        return SendError(req, HttpStatusFor(state), state);
    }

    size_t session_index = 0;
    bool authenticated = configured && GetInstance().FindSessionForRequest(req, session_index, true);
    std::string csrf;
    if (authenticated) {
        std::lock_guard<std::mutex> lock(GetInstance().session_mutex_);
        if (session_index < GetInstance().sessions_.size() && GetInstance().sessions_[session_index].active) {
            csrf = GetInstance().sessions_[session_index].csrf;
        } else {
            authenticated = false;
        }
    }

    cJSON* root = cJSON_CreateObject();
    cJSON* data = cJSON_CreateObject();
    if (root == nullptr || data == nullptr) {
        if (root) cJSON_Delete(root);
        if (data) cJSON_Delete(data);
        return SendNamedError(req, "500 Internal Server Error", "INTERNAL_ERROR");
    }
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddStringToObject(data, "version", "0.4.0-alpha.11.5");
    cJSON_AddBoolToObject(data, "configured", configured);
    cJSON_AddBoolToObject(data, "authenticated", authenticated);
    cJSON_AddBoolToObject(data, "authentication", kCareWebAuthEnabled);
    cJSON_AddNumberToObject(data, "session_timeout_minutes", kSessionTimeoutSeconds / 60U);
    if (authenticated) cJSON_AddStringToObject(data, "csrf", csrf.c_str());
    cJSON_AddItemToObject(root, "data", data);
    return SendRoot(req, root);
}

esp_err_t CareWebServer::HandleAuthSetup(httpd_req_t* req) {
    bool configured = false;
    CareError state = CareManager::GetInstance().IsAdminPasswordConfigured(configured);
    if (state != CareError::OK) {
        return SendError(req, HttpStatusFor(state), state);
    }
    if (configured) {
        return SendNamedError(req, "409 Conflict", "AUTH_ALREADY_CONFIGURED");
    }

    std::string body;
    std::string password;
    if (ReadBody(req, body) != ESP_OK || !ParseOneString(body, "password", password)) {
        return SendNamedError(req, "400 Bad Request", "INVALID_PASSWORD");
    }

    CareError result = CareManager::GetInstance().SetAdminPassword(password);
    password.clear();
    if (result != CareError::OK) {
        return SendNamedError(req, HttpStatusFor(result),
                              result == CareError::INVALID_ARGUMENT ? "INVALID_PASSWORD"
                                                                    : CareErrorToString(result));
    }

    auto& web = GetInstance();
    web.InvalidateAllSessions();
    web.ResetFailedLogins();
    std::string token;
    std::string csrf;
    web.CreateSession(token, csrf);
    const std::string cookie = BuildSessionCookie(token);
    httpd_resp_set_hdr(req, "Set-Cookie", cookie.c_str());

    cJSON* root = cJSON_CreateObject();
    cJSON* data = cJSON_CreateObject();
    if (root == nullptr || data == nullptr) {
        if (root) cJSON_Delete(root);
        if (data) cJSON_Delete(data);
        return SendNamedError(req, "500 Internal Server Error", "INTERNAL_ERROR");
    }
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddStringToObject(data, "csrf", csrf.c_str());
    cJSON_AddItemToObject(root, "data", data);
    ESP_LOGI(kTag, "Administrator password created; authenticated session opened");
    return SendRoot(req, root, "201 Created");
}

esp_err_t CareWebServer::HandleAuthLogin(httpd_req_t* req) {
    bool configured = false;
    CareError state = CareManager::GetInstance().IsAdminPasswordConfigured(configured);
    if (state != CareError::OK) {
        return SendError(req, HttpStatusFor(state), state);
    }
    if (!configured) {
        return SendNamedError(req, "409 Conflict", "AUTH_NOT_CONFIGURED");
    }

    auto& web = GetInstance();
    uint32_t retry_seconds = 0;
    if (web.LoginLocked(retry_seconds)) {
        std::string retry = std::to_string(retry_seconds);
        httpd_resp_set_hdr(req, "Retry-After", retry.c_str());
        return SendNamedError(req, "429 Too Many Requests", "AUTH_LOCKED");
    }

    std::string body;
    std::string password;
    if (ReadBody(req, body) != ESP_OK || !ParseOneString(body, "password", password)) {
        return SendNamedError(req, "400 Bad Request", "INVALID_REQUEST");
    }

    bool valid = false;
    CareError result = CareManager::GetInstance().VerifyAdminPassword(password, valid);
    password.clear();
    if (result != CareError::OK) {
        return SendError(req, HttpStatusFor(result), result);
    }
    if (!valid) {
        web.RecordFailedLogin();
        ESP_LOGW(kTag, "Failed administrator login");
        return SendNamedError(req, "401 Unauthorized", "AUTH_FAILED");
    }

    web.ResetFailedLogins();
    std::string token;
    std::string csrf;
    web.CreateSession(token, csrf);
    const std::string cookie = BuildSessionCookie(token);
    httpd_resp_set_hdr(req, "Set-Cookie", cookie.c_str());

    cJSON* root = cJSON_CreateObject();
    cJSON* data = cJSON_CreateObject();
    if (root == nullptr || data == nullptr) {
        if (root) cJSON_Delete(root);
        if (data) cJSON_Delete(data);
        return SendNamedError(req, "500 Internal Server Error", "INTERNAL_ERROR");
    }
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddStringToObject(data, "csrf", csrf.c_str());
    cJSON_AddItemToObject(root, "data", data);
    ESP_LOGI(kTag, "Administrator login succeeded");
    return SendRoot(req, root);
}

esp_err_t CareWebServer::HandleAuthLogout(httpd_req_t* req) {
    auto& web = GetInstance();
    if (!web.Authorize(req, true)) {
        return ESP_OK;
    }

    std::string token;
    if (web.ExtractSessionToken(req, token)) {
        web.InvalidateSessionToken(token);
    }
    httpd_resp_set_hdr(req, "Set-Cookie", ClearSessionCookieValue());
    ESP_LOGI(kTag, "Administrator session closed");
    return SendJsonText(req, "200 OK", R"({"success":true})");
}

esp_err_t CareWebServer::HandleAuthPassword(httpd_req_t* req) {
    auto& web = GetInstance();
    if (!web.Authorize(req, true)) {
        return ESP_OK;
    }

    std::string body;
    std::string current_password;
    std::string new_password;
    if (ReadBody(req, body) != ESP_OK ||
        !ParseTwoStrings(body, "current_password", current_password,
                         "new_password", new_password)) {
        return SendNamedError(req, "400 Bad Request", "INVALID_REQUEST");
    }

    bool valid = false;
    CareError verify = CareManager::GetInstance().VerifyAdminPassword(current_password, valid);
    current_password.clear();
    if (verify != CareError::OK) {
        return SendError(req, HttpStatusFor(verify), verify);
    }
    if (!valid) {
        new_password.clear();
        return SendNamedError(req, "401 Unauthorized", "AUTH_FAILED");
    }

    CareError save = CareManager::GetInstance().SetAdminPassword(new_password);
    new_password.clear();
    if (save != CareError::OK) {
        return SendNamedError(req, HttpStatusFor(save),
                              save == CareError::INVALID_ARGUMENT ? "INVALID_PASSWORD"
                                                                 : CareErrorToString(save));
    }

    // Password changes revoke every previous session. Create a fresh session
    // for the browser that performed the authenticated change.
    web.InvalidateAllSessions();
    web.ResetFailedLogins();
    std::string token;
    std::string csrf;
    web.CreateSession(token, csrf);
    const std::string cookie = BuildSessionCookie(token);
    httpd_resp_set_hdr(req, "Set-Cookie", cookie.c_str());

    cJSON* root = cJSON_CreateObject();
    cJSON* data = cJSON_CreateObject();
    if (root == nullptr || data == nullptr) {
        if (root) cJSON_Delete(root);
        if (data) cJSON_Delete(data);
        return SendNamedError(req, "500 Internal Server Error", "INTERNAL_ERROR");
    }
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddStringToObject(data, "csrf", csrf.c_str());
    cJSON_AddItemToObject(root, "data", data);
    ESP_LOGI(kTag, "Administrator password changed; previous sessions revoked");
    return SendRoot(req, root);
}

esp_err_t CareWebServer::HandleStatus(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, false)) return ESP_OK;

    cJSON* root = cJSON_CreateObject();
    cJSON* data = cJSON_CreateObject();
    if (root == nullptr || data == nullptr) {
        if (root) cJSON_Delete(root);
        if (data) cJSON_Delete(data);
        return SendJsonText(req, "500 Internal Server Error",
                            R"({"success":false,"error":"INTERNAL_ERROR"})");
    }
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddStringToObject(data, "version", "0.4.0-alpha.11.5");
    cJSON_AddBoolToObject(data, "ready", CareManager::GetInstance().IsReady());
    cJSON_AddNumberToObject(data, "max_people", static_cast<double>(kMaxPeople));
    cJSON_AddNumberToObject(data, "max_preferences", static_cast<double>(kMaxPreferences));
    cJSON_AddNumberToObject(data, "max_pillbox", static_cast<double>(kMaxPillboxEntries));
    cJSON_AddNumberToObject(data, "max_reminders", static_cast<double>(kMaxReminders));
    cJSON_AddNumberToObject(data, "max_routines", 64);
    cJSON_AddNumberToObject(data, "max_voice_recordings", 12);
    cJSON_AddNumberToObject(data, "port", kServerPort);
    cJSON_AddBoolToObject(data, "authentication", kCareWebAuthEnabled);
    cJSON_AddItemToObject(root, "data", data);
    return SendRoot(req, root);
}

esp_err_t CareWebServer::HandlePeopleList(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, false)) return ESP_OK;

    std::vector<CarePerson> people;
    CareError result = CareManager::GetInstance().ListPeople(people);
    if (result != CareError::OK) {
        return SendError(req, HttpStatusFor(result), result);
    }

    cJSON* root = cJSON_CreateObject();
    cJSON* data = cJSON_CreateArray();
    if (root == nullptr || data == nullptr) {
        if (root) cJSON_Delete(root);
        if (data) cJSON_Delete(data);
        return SendError(req, "500 Internal Server Error", CareError::INTERNAL_ERROR);
    }
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddItemToObject(root, "data", data);
    for (const auto& person : people) {
        cJSON* item = PersonToJson(person);
        if (item == nullptr) {
            cJSON_Delete(root);
            return SendError(req, "500 Internal Server Error", CareError::INTERNAL_ERROR);
        }
        cJSON_AddItemToArray(data, item);
    }
    return SendRoot(req, root);
}

esp_err_t CareWebServer::HandlePeopleCreate(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, true)) return ESP_OK;

    std::string body;
    if (ReadBody(req, body) != ESP_OK) {
        return SendError(req, "400 Bad Request", CareError::INVALID_ARGUMENT);
    }
    CarePerson person;
    CareError parsed = ParsePersonPayload(body, person);
    if (parsed != CareError::OK) {
        return SendError(req, HttpStatusFor(parsed), parsed);
    }
    std::string id;
    CareError result = CareManager::GetInstance().AddPerson(std::move(person), id);
    if (result != CareError::OK) {
        return SendError(req, HttpStatusFor(result), result);
    }

    // DP034_R4_CACHE_COHERENCE
    CareMcpService::GetInstance().InvalidatePeopleCache();

    CarePerson saved;
    result = CareManager::GetInstance().GetPerson(id, saved);
    if (result != CareError::OK) {
        return SendError(req, HttpStatusFor(result), result);
    }
    cJSON* root = cJSON_CreateObject();
    cJSON* data = PersonToJson(saved);
    if (root == nullptr || data == nullptr) {
        if (root) cJSON_Delete(root);
        if (data) cJSON_Delete(data);
        return SendError(req, "500 Internal Server Error", CareError::INTERNAL_ERROR);
    }
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddItemToObject(root, "data", data);
    return SendRoot(req, root, "201 Created");
}

esp_err_t CareWebServer::HandlePersonGet(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, false)) return ESP_OK;

    std::string id;
    if (!ExtractPersonId(req, id)) {
        return SendError(req, "400 Bad Request", CareError::INVALID_ID);
    }
    CarePerson person;
    CareError result = CareManager::GetInstance().GetPerson(id, person);
    if (result != CareError::OK) {
        return SendError(req, HttpStatusFor(result), result);
    }
    cJSON* root = cJSON_CreateObject();
    cJSON* data = PersonToJson(person);
    if (root == nullptr || data == nullptr) {
        if (root) cJSON_Delete(root);
        if (data) cJSON_Delete(data);
        return SendError(req, "500 Internal Server Error", CareError::INTERNAL_ERROR);
    }
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddItemToObject(root, "data", data);
    return SendRoot(req, root);
}

esp_err_t CareWebServer::HandlePersonUpdate(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, true)) return ESP_OK;

    std::string id;
    if (!ExtractPersonId(req, id)) {
        return SendError(req, "400 Bad Request", CareError::INVALID_ID);
    }
    std::string body;
    if (ReadBody(req, body) != ESP_OK) {
        return SendError(req, "400 Bad Request", CareError::INVALID_ARGUMENT);
    }
    CarePerson person;
    CareError parsed = ParsePersonPayload(body, person);
    if (parsed != CareError::OK) {
        return SendError(req, HttpStatusFor(parsed), parsed);
    }
    person.id = id;
    CareError result = CareManager::GetInstance().UpdatePerson(person);
    if (result != CareError::OK) {
        return SendError(req, HttpStatusFor(result), result);
    }

    // DP034_R4_CACHE_COHERENCE
    CareMcpService::GetInstance().InvalidatePeopleCache();

    CarePerson saved;
    result = CareManager::GetInstance().GetPerson(id, saved);
    if (result != CareError::OK) {
        return SendError(req, HttpStatusFor(result), result);
    }
    cJSON* root = cJSON_CreateObject();
    cJSON* data = PersonToJson(saved);
    if (root == nullptr || data == nullptr) {
        if (root) cJSON_Delete(root);
        if (data) cJSON_Delete(data);
        return SendError(req, "500 Internal Server Error", CareError::INTERNAL_ERROR);
    }
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddItemToObject(root, "data", data);
    return SendRoot(req, root);
}

esp_err_t CareWebServer::HandlePersonDelete(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, true)) return ESP_OK;

    std::string id;
    if (!ExtractPersonId(req, id)) {
        return SendError(req, "400 Bad Request", CareError::INVALID_ID);
    }
    CareError result = CareManager::GetInstance().DeletePerson(id);
    if (result != CareError::OK) {
        return SendError(req, HttpStatusFor(result), result);
    }

    // Una eliminaciÃ³n tambiÃ©n puede afectar preferencias asociadas.
    CareMcpService::GetInstance().InvalidatePeopleCache();
    CareMcpService::GetInstance().InvalidatePreferencesCache();

    return SendJsonText(req, "200 OK", R"({"success":true})");
}

}  // namespace xiaozhi_care
