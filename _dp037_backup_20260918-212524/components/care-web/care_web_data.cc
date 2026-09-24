#include "care_web_server.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <cJSON.h>
#include <mbedtls/base64.h>
#include <esp_log.h>
#include <nvs_flash.h>

#include "care_manager.h"
#include "care_mcp_service.h"
#include "care_models.h"

#include "care_daily/nvs_routine_repository.h"
#include "care_daily/nvs_routine_execution_repository.h"
#include "care_family/nvs_family_relationship_repository.h"
#include "care_voice/voice_recording_store.h"

namespace xiaozhi_care {
namespace {

constexpr size_t kMaxRequestBody = 65536;  // DP-018: JSON + base64 OGG up to 32 KB
constexpr char kApiPreferencesPrefix[] = "/api/preferences/";
constexpr char kApiPillboxPrefix[] = "/api/pillbox/";
constexpr char kApiRemindersPrefix[] = "/api/reminders/";
constexpr char kApiRoutinesPrefix[] = "/api/routines/";
constexpr char kApiFamilyPrefix[] = "/api/family/";

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
    if (status != nullptr) httpd_resp_set_status(req, status);
    return httpd_resp_sendstr(req, body);
}

esp_err_t SendNamedError(httpd_req_t* req, const char* status, const char* error) {
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) {
        return SendJsonText(req, "500 Internal Server Error",
                            "{\"success\":false,\"error\":\"INTERNAL_ERROR\"}");
    }
    cJSON_AddBoolToObject(root, "success", false);
    cJSON_AddStringToObject(root, "error", error != nullptr ? error : "INTERNAL_ERROR");
    char* raw = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (raw == nullptr) {
        return SendJsonText(req, "500 Internal Server Error",
                            "{\"success\":false,\"error\":\"INTERNAL_ERROR\"}");
    }
    SetJson(req);
    httpd_resp_set_status(req, status);
    esp_err_t result = httpd_resp_sendstr(req, raw);
    cJSON_free(raw);
    return result;
}

esp_err_t SendError(httpd_req_t* req, const char* status, CareError error) {
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) {
        return SendJsonText(req, "500 Internal Server Error",
                            "{\"success\":false,\"error\":\"INTERNAL_ERROR\"}");
    }
    cJSON_AddBoolToObject(root, "success", false);
    cJSON_AddStringToObject(root, "error", CareErrorToString(error));
    char* raw = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (raw == nullptr) {
        return SendJsonText(req, "500 Internal Server Error",
                            "{\"success\":false,\"error\":\"INTERNAL_ERROR\"}");
    }
    SetJson(req);
    httpd_resp_set_status(req, status);
    esp_err_t result = httpd_resp_sendstr(req, raw);
    cJSON_free(raw);
    return result;
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
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (ret <= 0) { body.clear(); return ESP_FAIL; }
        received += static_cast<size_t>(ret);
    }
    return ESP_OK;
}

std::string ReadString(const cJSON* root, const char* name) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, name);
    return cJSON_IsString(item) && item->valuestring != nullptr ? item->valuestring : std::string();
}

int ReadInt(const cJSON* root, const char* name, int fallback) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, name);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

bool ReadBoolField(const cJSON* root, const char* name, bool fallback) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, name);
    return cJSON_IsBool(item) ? cJSON_IsTrue(item) : fallback;
}

bool ReadEnabled(const cJSON* root) {
    const cJSON* enabled = cJSON_GetObjectItemCaseSensitive(root, "enabled");
    return !cJSON_IsBool(enabled) || cJSON_IsTrue(enabled);
}

bool ExtractId(httpd_req_t* req, const char* prefix, std::string& id) {
    id.clear();
    const char* uri = req->uri;
    const size_t prefix_len = std::strlen(prefix);
    if (std::strncmp(uri, prefix, prefix_len) != 0) return false;
    const char* start = uri + prefix_len;
    if (*start == '\0') return false;
    const char* end = std::strchr(start, '?');
    id.assign(start, end == nullptr ? std::strlen(start) : static_cast<size_t>(end - start));
    return id.find('/') == std::string::npos;
}

esp_err_t SendRoot(httpd_req_t* req, cJSON* root, const char* status = nullptr) {
    if (root == nullptr) {
        return SendJsonText(req, "500 Internal Server Error",
                            "{\"success\":false,\"error\":\"INTERNAL_ERROR\"}");
    }
    char* raw = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (raw == nullptr) {
        return SendJsonText(req, "500 Internal Server Error",
                            "{\"success\":false,\"error\":\"INTERNAL_ERROR\"}");
    }
    SetJson(req);
    if (status != nullptr) httpd_resp_set_status(req, status);
    esp_err_t result = httpd_resp_sendstr(req, raw);
    cJSON_free(raw);
    return result;
}


void AddUsageObject(cJSON* parent, const char* name, int used, int max_value) {
    cJSON* item = cJSON_CreateObject();
    if (item == nullptr) return;
    cJSON_AddNumberToObject(item, "used", used);
    cJSON_AddNumberToObject(item, "max", max_value);
    cJSON_AddBoolToObject(item, "near_limit", max_value > 0 && used >= (max_value * 8 / 10));
    cJSON_AddItemToObject(parent, name, item);
}

CareError ParseProfilePayload(const std::string& body, CareProfile& profile) {
    cJSON* root = cJSON_ParseWithLength(body.data(), body.size());
    if (root == nullptr || !cJSON_IsObject(root)) {
        if (root) cJSON_Delete(root);
        return CareError::INVALID_ARGUMENT;
    }
    const cJSON* name = cJSON_GetObjectItemCaseSensitive(root, "name");
    if (!cJSON_IsString(name) || name->valuestring == nullptr || name->valuestring[0] == '\0') {
        cJSON_Delete(root); return CareError::INVALID_ARGUMENT;
    }
    profile.name = name->valuestring;
    profile.nickname = ReadString(root, "nickname");
    profile.birthday = ReadString(root, "birthday");
    profile.city = ReadString(root, "city");
    profile.timezone = ReadString(root, "timezone");
    profile.notes = ReadString(root, "notes");
    cJSON_Delete(root);
    return CareError::OK;
}

cJSON* ProfileToJson(const CareProfile& profile, bool configured) {
    cJSON* item = cJSON_CreateObject();
    if (item == nullptr) return nullptr;
    cJSON_AddBoolToObject(item, "configured", configured);
    cJSON_AddStringToObject(item, "name", profile.name.c_str());
    cJSON_AddStringToObject(item, "nickname", profile.nickname.c_str());
    cJSON_AddStringToObject(item, "birthday", profile.birthday.c_str());
    cJSON_AddStringToObject(item, "city", profile.city.c_str());
    cJSON_AddStringToObject(item, "timezone", profile.timezone.c_str());
    cJSON_AddStringToObject(item, "notes", profile.notes.c_str());
    return item;
}

CareError ParsePreferencePayload(const std::string& body, CarePreference& value) {
    cJSON* root = cJSON_ParseWithLength(body.data(), body.size());
    if (root == nullptr || !cJSON_IsObject(root)) {
        if (root) cJSON_Delete(root);
        return CareError::INVALID_ARGUMENT;
    }
    const cJSON* category = cJSON_GetObjectItemCaseSensitive(root, "category");
    const cJSON* pref_value = cJSON_GetObjectItemCaseSensitive(root, "value");
    if (!cJSON_IsString(category) || category->valuestring == nullptr || category->valuestring[0] == '\0' ||
        !cJSON_IsString(pref_value) || pref_value->valuestring == nullptr || pref_value->valuestring[0] == '\0') {
        cJSON_Delete(root); return CareError::INVALID_ARGUMENT;
    }
    value.owner_id = ReadString(root, "owner_id");
    if (value.owner_id.empty()) value.owner_id = "profile";
    value.category = category->valuestring;
    value.value = pref_value->valuestring;
    value.notes = ReadString(root, "notes");
    value.enabled = ReadEnabled(root);
    cJSON_Delete(root);
    return CareError::OK;
}

cJSON* PreferenceToJson(const CarePreference& value) {
    cJSON* item = cJSON_CreateObject();
    if (item == nullptr) return nullptr;
    cJSON_AddStringToObject(item, "id", value.id.c_str());
    cJSON_AddStringToObject(item, "owner_id", value.owner_id.c_str());
    cJSON_AddStringToObject(item, "category", value.category.c_str());
    cJSON_AddStringToObject(item, "value", value.value.c_str());
    cJSON_AddStringToObject(item, "notes", value.notes.c_str());
    cJSON_AddBoolToObject(item, "enabled", value.enabled);
    return item;
}

CareError ParsePillboxPayload(const std::string& body, CarePillboxEntry& value) {
    cJSON* root = cJSON_ParseWithLength(body.data(), body.size());
    if (root == nullptr || !cJSON_IsObject(root)) {
        if (root) cJSON_Delete(root);
        return CareError::INVALID_ARGUMENT;
    }
    const cJSON* weekday = cJSON_GetObjectItemCaseSensitive(root, "weekday");
    const cJSON* period = cJSON_GetObjectItemCaseSensitive(root, "period");
    const cJSON* compartment = cJSON_GetObjectItemCaseSensitive(root, "compartment");
    if (!cJSON_IsNumber(weekday) || weekday->valueint < 1 || weekday->valueint > 7 ||
        !cJSON_IsString(period) || period->valuestring == nullptr || period->valuestring[0] == '\0' ||
        !cJSON_IsString(compartment) || compartment->valuestring == nullptr || compartment->valuestring[0] == '\0') {
        cJSON_Delete(root); return CareError::INVALID_ARGUMENT;
    }
    value.weekday = static_cast<uint8_t>(weekday->valueint);
    value.period = period->valuestring;
    value.time = ReadString(root, "time");
    value.compartment = compartment->valuestring;
    value.pill_color = ReadString(root, "pill_color");
    value.notes = ReadString(root, "notes");
    value.enabled = ReadEnabled(root);
    cJSON_Delete(root);
    return CareError::OK;
}

cJSON* PillboxToJson(const CarePillboxEntry& value) {
    cJSON* item = cJSON_CreateObject();
    if (item == nullptr) return nullptr;
    cJSON_AddStringToObject(item, "id", value.id.c_str());
    cJSON_AddNumberToObject(item, "weekday", value.weekday);
    cJSON_AddStringToObject(item, "period", value.period.c_str());
    cJSON_AddStringToObject(item, "time", value.time.c_str());
    cJSON_AddStringToObject(item, "compartment", value.compartment.c_str());
    cJSON_AddStringToObject(item, "pill_color", value.pill_color.c_str());
    cJSON_AddStringToObject(item, "notes", value.notes.c_str());
    cJSON_AddBoolToObject(item, "enabled", value.enabled);
    return item;
}

CareError ParseReminderPayload(const std::string& body, CareReminder& value) {
    cJSON* root = cJSON_ParseWithLength(body.data(), body.size());
    if (root == nullptr || !cJSON_IsObject(root)) {
        if (root) cJSON_Delete(root);
        return CareError::INVALID_ARGUMENT;
    }
    const cJSON* title = cJSON_GetObjectItemCaseSensitive(root, "title");
    const cJSON* date = cJSON_GetObjectItemCaseSensitive(root, "date");
    const cJSON* recurrence = cJSON_GetObjectItemCaseSensitive(root, "recurrence");
    if (!cJSON_IsString(title) || title->valuestring == nullptr || title->valuestring[0] == '\0' ||
        !cJSON_IsString(date) || date->valuestring == nullptr || date->valuestring[0] == '\0' ||
        !cJSON_IsString(recurrence) || recurrence->valuestring == nullptr || recurrence->valuestring[0] == '\0') {
        cJSON_Delete(root); return CareError::INVALID_ARGUMENT;
    }
    value.title = title->valuestring;
    value.date = date->valuestring;
    value.time = ReadString(root, "time");
    value.recurrence = recurrence->valuestring;
    value.related_person_id = ReadString(root, "related_person_id");
    value.notes = ReadString(root, "notes");
    value.enabled = ReadEnabled(root);
    cJSON_Delete(root);
    return CareError::OK;
}

cJSON* ReminderToJson(const CareReminder& value) {
    cJSON* item = cJSON_CreateObject();
    if (item == nullptr) return nullptr;
    cJSON_AddStringToObject(item, "id", value.id.c_str());
    cJSON_AddStringToObject(item, "title", value.title.c_str());
    cJSON_AddStringToObject(item, "date", value.date.c_str());
    cJSON_AddStringToObject(item, "time", value.time.c_str());
    cJSON_AddStringToObject(item, "recurrence", value.recurrence.c_str());
    cJSON_AddStringToObject(item, "related_person_id", value.related_person_id.c_str());
    cJSON_AddStringToObject(item, "notes", value.notes.c_str());
    cJSON_AddBoolToObject(item, "enabled", value.enabled);
    return item;
}


bool ParseDailyTimeText(const std::string& text, daily::DailyTime& out) {
    if (text.size() != 5 || text[2] != ':') {
        return false;
    }
    if (text[0] < '0' || text[0] > '9' || text[1] < '0' || text[1] > '9' ||
        text[3] < '0' || text[3] > '9' || text[4] < '0' || text[4] > '9') {
        return false;
    }
    const int hour = (text[0] - '0') * 10 + (text[1] - '0');
    const int minute = (text[3] - '0') * 10 + (text[4] - '0');
    out = daily::DailyTime(static_cast<uint8_t>(hour), static_cast<uint8_t>(minute));
    return out.IsValid();
}

std::string DailyTimeText(const daily::DailyTime& time) {
    if (!time.IsValid()) {
        return "";
    }

    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "%02u:%02u", static_cast<unsigned>(time.hour),
                  static_cast<unsigned>(time.minute));
    return buffer;
}

const char* RoutineTypeText(daily::RoutineType type) {
    switch (type) {
        case daily::RoutineType::Medication: return "medication";
        case daily::RoutineType::Hydration: return "hydration";
        case daily::RoutineType::Exercise: return "exercise";
        case daily::RoutineType::HealthMeasurement: return "health_measurement";
        case daily::RoutineType::Reminder: return "reminder";
        case daily::RoutineType::Birthday: return "birthday";
        case daily::RoutineType::Custom: return "custom";
    }
    return "custom";
}

daily::RoutineType ParseRoutineType(const std::string& value) {
    if (value == "medication") return daily::RoutineType::Medication;
    if (value == "hydration") return daily::RoutineType::Hydration;
    if (value == "exercise") return daily::RoutineType::Exercise;
    if (value == "health_measurement") return daily::RoutineType::HealthMeasurement;
    if (value == "reminder") return daily::RoutineType::Reminder;
    if (value == "birthday") return daily::RoutineType::Birthday;
    return daily::RoutineType::Custom;
}

const char* RoutineStateText(daily::RoutineState state) {
    switch (state) {
        case daily::RoutineState::Active: return "active";
        case daily::RoutineState::Paused: return "paused";
        case daily::RoutineState::Archived: return "archived";
    }
    return "active";
}

daily::RoutineState ParseRoutineState(const std::string& value) {
    if (value == "paused") return daily::RoutineState::Paused;
    if (value == "archived") return daily::RoutineState::Archived;
    return daily::RoutineState::Active;
}

const char* RoutineRepeatText(daily::RoutineRepeatType repeat) {
    switch (repeat) {
        case daily::RoutineRepeatType::Daily: return "daily";
        case daily::RoutineRepeatType::SpecificWeekdays: return "specific_weekdays";
        case daily::RoutineRepeatType::EveryNDays: return "every_n_days";
        case daily::RoutineRepeatType::AsNeeded: return "as_needed";
    }
    return "daily";
}

daily::RoutineRepeatType ParseRoutineRepeat(const std::string& value) {
    if (value == "specific_weekdays") return daily::RoutineRepeatType::SpecificWeekdays;
    if (value == "every_n_days") return daily::RoutineRepeatType::EveryNDays;
    if (value == "as_needed") return daily::RoutineRepeatType::AsNeeded;
    return daily::RoutineRepeatType::Daily;
}

const char* RoutineMomentText(daily::RoutineMoment moment) {
    switch (moment) {
        case daily::RoutineMoment::Anytime: return "anytime";
        case daily::RoutineMoment::Fasting: return "fasting";
        case daily::RoutineMoment::BeforeBreakfast: return "before_breakfast";
        case daily::RoutineMoment::WithBreakfast: return "with_breakfast";
        case daily::RoutineMoment::AfterBreakfast: return "after_breakfast";
        case daily::RoutineMoment::BeforeLunch: return "before_lunch";
        case daily::RoutineMoment::WithLunch: return "with_lunch";
        case daily::RoutineMoment::AfterLunch: return "after_lunch";
        case daily::RoutineMoment::BeforeDinner: return "before_dinner";
        case daily::RoutineMoment::WithDinner: return "with_dinner";
        case daily::RoutineMoment::AfterDinner: return "after_dinner";
        case daily::RoutineMoment::Bedtime: return "bedtime";
    }
    return "anytime";
}

daily::RoutineMoment ParseRoutineMoment(const std::string& value) {
    if (value == "fasting") return daily::RoutineMoment::Fasting;
    if (value == "before_breakfast") return daily::RoutineMoment::BeforeBreakfast;
    if (value == "with_breakfast") return daily::RoutineMoment::WithBreakfast;
    if (value == "after_breakfast") return daily::RoutineMoment::AfterBreakfast;
    if (value == "before_lunch") return daily::RoutineMoment::BeforeLunch;
    if (value == "with_lunch") return daily::RoutineMoment::WithLunch;
    if (value == "after_lunch") return daily::RoutineMoment::AfterLunch;
    if (value == "before_dinner") return daily::RoutineMoment::BeforeDinner;
    if (value == "with_dinner") return daily::RoutineMoment::WithDinner;
    if (value == "after_dinner") return daily::RoutineMoment::AfterDinner;
    if (value == "bedtime") return daily::RoutineMoment::Bedtime;
    return daily::RoutineMoment::Anytime;
}

const char* PlacementTypeText(daily::PlacementType type) {
    switch (type) {
        case daily::PlacementType::None: return "none";
        case daily::PlacementType::Pillbox: return "pillbox";
        case daily::PlacementType::Blister: return "blister";
        case daily::PlacementType::OriginalBox: return "original_box";
        case daily::PlacementType::Table: return "table";
        case daily::PlacementType::Shelf: return "shelf";
        case daily::PlacementType::Custom: return "custom";
    }
    return "none";
}


const char* CareAlertModeText(daily::CareAlertMode mode) {
    switch (mode) {
        case daily::CareAlertMode::Disabled: return "disabled";
        case daily::CareAlertMode::VisualOnly: return "visual";
        case daily::CareAlertMode::SoundOnly: return "sound";
        case daily::CareAlertMode::VisualAndSound: return "visual_sound";
    }
    return "visual_sound";
}

daily::CareAlertMode ParseCareAlertMode(const std::string& value) {
    if (value == "disabled") return daily::CareAlertMode::Disabled;
    if (value == "visual") return daily::CareAlertMode::VisualOnly;
    if (value == "sound") return daily::CareAlertMode::SoundOnly;
    if (value == "visual_sound") return daily::CareAlertMode::VisualAndSound;
    return daily::CareAlertMode::VisualOnly;
}


const char* CareVisualPatternText(daily::CareVisualPattern pattern) {
    switch (pattern) {
        case daily::CareVisualPattern::None: return "none";
        case daily::CareVisualPattern::Solid: return "solid";
        case daily::CareVisualPattern::Breathing: return "breathing";
        case daily::CareVisualPattern::SoftBlink: return "soft_blink";
        case daily::CareVisualPattern::ProgressBar: return "progress_bar";
        case daily::CareVisualPattern::PillboxSlot: return "pillbox_slot";
    }
    return "breathing";
}

daily::CareVisualPattern ParseCareVisualPattern(const std::string& value) {
    if (value == "none") return daily::CareVisualPattern::None;
    if (value == "solid") return daily::CareVisualPattern::Solid;
    if (value == "breathing") return daily::CareVisualPattern::Breathing;
    if (value == "soft_blink") return daily::CareVisualPattern::SoftBlink;
    if (value == "progress_bar") return daily::CareVisualPattern::ProgressBar;
    if (value == "pillbox_slot") return daily::CareVisualPattern::PillboxSlot;
    return daily::CareVisualPattern::Breathing;
}

const char* CareAlertColorText(daily::CareAlertColor color) {
    switch (color) {
        case daily::CareAlertColor::Auto: return "auto";
        case daily::CareAlertColor::Green: return "green";
        case daily::CareAlertColor::Blue: return "blue";
        case daily::CareAlertColor::Yellow: return "yellow";
        case daily::CareAlertColor::SoftRed: return "soft_red";
        case daily::CareAlertColor::Violet: return "violet";
        case daily::CareAlertColor::White: return "white";
    }
    return "auto";
}

daily::CareAlertColor ParseCareAlertColor(const std::string& value) {
    if (value == "green") return daily::CareAlertColor::Green;
    if (value == "blue") return daily::CareAlertColor::Blue;
    if (value == "yellow") return daily::CareAlertColor::Yellow;
    if (value == "soft_red") return daily::CareAlertColor::SoftRed;
    if (value == "violet") return daily::CareAlertColor::Violet;
    if (value == "white") return daily::CareAlertColor::White;
    return daily::CareAlertColor::Auto;
}

const char* CareSoundPatternText(daily::CareSoundPattern pattern) {
    switch (pattern) {
        case daily::CareSoundPattern::None: return "none";
        case daily::CareSoundPattern::SoftBeep: return "soft_beep";
        case daily::CareSoundPattern::Chime: return "chime";
        case daily::CareSoundPattern::FriendlyReminder: return "friendly_reminder";
        case daily::CareSoundPattern::Progressive: return "progressive";
    }
    return "soft_beep";
}

daily::CareSoundPattern ParseCareSoundPattern(const std::string& value) {
    if (value == "none") return daily::CareSoundPattern::None;
    if (value == "chime") return daily::CareSoundPattern::Chime;
    if (value == "friendly_reminder") return daily::CareSoundPattern::FriendlyReminder;
    if (value == "progressive") return daily::CareSoundPattern::Progressive;
    return daily::CareSoundPattern::SoftBeep;
}

daily::PlacementType ParsePlacementType(const std::string& value) {
    if (value == "pillbox") return daily::PlacementType::Pillbox;
    if (value == "blister") return daily::PlacementType::Blister;
    if (value == "original_box") return daily::PlacementType::OriginalBox;
    if (value == "table") return daily::PlacementType::Table;
    if (value == "shelf") return daily::PlacementType::Shelf;
    if (value == "custom") return daily::PlacementType::Custom;
    return daily::PlacementType::None;
}

bool EnsureRoutineRepository(daily::NvsRoutineRepository*& repo) {
    static daily::NvsRoutineRepository instance;
    static bool initialized = false;
    if (!initialized) {
        initialized = instance.Init();
    }
    repo = &instance;
    return initialized;
}

CareError ParseRoutinePayload(const std::string& body, daily::CareRoutine& routine) {
    cJSON* root = cJSON_ParseWithLength(body.data(), body.size());
    if (root == nullptr || !cJSON_IsObject(root)) {
        if (root) cJSON_Delete(root);
        return CareError::INVALID_ARGUMENT;
    }

    const std::string title = ReadString(root, "title");
    const std::string time_text = ReadString(root, "time");
    if (title.empty()) {
        cJSON_Delete(root);
        return CareError::INVALID_ARGUMENT;
    }

    daily::DailyTime parsed_time;
    if (!ParseDailyTimeText(time_text, parsed_time)) {
        cJSON_Delete(root);
        return CareError::INVALID_TIME;
    }

    routine.title = title;
    routine.description = ReadString(root, "description");
    routine.data = ReadString(root, "data");
    routine.type = ParseRoutineType(ReadString(root, "type"));
    routine.state = ParseRoutineState(ReadString(root, "state"));

    routine.schedule.time = parsed_time;
    routine.schedule.repeat = ParseRoutineRepeat(ReadString(root, "repeat"));
    routine.schedule.moment = ParseRoutineMoment(ReadString(root, "moment"));
    routine.schedule.interval_days = static_cast<uint16_t>(ReadInt(root, "interval_days", 1));
    routine.schedule.reminder_window_minutes = static_cast<uint16_t>(ReadInt(root, "reminder_window_minutes", 30));

    const cJSON* weekdays = cJSON_GetObjectItemCaseSensitive(root, "weekdays");
    if (cJSON_IsArray(weekdays)) {
        for (int i = 0; i < 7; ++i) {
            const cJSON* item = cJSON_GetArrayItem(weekdays, i);
            routine.schedule.weekdays[static_cast<size_t>(i)] = cJSON_IsBool(item) ? cJSON_IsTrue(item) : false;
        }
    }

    routine.placement.type = ParsePlacementType(ReadString(root, "placement_type"));
    routine.placement.description = ReadString(root, "placement_description");
    routine.placement.compartment = ReadString(root, "compartment");
    routine.placement.monitored = ReadBoolField(root, "monitored", false);
    routine.placement.ble_slot = static_cast<uint8_t>(ReadInt(root, "ble_slot", 0));
    routine.placement.led_number = static_cast<uint8_t>(ReadInt(root, "led_number", 0));

    routine.alert.enabled = ReadBoolField(root, "alert_enabled", true);
    routine.alert.mode = ParseCareAlertMode(ReadString(root, "alert_mode"));
    routine.alert.visual_pattern = ParseCareVisualPattern(ReadString(root, "alert_visual_pattern"));
    routine.alert.visual_color = ParseCareAlertColor(ReadString(root, "alert_visual_color"));
    routine.alert.sound_pattern = ParseCareSoundPattern(ReadString(root, "alert_sound_pattern"));
    routine.alert.voice_enabled = ReadBoolField(root, "alert_voice_enabled", false);
    routine.alert.repeat_minutes = static_cast<uint16_t>(ReadInt(root, "alert_repeat_minutes", 10));
    routine.alert.max_repeats = static_cast<uint8_t>(ReadInt(root, "alert_max_repeats", 3));

    cJSON_Delete(root);
    return (routine.schedule.IsValid() && routine.alert.IsValid()) ? CareError::OK : CareError::INVALID_ARGUMENT;
}

cJSON* RoutineToJson(const daily::CareRoutine& routine) {
    cJSON* item = cJSON_CreateObject();
    if (item == nullptr) return nullptr;

    cJSON_AddStringToObject(item, "id", routine.id.Str().c_str());
    cJSON_AddStringToObject(item, "type", RoutineTypeText(routine.type));
    cJSON_AddStringToObject(item, "state", RoutineStateText(routine.state));
    cJSON_AddStringToObject(item, "title", routine.title.c_str());
    cJSON_AddStringToObject(item, "description", routine.description.c_str());
    cJSON_AddStringToObject(item, "time", DailyTimeText(routine.schedule.time).c_str());
    cJSON_AddStringToObject(item, "repeat", RoutineRepeatText(routine.schedule.repeat));
    cJSON_AddStringToObject(item, "moment", RoutineMomentText(routine.schedule.moment));
    cJSON_AddNumberToObject(item, "interval_days", routine.schedule.interval_days);
    cJSON_AddNumberToObject(item, "reminder_window_minutes", routine.schedule.reminder_window_minutes);

    cJSON* weekdays = cJSON_AddArrayToObject(item, "weekdays");
    if (weekdays == nullptr) {
        cJSON_Delete(item);
        return nullptr;
    }
    for (bool enabled : routine.schedule.weekdays) {
        cJSON_AddItemToArray(weekdays, cJSON_CreateBool(enabled));
    }

    cJSON_AddStringToObject(item, "placement_type", PlacementTypeText(routine.placement.type));
    cJSON_AddStringToObject(item, "placement_description", routine.placement.description.c_str());
    cJSON_AddStringToObject(item, "compartment", routine.placement.compartment.c_str());
    cJSON_AddBoolToObject(item, "monitored", routine.placement.monitored);
    cJSON_AddNumberToObject(item, "ble_slot", routine.placement.ble_slot);
    cJSON_AddNumberToObject(item, "led_number", routine.placement.led_number);
    cJSON_AddBoolToObject(item, "alert_enabled", routine.alert.enabled);
    cJSON_AddStringToObject(item, "alert_mode", CareAlertModeText(routine.alert.mode));
    cJSON_AddStringToObject(item, "alert_visual_pattern", CareVisualPatternText(routine.alert.visual_pattern));
    cJSON_AddStringToObject(item, "alert_visual_color", CareAlertColorText(routine.alert.visual_color));
    cJSON_AddStringToObject(item, "alert_sound_pattern", CareSoundPatternText(routine.alert.sound_pattern));
    cJSON_AddBoolToObject(item, "alert_voice_enabled", routine.alert.voice_enabled);
    cJSON_AddNumberToObject(item, "alert_repeat_minutes", routine.alert.repeat_minutes);
    cJSON_AddNumberToObject(item, "alert_max_repeats", routine.alert.max_repeats);
    cJSON_AddStringToObject(item, "data", routine.data.c_str());
    cJSON_AddBoolToObject(item, "enabled", routine.state == daily::RoutineState::Active);
    return item;
}

template <typename T, typename ToJson>
esp_err_t SendList(httpd_req_t* req, const std::vector<T>& values, ToJson to_json) {
    cJSON* root = cJSON_CreateObject();
    cJSON* data = cJSON_CreateArray();
    if (root == nullptr || data == nullptr) {
        if (root) cJSON_Delete(root);
        if (data) cJSON_Delete(data);
        return SendError(req, "500 Internal Server Error", CareError::INTERNAL_ERROR);
    }
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddItemToObject(root, "data", data);
    for (const auto& value : values) {
        cJSON* item = to_json(value);
        if (item == nullptr) {
            cJSON_Delete(root);
            return SendError(req, "500 Internal Server Error", CareError::INTERNAL_ERROR);
        }
        cJSON_AddItemToArray(data, item);
    }
    return SendRoot(req, root);
}

template <typename T, typename ToJson>
esp_err_t SendOne(httpd_req_t* req, const T& value, ToJson to_json, const char* status = nullptr) {
    cJSON* root = cJSON_CreateObject();
    cJSON* data = to_json(value);
    if (root == nullptr || data == nullptr) {
        if (root) cJSON_Delete(root);
        if (data) cJSON_Delete(data);
        return SendError(req, "500 Internal Server Error", CareError::INTERNAL_ERROR);
    }
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddItemToObject(root, "data", data);
    return SendRoot(req, root, status);
}

}  // namespace

esp_err_t CareWebServer::HandleProfileGet(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, false)) return ESP_OK;
    CareProfile profile;
    CareError result = CareManager::GetInstance().GetProfile(profile);
    bool configured = true;
    if (result == CareError::NOT_FOUND) {
        configured = false;
        profile = CareProfile{};
    } else if (result != CareError::OK) {
        return SendError(req, HttpStatusFor(result), result);
    }
    cJSON* root = cJSON_CreateObject();
    cJSON* data = ProfileToJson(profile, configured);
    if (root == nullptr || data == nullptr) {
        if (root) cJSON_Delete(root);
        if (data) cJSON_Delete(data);
        return SendError(req, "500 Internal Server Error", CareError::INTERNAL_ERROR);
    }
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddItemToObject(root, "data", data);
    return SendRoot(req, root);
}

esp_err_t CareWebServer::HandleProfilePut(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, true)) return ESP_OK;
    std::string body;
    if (ReadBody(req, body) != ESP_OK) return SendError(req, "400 Bad Request", CareError::INVALID_ARGUMENT);
    CareProfile profile;
    CareError parsed = ParseProfilePayload(body, profile);
    if (parsed != CareError::OK) return SendError(req, HttpStatusFor(parsed), parsed);
    CareError result = CareManager::GetInstance().SaveProfile(profile);
    if (result != CareError::OK) return SendError(req, HttpStatusFor(result), result);
    return SendOne(req, profile, [](const CareProfile& p) { return ProfileToJson(p, true); });
}

esp_err_t CareWebServer::HandlePreferencesList(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, false)) return ESP_OK;
    std::vector<CarePreference> values;
    CareError result = CareManager::GetInstance().ListPreferences(values);
    if (result != CareError::OK) return SendError(req, HttpStatusFor(result), result);
    return SendList(req, values, PreferenceToJson);
}

esp_err_t CareWebServer::HandlePreferencesCreate(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, true)) return ESP_OK;
    std::string body;
    if (ReadBody(req, body) != ESP_OK) return SendError(req, "400 Bad Request", CareError::INVALID_ARGUMENT);
    CarePreference value;
    CareError parsed = ParsePreferencePayload(body, value);
    if (parsed != CareError::OK) return SendError(req, HttpStatusFor(parsed), parsed);
    std::string id;
    CareError result = CareManager::GetInstance().AddPreference(std::move(value), id);
    if (result != CareError::OK) return SendError(req, HttpStatusFor(result), result);

    // DP034_R4_CACHE_COHERENCE
    CareMcpService::GetInstance().InvalidatePreferencesCache();

    CarePreference saved;
    result = CareManager::GetInstance().GetPreference(id, saved);
    if (result != CareError::OK) return SendError(req, HttpStatusFor(result), result);
    return SendOne(req, saved, PreferenceToJson, "201 Created");
}

esp_err_t CareWebServer::HandlePreferenceGet(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, false)) return ESP_OK;
    std::string id;
    if (!ExtractId(req, kApiPreferencesPrefix, id)) return SendError(req, "400 Bad Request", CareError::INVALID_ID);
    CarePreference value;
    CareError result = CareManager::GetInstance().GetPreference(id, value);
    if (result != CareError::OK) return SendError(req, HttpStatusFor(result), result);
    return SendOne(req, value, PreferenceToJson);
}

esp_err_t CareWebServer::HandlePreferenceUpdate(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, true)) return ESP_OK;
    std::string id;
    if (!ExtractId(req, kApiPreferencesPrefix, id)) return SendError(req, "400 Bad Request", CareError::INVALID_ID);
    std::string body;
    if (ReadBody(req, body) != ESP_OK) return SendError(req, "400 Bad Request", CareError::INVALID_ARGUMENT);
    CarePreference value;
    CareError parsed = ParsePreferencePayload(body, value);
    if (parsed != CareError::OK) return SendError(req, HttpStatusFor(parsed), parsed);
    value.id = id;
    CareError result = CareManager::GetInstance().UpdatePreference(value);
    if (result != CareError::OK) return SendError(req, HttpStatusFor(result), result);

    // DP034_R4_CACHE_COHERENCE
    CareMcpService::GetInstance().InvalidatePreferencesCache();

    return SendOne(req, value, PreferenceToJson);
}

esp_err_t CareWebServer::HandlePreferenceDelete(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, true)) return ESP_OK;
    std::string id;
    if (!ExtractId(req, kApiPreferencesPrefix, id)) return SendError(req, "400 Bad Request", CareError::INVALID_ID);
    CareError result = CareManager::GetInstance().DeletePreference(id);
    if (result != CareError::OK) return SendError(req, HttpStatusFor(result), result);

    // DP034_R4_CACHE_COHERENCE
    CareMcpService::GetInstance().InvalidatePreferencesCache();

    return SendJsonText(req, "200 OK", "{\"success\":true}");
}

esp_err_t CareWebServer::HandlePillboxList(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, false)) return ESP_OK;
    std::vector<CarePillboxEntry> values;
    CareError result = CareManager::GetInstance().ListPillboxEntries(values);
    if (result != CareError::OK) return SendError(req, HttpStatusFor(result), result);
    return SendList(req, values, PillboxToJson);
}

esp_err_t CareWebServer::HandlePillboxCreate(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, true)) return ESP_OK;
    std::string body;
    if (ReadBody(req, body) != ESP_OK) return SendError(req, "400 Bad Request", CareError::INVALID_ARGUMENT);
    CarePillboxEntry value;
    CareError parsed = ParsePillboxPayload(body, value);
    if (parsed != CareError::OK) return SendError(req, HttpStatusFor(parsed), parsed);
    std::string id;
    CareError result = CareManager::GetInstance().AddPillboxEntry(std::move(value), id);
    if (result != CareError::OK) return SendError(req, HttpStatusFor(result), result);
    CarePillboxEntry saved;
    result = CareManager::GetInstance().GetPillboxEntry(id, saved);
    if (result != CareError::OK) return SendError(req, HttpStatusFor(result), result);
    return SendOne(req, saved, PillboxToJson, "201 Created");
}

esp_err_t CareWebServer::HandlePillboxGet(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, false)) return ESP_OK;
    std::string id;
    if (!ExtractId(req, kApiPillboxPrefix, id)) return SendError(req, "400 Bad Request", CareError::INVALID_ID);
    CarePillboxEntry value;
    CareError result = CareManager::GetInstance().GetPillboxEntry(id, value);
    if (result != CareError::OK) return SendError(req, HttpStatusFor(result), result);
    return SendOne(req, value, PillboxToJson);
}

esp_err_t CareWebServer::HandlePillboxUpdate(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, true)) return ESP_OK;
    std::string id;
    if (!ExtractId(req, kApiPillboxPrefix, id)) return SendError(req, "400 Bad Request", CareError::INVALID_ID);
    std::string body;
    if (ReadBody(req, body) != ESP_OK) return SendError(req, "400 Bad Request", CareError::INVALID_ARGUMENT);
    CarePillboxEntry value;
    CareError parsed = ParsePillboxPayload(body, value);
    if (parsed != CareError::OK) return SendError(req, HttpStatusFor(parsed), parsed);
    value.id = id;
    CareError result = CareManager::GetInstance().UpdatePillboxEntry(value);
    if (result != CareError::OK) return SendError(req, HttpStatusFor(result), result);
    return SendOne(req, value, PillboxToJson);
}

esp_err_t CareWebServer::HandlePillboxDelete(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, true)) return ESP_OK;
    std::string id;
    if (!ExtractId(req, kApiPillboxPrefix, id)) return SendError(req, "400 Bad Request", CareError::INVALID_ID);
    CareError result = CareManager::GetInstance().DeletePillboxEntry(id);
    if (result != CareError::OK) return SendError(req, HttpStatusFor(result), result);
    return SendJsonText(req, "200 OK", "{\"success\":true}");
}

esp_err_t CareWebServer::HandleRemindersList(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, false)) return ESP_OK;
    std::vector<CareReminder> values;
    CareError result = CareManager::GetInstance().ListReminders(values);
    if (result != CareError::OK) return SendError(req, HttpStatusFor(result), result);
    return SendList(req, values, ReminderToJson);
}

esp_err_t CareWebServer::HandleRemindersCreate(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, true)) return ESP_OK;
    std::string body;
    if (ReadBody(req, body) != ESP_OK) return SendError(req, "400 Bad Request", CareError::INVALID_ARGUMENT);
    CareReminder value;
    CareError parsed = ParseReminderPayload(body, value);
    if (parsed != CareError::OK) return SendError(req, HttpStatusFor(parsed), parsed);
    std::string id;
    CareError result = CareManager::GetInstance().AddReminder(std::move(value), id);
    if (result != CareError::OK) return SendError(req, HttpStatusFor(result), result);
    CareReminder saved;
    result = CareManager::GetInstance().GetReminder(id, saved);
    if (result != CareError::OK) return SendError(req, HttpStatusFor(result), result);
    return SendOne(req, saved, ReminderToJson, "201 Created");
}

esp_err_t CareWebServer::HandleReminderGet(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, false)) return ESP_OK;
    std::string id;
    if (!ExtractId(req, kApiRemindersPrefix, id)) return SendError(req, "400 Bad Request", CareError::INVALID_ID);
    CareReminder value;
    CareError result = CareManager::GetInstance().GetReminder(id, value);
    if (result != CareError::OK) return SendError(req, HttpStatusFor(result), result);
    return SendOne(req, value, ReminderToJson);
}

esp_err_t CareWebServer::HandleReminderUpdate(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, true)) return ESP_OK;
    std::string id;
    if (!ExtractId(req, kApiRemindersPrefix, id)) return SendError(req, "400 Bad Request", CareError::INVALID_ID);
    std::string body;
    if (ReadBody(req, body) != ESP_OK) return SendError(req, "400 Bad Request", CareError::INVALID_ARGUMENT);
    CareReminder value;
    CareError parsed = ParseReminderPayload(body, value);
    if (parsed != CareError::OK) return SendError(req, HttpStatusFor(parsed), parsed);
    value.id = id;
    CareError result = CareManager::GetInstance().UpdateReminder(value);
    if (result != CareError::OK) return SendError(req, HttpStatusFor(result), result);
    return SendOne(req, value, ReminderToJson);
}

esp_err_t CareWebServer::HandleReminderDelete(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, true)) return ESP_OK;
    std::string id;
    if (!ExtractId(req, kApiRemindersPrefix, id)) return SendError(req, "400 Bad Request", CareError::INVALID_ID);
    CareError result = CareManager::GetInstance().DeleteReminder(id);
    if (result != CareError::OK) return SendError(req, HttpStatusFor(result), result);

    // DP-018: preserve the audio file, but remove the association if its reminder disappears.
    auto& voice_store = voice::VoiceRecordingStore::GetInstance();
    if (voice_store.Init() && !voice_store.UnassignReminder(id)) {
        ESP_LOGW("CARE_WEB", "Reminder deleted but voice association could not be cleared id=%s", id.c_str());
    }
    return SendJsonText(req, "200 OK", "{\"success\":true}");
}



CareError ParseFamilyPayload(const std::string& body, family::FamilyRelationship& value) {
    cJSON* root = cJSON_ParseWithLength(body.data(), body.size());
    if (root == nullptr || !cJSON_IsObject(root)) {
        if (root) cJSON_Delete(root);
        return CareError::INVALID_ARGUMENT;
    }
    value.from_person_id = ReadString(root, "from_person_id");
    value.to_person_id = ReadString(root, "to_person_id");
    value.type = ReadString(root, "type");
    value.label = ReadString(root, "label");
    value.notes = ReadString(root, "notes");
    value.enabled = ReadEnabled(root);
    cJSON_Delete(root);
    if (value.from_person_id.empty() || value.to_person_id.empty()) return CareError::INVALID_ARGUMENT;
    if (value.from_person_id == value.to_person_id) return CareError::INVALID_ARGUMENT;
    if (value.type.empty()) value.type = "other";
    if (value.label.empty()) value.label = "relación";
    return CareError::OK;
}

cJSON* FamilyRelationshipToJson(const family::FamilyRelationship& value) {
    cJSON* item = cJSON_CreateObject();
    if (item == nullptr) return nullptr;
    cJSON_AddStringToObject(item, "id", value.id.value.c_str());
    cJSON_AddStringToObject(item, "from_person_id", value.from_person_id.c_str());
    cJSON_AddStringToObject(item, "to_person_id", value.to_person_id.c_str());
    cJSON_AddStringToObject(item, "type", value.type.c_str());
    cJSON_AddStringToObject(item, "label", value.label.c_str());
    cJSON_AddStringToObject(item, "notes", value.notes.c_str());
    cJSON_AddBoolToObject(item, "enabled", value.enabled);
    return item;
}

bool EnsureFamilyRepository(family::NvsFamilyRelationshipRepository*& repo) {
    repo = &family::GetFamilyRelationshipRepository();
    return repo->IsReady() || repo->Init();
}

esp_err_t CareWebServer::HandleFamilyList(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, false)) return ESP_OK;
    family::NvsFamilyRelationshipRepository* repo = nullptr;
    if (!EnsureFamilyRepository(repo)) return SendError(req, "503 Service Unavailable", CareError::NOT_INITIALIZED);
    return SendList(req, repo->List(), FamilyRelationshipToJson);
}

esp_err_t CareWebServer::HandleFamilyCreate(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, true)) return ESP_OK;
    family::NvsFamilyRelationshipRepository* repo = nullptr;
    if (!EnsureFamilyRepository(repo)) return SendError(req, "503 Service Unavailable", CareError::NOT_INITIALIZED);
    std::string body;
    if (ReadBody(req, body) != ESP_OK) return SendError(req, "400 Bad Request", CareError::INVALID_ARGUMENT);
    family::FamilyRelationship value;
    CareError parsed = ParseFamilyPayload(body, value);
    if (parsed != CareError::OK) return SendError(req, HttpStatusFor(parsed), parsed);
    value.id = repo->GenerateId();
    if (value.id.Empty()) return SendError(req, "500 Internal Server Error", CareError::STORAGE_ERROR);
    if (!repo->Save(value)) return SendError(req, "500 Internal Server Error", CareError::STORAGE_ERROR);
    return SendOne(req, value, FamilyRelationshipToJson, "201 Created");
}

esp_err_t CareWebServer::HandleFamilyGet(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, false)) return ESP_OK;
    family::NvsFamilyRelationshipRepository* repo = nullptr;
    if (!EnsureFamilyRepository(repo)) return SendError(req, "503 Service Unavailable", CareError::NOT_INITIALIZED);
    std::string id;
    if (!ExtractId(req, kApiFamilyPrefix, id)) return SendError(req, "400 Bad Request", CareError::INVALID_ID);
    auto value = repo->FindById(family::RelationshipId(id));
    if (!value.has_value()) return SendError(req, "404 Not Found", CareError::NOT_FOUND);
    return SendOne(req, value.value(), FamilyRelationshipToJson);
}

esp_err_t CareWebServer::HandleFamilyUpdate(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, true)) return ESP_OK;
    family::NvsFamilyRelationshipRepository* repo = nullptr;
    if (!EnsureFamilyRepository(repo)) return SendError(req, "503 Service Unavailable", CareError::NOT_INITIALIZED);
    std::string id;
    if (!ExtractId(req, kApiFamilyPrefix, id)) return SendError(req, "400 Bad Request", CareError::INVALID_ID);
    std::string body;
    if (ReadBody(req, body) != ESP_OK) return SendError(req, "400 Bad Request", CareError::INVALID_ARGUMENT);
    family::FamilyRelationship value;
    CareError parsed = ParseFamilyPayload(body, value);
    if (parsed != CareError::OK) return SendError(req, HttpStatusFor(parsed), parsed);
    value.id = family::RelationshipId(id);
    if (!repo->Save(value)) return SendError(req, "500 Internal Server Error", CareError::STORAGE_ERROR);
    return SendOne(req, value, FamilyRelationshipToJson);
}

esp_err_t CareWebServer::HandleFamilyDelete(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, true)) return ESP_OK;
    family::NvsFamilyRelationshipRepository* repo = nullptr;
    if (!EnsureFamilyRepository(repo)) return SendError(req, "503 Service Unavailable", CareError::NOT_INITIALIZED);
    std::string id;
    if (!ExtractId(req, kApiFamilyPrefix, id)) return SendError(req, "400 Bad Request", CareError::INVALID_ID);
    if (!repo->Delete(family::RelationshipId(id))) return SendError(req, "404 Not Found", CareError::NOT_FOUND);
    return SendJsonText(req, "200 OK", "{\"success\":true}");
}

esp_err_t CareWebServer::HandleRoutinesList(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, false)) return ESP_OK;
    daily::NvsRoutineRepository* repo = nullptr;
    if (!EnsureRoutineRepository(repo)) return SendError(req, "503 Service Unavailable", CareError::NOT_INITIALIZED);
    std::vector<daily::CareRoutine> values = repo->List();
    return SendList(req, values, RoutineToJson);
}

esp_err_t CareWebServer::HandleRoutinesCreate(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, true)) return ESP_OK;
    daily::NvsRoutineRepository* repo = nullptr;
    if (!EnsureRoutineRepository(repo)) return SendError(req, "503 Service Unavailable", CareError::NOT_INITIALIZED);

    std::string body;
    if (ReadBody(req, body) != ESP_OK) return SendError(req, "400 Bad Request", CareError::INVALID_ARGUMENT);
    daily::CareRoutine value;
    CareError parsed = ParseRoutinePayload(body, value);
    if (parsed != CareError::OK) return SendError(req, HttpStatusFor(parsed), parsed);
    value.id = repo->GenerateId();
    if (value.id.Empty()) return SendError(req, "500 Internal Server Error", CareError::STORAGE_ERROR);
    if (!repo->Save(value)) return SendError(req, "500 Internal Server Error", CareError::STORAGE_ERROR);
    ESP_LOGI("CARE_WEB", "Routine created successfully id=%s title=%s",
             value.id.Str().c_str(), value.title.c_str());
    return SendOne(req, value, RoutineToJson, "201 Created");
}

esp_err_t CareWebServer::HandleRoutineGet(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, false)) return ESP_OK;
    daily::NvsRoutineRepository* repo = nullptr;
    if (!EnsureRoutineRepository(repo)) return SendError(req, "503 Service Unavailable", CareError::NOT_INITIALIZED);
    std::string id;
    if (!ExtractId(req, kApiRoutinesPrefix, id)) return SendError(req, "400 Bad Request", CareError::INVALID_ID);
    std::optional<daily::CareRoutine> value = repo->FindById(daily::RoutineId(id));
    if (!value.has_value()) return SendError(req, "404 Not Found", CareError::NOT_FOUND);
    return SendOne(req, value.value(), RoutineToJson);
}

esp_err_t CareWebServer::HandleRoutineUpdate(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, true)) return ESP_OK;
    daily::NvsRoutineRepository* repo = nullptr;
    if (!EnsureRoutineRepository(repo)) return SendError(req, "503 Service Unavailable", CareError::NOT_INITIALIZED);
    std::string id;
    if (!ExtractId(req, kApiRoutinesPrefix, id)) return SendError(req, "400 Bad Request", CareError::INVALID_ID);

    std::string body;
    if (ReadBody(req, body) != ESP_OK) return SendError(req, "400 Bad Request", CareError::INVALID_ARGUMENT);
    daily::CareRoutine value;
    CareError parsed = ParseRoutinePayload(body, value);
    if (parsed != CareError::OK) return SendError(req, HttpStatusFor(parsed), parsed);
    value.id = daily::RoutineId(id);
    if (!repo->Save(value)) return SendError(req, "500 Internal Server Error", CareError::STORAGE_ERROR);
    ESP_LOGI("CARE_WEB", "Routine updated successfully id=%s title=%s",
             value.id.Str().c_str(), value.title.c_str());
    return SendOne(req, value, RoutineToJson);
}

esp_err_t CareWebServer::HandleRoutineDelete(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, true)) return ESP_OK;
    daily::NvsRoutineRepository* repo = nullptr;
    if (!EnsureRoutineRepository(repo)) return SendError(req, "503 Service Unavailable", CareError::NOT_INITIALIZED);
    std::string id;
    if (!ExtractId(req, kApiRoutinesPrefix, id)) return SendError(req, "400 Bad Request", CareError::INVALID_ID);
    if (!repo->Delete(daily::RoutineId(id))) return SendError(req, "404 Not Found", CareError::NOT_FOUND);
    return SendJsonText(req, "200 OK", "{\"success\":true}");
}



bool ValidateVoiceTargetId(const std::string& target_id) {
    if (target_id.empty()) return false;

    static const std::string kRoutinePrefix = "routine:";
    static const std::string kReminderPrefix = "reminder:";

    if (target_id.compare(0, kRoutinePrefix.size(), kRoutinePrefix) == 0) {
        const std::string routine_id = target_id.substr(kRoutinePrefix.size());
        if (routine_id.empty()) return false;

        daily::NvsRoutineRepository* repo = nullptr;
        if (!EnsureRoutineRepository(repo)) return false;

        std::optional<daily::CareRoutine> routine = repo->FindById(daily::RoutineId(routine_id));
        return routine.has_value();
    }

    std::string reminder_id = target_id;
    if (target_id.compare(0, kReminderPrefix.size(), kReminderPrefix) == 0) {
        reminder_id = target_id.substr(kReminderPrefix.size());
    }

    if (reminder_id.empty()) return false;

    CareReminder reminder;
    return CareManager::GetInstance().GetReminder(reminder_id, reminder) == CareError::OK;
}


esp_err_t CareWebServer::HandleVoiceRecordingsList(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, false)) return ESP_OK;
    auto& store = voice::VoiceRecordingStore::GetInstance();
    if (!store.Init()) return SendNamedError(req, "503 Service Unavailable", "VOICE_STORAGE_NOT_READY");

    cJSON* root = cJSON_CreateObject();
    cJSON* data = cJSON_CreateArray();
    cJSON* limits = cJSON_CreateObject();
    if (root == nullptr || data == nullptr || limits == nullptr) {
        if (root) cJSON_Delete(root);
        if (data) cJSON_Delete(data);
        if (limits) cJSON_Delete(limits);
        return SendNamedError(req, "500 Internal Server Error", "INTERNAL_ERROR");
    }

    cJSON_AddBoolToObject(root, "success", true);
    const auto& l = store.Limits();
    cJSON_AddNumberToObject(limits, "max_recordings", static_cast<double>(l.max_recordings));
    cJSON_AddNumberToObject(limits, "max_file_bytes", static_cast<double>(l.max_file_bytes));
    cJSON_AddNumberToObject(limits, "max_duration_ms", l.max_duration_ms);
    cJSON_AddItemToObject(root, "limits", limits);

    for (const auto& r : store.List()) {
        cJSON* item = cJSON_CreateObject();
        if (item == nullptr) {
            cJSON_Delete(root);
            return SendNamedError(req, "500 Internal Server Error", "INTERNAL_ERROR");
        }
        cJSON_AddStringToObject(item, "id", r.id.c_str());
        cJSON_AddStringToObject(item, "label", r.label.c_str());
        cJSON_AddStringToObject(item, "text", r.text.c_str());
        cJSON_AddStringToObject(item, "reminder_id", r.reminder_id.c_str());
        cJSON_AddNumberToObject(item, "duration_ms", r.duration_ms);
        cJSON_AddNumberToObject(item, "size_bytes", static_cast<double>(r.size_bytes));
        cJSON_AddBoolToObject(item, "enabled", r.enabled);
        cJSON_AddItemToArray(data, item);
    }

    cJSON_AddItemToObject(root, "data", data);
    return SendRoot(req, root);
}

esp_err_t CareWebServer::HandleVoiceRecordingsCreate(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, true)) return ESP_OK;

    std::string body;
    if (ReadBody(req, body) != ESP_OK) {
        return SendNamedError(req, "400 Bad Request", "VOICE_UPLOAD_TOO_LARGE");
    }

    cJSON* root = cJSON_ParseWithLength(body.data(), body.size());
    if (!cJSON_IsObject(root)) {
        if (root) cJSON_Delete(root);
        return SendNamedError(req, "400 Bad Request", "INVALID_JSON");
    }

    const std::string label = ReadString(root, "label");
    const std::string text_note = ReadString(root, "text");
    const std::string reminder_id = ReadString(root, "reminder_id");
    std::string b64 = ReadString(root, "content_base64");
    cJSON_Delete(root);

    if (label.empty() || reminder_id.empty() || b64.empty()) {
        return SendNamedError(req, "400 Bad Request", "INVALID_VOICE_RECORDING");
    }

    if (!ValidateVoiceTargetId(reminder_id)) {
        return SendNamedError(req, "404 Not Found", "REMINDER_NOT_FOUND");
    }

    size_t decoded_len = 0;
    int rc = mbedtls_base64_decode(
        nullptr, 0, &decoded_len,
        reinterpret_cast<const unsigned char*>(b64.data()), b64.size());
    if (rc != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL || decoded_len == 0) {
        return SendNamedError(req, "400 Bad Request", "INVALID_BASE64");
    }

    std::vector<uint8_t> bytes(decoded_len);
    rc = mbedtls_base64_decode(
        bytes.data(), bytes.size(), &decoded_len,
        reinterpret_cast<const unsigned char*>(b64.data()), b64.size());
    b64.clear();
    if (rc != 0) return SendNamedError(req, "400 Bad Request", "INVALID_BASE64");
    bytes.resize(decoded_len);

    voice::VoiceRecordingInfo saved;
    std::string error;
    auto& store = voice::VoiceRecordingStore::GetInstance();
    if (!store.Save(label, text_note, reminder_id, bytes.data(), bytes.size(), saved, error)) {
        const char* status = error == "REMINDER_ALREADY_HAS_AUDIO" ? "409 Conflict" : "400 Bad Request";
        ESP_LOGW("CARE_WEB", "Voice recording rejected: %s", error.c_str());
        return SendNamedError(req, status, error.c_str());
    }

    cJSON* out = cJSON_CreateObject();
    cJSON* response_data = cJSON_CreateObject();
    if (out == nullptr || response_data == nullptr) {
        if (out) cJSON_Delete(out);
        if (response_data) cJSON_Delete(response_data);
        return SendNamedError(req, "500 Internal Server Error", "INTERNAL_ERROR");
    }
    cJSON_AddBoolToObject(out, "success", true);
    cJSON_AddStringToObject(response_data, "id", saved.id.c_str());
    cJSON_AddStringToObject(response_data, "label", saved.label.c_str());
    cJSON_AddStringToObject(response_data, "text", saved.text.c_str());
    cJSON_AddStringToObject(response_data, "reminder_id", saved.reminder_id.c_str());
    cJSON_AddNumberToObject(response_data, "duration_ms", saved.duration_ms);
    cJSON_AddNumberToObject(response_data, "size_bytes", static_cast<double>(saved.size_bytes));
    cJSON_AddItemToObject(out, "data", response_data);

    ESP_LOGI("CARE_WEB", "Voice recording created id=%s reminder=%s",
             saved.id.c_str(), saved.reminder_id.c_str());
    return SendRoot(req, out, "201 Created");
}

esp_err_t CareWebServer::HandleVoiceRecordingUpdate(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, true)) return ESP_OK;

    std::string id;
    if (!ExtractId(req, "/api/voice-recordings/", id)) {
        return SendNamedError(req, "400 Bad Request", "INVALID_ID");
    }

    std::string body;
    if (ReadBody(req, body) != ESP_OK) {
        return SendNamedError(req, "400 Bad Request", "INVALID_REQUEST");
    }

    cJSON* root = cJSON_ParseWithLength(body.data(), body.size());
    if (!cJSON_IsObject(root)) {
        if (root) cJSON_Delete(root);
        return SendNamedError(req, "400 Bad Request", "INVALID_JSON");
    }
    const std::string reminder_id = ReadString(root, "reminder_id");
    cJSON_Delete(root);

    if (!reminder_id.empty() && !ValidateVoiceTargetId(reminder_id)) {
        return SendNamedError(req, "404 Not Found", "REMINDER_NOT_FOUND");
    }

    std::string error;
    auto& store = voice::VoiceRecordingStore::GetInstance();
    if (!store.AssignToReminder(id, reminder_id, error)) {
        if (error == "NOT_FOUND") return SendNamedError(req, "404 Not Found", error.c_str());
        if (error == "REMINDER_ALREADY_HAS_AUDIO") return SendNamedError(req, "409 Conflict", error.c_str());
        return SendNamedError(req, "500 Internal Server Error",
                              error.empty() ? "VOICE_ASSIGNMENT_FAILED" : error.c_str());
    }

    return SendJsonText(req, "200 OK", "{\"success\":true}");
}


esp_err_t CareWebServer::HandleVoiceRecordingAudio(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, false)) {
        return ESP_OK;
    }

    std::string uri = req ? std::string(req->uri) : std::string();

    // El navegador puede agregar ?_=timestamp. Lo quitamos para validar bien el sufijo /audio.
    const size_t query_pos = uri.find('?');
    if (query_pos != std::string::npos) {
        uri = uri.substr(0, query_pos);
    }

    const std::string prefix = "/api/voice-recordings/";
    const std::string suffix = "/audio";

    if (uri.size() <= prefix.size() + suffix.size() ||
        uri.compare(0, prefix.size(), prefix) != 0 ||
        uri.compare(uri.size() - suffix.size(), suffix.size(), suffix) != 0) {
        return SendNamedError(req, "400 Bad Request", "INVALID_ID");
    }

    const std::string id = uri.substr(prefix.size(), uri.size() - prefix.size() - suffix.size());
    if (id.empty()) {
        return SendNamedError(req, "400 Bad Request", "INVALID_ID");
    }

    auto& store = voice::VoiceRecordingStore::GetInstance();
    if (!store.Init()) {
        return SendNamedError(req, "503 Service Unavailable", "VOICE_STORAGE_NOT_READY");
    }

    std::string audio;
    voice::VoiceRecordingInfo info;
    if (!store.LoadAudio(id, audio, &info) || audio.empty()) {
        return SendNamedError(req, "404 Not Found", "NOT_FOUND");
    }

    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_type(req, "audio/ogg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline");

    return httpd_resp_send(req, audio.data(), audio.size());
}

esp_err_t CareWebServer::HandleVoiceRecordingDelete(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, true)) return ESP_OK;

    std::string id;
    if (!ExtractId(req, "/api/voice-recordings/", id)) {
        return SendNamedError(req, "400 Bad Request", "INVALID_ID");
    }

    std::string error;
    if (!voice::VoiceRecordingStore::GetInstance().Delete(id, &error)) {
        if (error == "NOT_FOUND") return SendNamedError(req, "404 Not Found", error.c_str());
        if (error == "AUDIO_ASSIGNED_TO_REMINDER") {
            return SendNamedError(req, "409 Conflict", error.c_str());
        }
        return SendNamedError(req, "500 Internal Server Error",
                              error.empty() ? "VOICE_DELETE_FAILED" : error.c_str());
    }

    return SendJsonText(req, "200 OK", "{\"success\":true}");
}


esp_err_t CareWebServer::HandleMaintenanceStatus(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, false)) return ESP_OK;

    cJSON* root = cJSON_CreateObject();
    cJSON* data = cJSON_CreateObject();
    cJSON* usage = cJSON_CreateObject();
    if (root == nullptr || data == nullptr || usage == nullptr) {
        if (root) cJSON_Delete(root);
        if (data) cJSON_Delete(data);
        if (usage) cJSON_Delete(usage);
        return SendJsonText(req, "500 Internal Server Error", R"({"success":false,"error":"INTERNAL_ERROR"})");
    }

    std::vector<CarePerson> people;
    std::vector<CarePreference> preferences;
    std::vector<CarePillboxEntry> legacy_pillbox;
    std::vector<CareReminder> reminders;
    std::vector<voice::VoiceRecordingInfo> voice_recordings;

    CareManager::GetInstance().ListPeople(people);
    CareManager::GetInstance().ListPreferences(preferences);
    CareManager::GetInstance().ListPillboxEntries(legacy_pillbox);
    CareManager::GetInstance().ListReminders(reminders);
    auto& voice_store = voice::VoiceRecordingStore::GetInstance();
    if (voice_store.Init()) voice_recordings = voice_store.List();

    daily::NvsRoutineRepository routine_repo;
    std::vector<daily::CareRoutine> routines;
    if (routine_repo.Init()) routines = routine_repo.List();

    daily::NvsRoutineExecutionRepository execution_repo;
    std::vector<daily::RoutineExecution> executions;
    if (execution_repo.Init()) executions = execution_repo.List();

    auto& family_repo = family::GetFamilyRelationshipRepository();
    family_repo.Init();
    std::vector<family::FamilyRelationship> family = family_repo.List();

    AddUsageObject(usage, "people", static_cast<int>(people.size()), static_cast<int>(kMaxPeople));
    AddUsageObject(usage, "preferences", static_cast<int>(preferences.size()), static_cast<int>(kMaxPreferences));
    AddUsageObject(usage, "legacy_pillbox", static_cast<int>(legacy_pillbox.size()), static_cast<int>(kMaxPillboxEntries));
    AddUsageObject(usage, "reminders", static_cast<int>(reminders.size()), static_cast<int>(kMaxReminders));
    AddUsageObject(usage, "voice_recordings", static_cast<int>(voice_recordings.size()),
                   static_cast<int>(voice_store.Limits().max_recordings));
    AddUsageObject(usage, "routines", static_cast<int>(routines.size()), 64);
    AddUsageObject(usage, "executions", static_cast<int>(executions.size()), 128);
    AddUsageObject(usage, "family", static_cast<int>(family.size()), 64);

    nvs_stats_t stats = {};
    esp_err_t stats_err = nvs_get_stats(nullptr, &stats);
    cJSON* nvs = cJSON_CreateObject();
    if (nvs != nullptr) {
        cJSON_AddBoolToObject(nvs, "available", stats_err == ESP_OK);
        if (stats_err == ESP_OK) {
            cJSON_AddNumberToObject(nvs, "used_entries", stats.used_entries);
            cJSON_AddNumberToObject(nvs, "free_entries", stats.free_entries);
            cJSON_AddNumberToObject(nvs, "total_entries", stats.total_entries);
            cJSON_AddNumberToObject(nvs, "namespace_count", stats.namespace_count);
            cJSON_AddBoolToObject(nvs, "low_space", stats.free_entries < 32);
        } else {
            cJSON_AddStringToObject(nvs, "error", esp_err_to_name(stats_err));
        }
        cJSON_AddItemToObject(data, "nvs", nvs);
    }

    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddItemToObject(data, "usage", usage);
    cJSON_AddItemToObject(root, "data", data);
    return SendRoot(req, root);
}

esp_err_t CareWebServer::HandleMaintenanceClearExecutions(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, true)) return ESP_OK;

    daily::NvsRoutineExecutionRepository repo;
    if (!repo.Init()) {
        return SendJsonText(req, "500 Internal Server Error", R"({"success":false,"error":"STORAGE_ERROR"})");
    }

    std::vector<daily::RoutineExecution> executions = repo.List();
    int deleted = 0;
    for (const auto& execution : executions) {
        if (repo.Delete(execution.id)) {
            ++deleted;
        }
    }

    cJSON* root = cJSON_CreateObject();
    cJSON* data = cJSON_CreateObject();
    if (root == nullptr || data == nullptr) {
        if (root) cJSON_Delete(root);
        if (data) cJSON_Delete(data);
        return SendJsonText(req, "500 Internal Server Error", R"({"success":false,"error":"INTERNAL_ERROR"})");
    }
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddNumberToObject(data, "deleted", deleted);
    cJSON_AddItemToObject(root, "data", data);
    ESP_LOGI("CARE_WEB", "Maintenance cleared %d routine execution event(s)", deleted);
    return SendRoot(req, root);
}

esp_err_t CareWebServer::HandleMaintenanceClearLegacyPillbox(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, true)) return ESP_OK;

    std::vector<CarePillboxEntry> entries;
    CareError result = CareManager::GetInstance().ListPillboxEntries(entries);
    if (result != CareError::OK) {
        return SendError(req, HttpStatusFor(result), result);
    }

    int deleted = 0;
    for (const auto& entry : entries) {
        if (!entry.id.empty() && CareManager::GetInstance().DeletePillboxEntry(entry.id) == CareError::OK) {
            ++deleted;
        }
    }

    cJSON* root = cJSON_CreateObject();
    cJSON* data = cJSON_CreateObject();
    if (root == nullptr || data == nullptr) {
        if (root) cJSON_Delete(root);
        if (data) cJSON_Delete(data);
        return SendJsonText(req, "500 Internal Server Error", R"({"success":false,"error":"INTERNAL_ERROR"})");
    }
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddNumberToObject(data, "deleted", deleted);
    cJSON_AddItemToObject(root, "data", data);
    ESP_LOGI("CARE_WEB", "Maintenance cleared %d legacy pillbox entrie(s)", deleted);
    return SendRoot(req, root);
}

}  // namespace xiaozhi_care

