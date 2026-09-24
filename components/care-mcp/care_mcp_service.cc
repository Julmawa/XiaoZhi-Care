#include "care_mcp_service.h"

#include <cJSON.h>
#include <esp_log.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <optional>
#include <mutex>
#include <string>
#include <vector>

#include "care_manager.h"
#include "care_daily/daily_routine_engine.h"
#include "care_daily/nvs_routine_repository.h"
#include "care_daily/nvs_routine_execution_repository.h"
#include "care_family/nvs_family_relationship_repository.h"

#include "care_news/news_settings.h"
namespace xiaozhi_care {

// DP034_R1_MCP_RAM_CACHE
// Cache corto en RAM para evitar releer /care_data en consultas MCP consecutivas.
// TTL: 120 segundos. Si se editan datos desde el panel, como máximo tarda ese tiempo en refrescar.
namespace {

// DP034_R4_FIX1_LONG_TTL
// El panel invalida el cache inmediatamente al modificar datos.
// 24 h queda solo como red de seguridad ante cambios por otras rutas.
constexpr uint32_t kDp034McpCacheTtlMs = 86400000;

struct Dp034McpMemoryCache {
    // DP034_R4_THREAD_SAFE_CACHE
    std::mutex mutex;

    bool people_valid = false;
    uint32_t people_loaded_ms = 0;
    std::vector<CarePerson> people;

    bool preferences_valid = false;
    uint32_t preferences_loaded_ms = 0;
    std::vector<CarePreference> preferences;
};

static Dp034McpMemoryCache g_dp034_mcp_cache;

static bool Dp034CacheFresh(uint32_t loaded_ms) {
    if (loaded_ms == 0) {
        return false;
    }
    const uint32_t now_ms = esp_log_timestamp();
    return (uint32_t)(now_ms - loaded_ms) <= kDp034McpCacheTtlMs;
}

static CareError CareMcpListPeopleCached(std::vector<CarePerson>& out) {
    const uint32_t t0_ms = esp_log_timestamp();

    std::lock_guard<std::mutex> lock(g_dp034_mcp_cache.mutex);

    if (g_dp034_mcp_cache.people_valid &&
        Dp034CacheFresh(g_dp034_mcp_cache.people_loaded_ms)) {
        out = g_dp034_mcp_cache.people;
        ESP_LOGI("CARE_MCP_CACHE", "people hit count=%u elapsed_ms=%lu",
                 (unsigned)out.size(),
                 (unsigned long)(esp_log_timestamp() - t0_ms));
        return CareError::OK;
    }

    std::vector<CarePerson> loaded;
    const CareError err = CareManager::GetInstance().ListPeople(loaded);

    if (err == CareError::OK) {
        g_dp034_mcp_cache.people = loaded;
        g_dp034_mcp_cache.people_loaded_ms = esp_log_timestamp();
        g_dp034_mcp_cache.people_valid = true;
        out = g_dp034_mcp_cache.people;

        ESP_LOGI("CARE_MCP_CACHE", "people miss count=%u elapsed_ms=%lu",
                 (unsigned)out.size(),
                 (unsigned long)(esp_log_timestamp() - t0_ms));
    } else {
        ESP_LOGW("CARE_MCP_CACHE", "people load_error=%d elapsed_ms=%lu",
                 (int)err,
                 (unsigned long)(esp_log_timestamp() - t0_ms));
    }

    return err;
}

static CareError CareMcpListPreferencesCached(std::vector<CarePreference>& out) {
    const uint32_t t0_ms = esp_log_timestamp();

    std::lock_guard<std::mutex> lock(g_dp034_mcp_cache.mutex);

    if (g_dp034_mcp_cache.preferences_valid &&
        Dp034CacheFresh(g_dp034_mcp_cache.preferences_loaded_ms)) {
        out = g_dp034_mcp_cache.preferences;
        ESP_LOGI("CARE_MCP_CACHE", "preferences hit count=%u elapsed_ms=%lu",
                 (unsigned)out.size(),
                 (unsigned long)(esp_log_timestamp() - t0_ms));
        return CareError::OK;
    }

    std::vector<CarePreference> loaded;
    const CareError err = CareManager::GetInstance().ListPreferences(loaded);

    if (err == CareError::OK) {
        g_dp034_mcp_cache.preferences = loaded;
        g_dp034_mcp_cache.preferences_loaded_ms = esp_log_timestamp();
        g_dp034_mcp_cache.preferences_valid = true;
        out = g_dp034_mcp_cache.preferences;

        ESP_LOGI("CARE_MCP_CACHE", "preferences miss count=%u elapsed_ms=%lu",
                 (unsigned)out.size(),
                 (unsigned long)(esp_log_timestamp() - t0_ms));
    } else {
        ESP_LOGW("CARE_MCP_CACHE", "preferences load_error=%d elapsed_ms=%lu",
                 (int)err,
                 (unsigned long)(esp_log_timestamp() - t0_ms));
    }

    return err;
}


// DP034_R4_CACHE_COHERENCE
static void Dp034InvalidatePeopleCacheInternal() {
    std::lock_guard<std::mutex> lock(g_dp034_mcp_cache.mutex);

    g_dp034_mcp_cache.people_valid = false;
    g_dp034_mcp_cache.people_loaded_ms = 0;
    g_dp034_mcp_cache.people.clear();

    ESP_LOGI("CARE_MCP_CACHE", "people invalidated");
}

static void Dp034InvalidatePreferencesCacheInternal() {
    std::lock_guard<std::mutex> lock(g_dp034_mcp_cache.mutex);

    g_dp034_mcp_cache.preferences_valid = false;
    g_dp034_mcp_cache.preferences_loaded_ms = 0;
    g_dp034_mcp_cache.preferences.clear();

    ESP_LOGI("CARE_MCP_CACHE", "preferences invalidated");
}

}  // namespace


void CareMcpService::PreloadCache() {
    const uint32_t t0_ms = esp_log_timestamp();

    std::vector<CarePerson> people;
    std::vector<CarePreference> preferences;

    const CareError people_err = CareMcpListPeopleCached(people);
    const CareError preferences_err =
        CareMcpListPreferencesCached(preferences);

    const uint32_t elapsed_ms = esp_log_timestamp() - t0_ms;

    if (people_err == CareError::OK &&
        preferences_err == CareError::OK) {

        ESP_LOGI(
            "CARE_MCP_CACHE",
            "preload ready people=%u preferences=%u elapsed_ms=%lu",
            static_cast<unsigned>(people.size()),
            static_cast<unsigned>(preferences.size()),
            static_cast<unsigned long>(elapsed_ms)
        );
    } else {
        ESP_LOGW(
            "CARE_MCP_CACHE",
            "preload partial people_err=%d preferences_err=%d elapsed_ms=%lu",
            static_cast<int>(people_err),
            static_cast<int>(preferences_err),
            static_cast<unsigned long>(elapsed_ms)
        );
    }
}

void CareMcpService::InvalidatePeopleCache() {
    Dp034InvalidatePeopleCacheInternal();
}

void CareMcpService::InvalidatePreferencesCache() {
    Dp034InvalidatePreferencesCacheInternal();
}


namespace {

constexpr size_t kMaxMcpItems = 12;
constexpr time_t kMinTrustedEpoch = 1704067200;  // 2024-01-01 UTC

std::string Stringify(cJSON* root) {
    if (root == nullptr) {
        return R"({"ok":false,"error":"INTERNAL_ERROR"})";
    }
    char* raw = cJSON_PrintUnformatted(root);
    std::string out = raw != nullptr ? raw : R"({"ok":false,"error":"INTERNAL_ERROR"})";
    if (raw != nullptr) {
        cJSON_free(raw);
    }
    cJSON_Delete(root);
    return out;
}

std::string ErrorJson(CareError error) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", false);
    cJSON_AddStringToObject(root, "error", CareErrorToString(error));
    return Stringify(root);
}

std::string ErrorJson(const char* error) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", false);
    cJSON_AddStringToObject(root, "error", error != nullptr ? error : "INTERNAL_ERROR");
    return Stringify(root);
}

void AddStringIfNotEmpty(cJSON* object, const char* key, const std::string& value) {
    if (object != nullptr && !value.empty()) {
        cJSON_AddStringToObject(object, key, value.c_str());
    }
}

void AppendNormalizedCodepoint(std::string& out, const unsigned char* bytes, size_t length) {
    if (length == 1) {
        unsigned char c = bytes[0];
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<unsigned char>(c - 'A' + 'a');
        }
        out.push_back(static_cast<char>(c));
        return;
    }

    if (length == 2 && bytes[0] == 0xC3) {
        switch (bytes[1]) {
            case 0x81: case 0xA1: case 0x80: case 0xA0: case 0x84: case 0xA4:
                out.push_back('a'); return;
            case 0x89: case 0xA9: case 0x88: case 0xA8: case 0x8B: case 0xAB:
                out.push_back('e'); return;
            case 0x8D: case 0xAD: case 0x8C: case 0xAC: case 0x8F: case 0xAF:
                out.push_back('i'); return;
            case 0x93: case 0xB3: case 0x92: case 0xB2: case 0x96: case 0xB6:
                out.push_back('o'); return;
            case 0x9A: case 0xBA: case 0x99: case 0xB9: case 0x9C: case 0xBC:
                out.push_back('u'); return;
            case 0x91: case 0xB1:
                out.push_back('n'); return;
            default:
                break;
        }
    }

    out.append(reinterpret_cast<const char*>(bytes), length);
}

std::string Normalize(const std::string& value) {
    std::string out;
    out.reserve(value.size());

    const auto* bytes = reinterpret_cast<const unsigned char*>(value.data());
    bool previous_space = true;
    size_t i = 0;
    while (i < value.size()) {
        const unsigned char c = bytes[i];
        size_t length = 1;
        if ((c & 0xE0) == 0xC0) length = 2;
        else if ((c & 0xF0) == 0xE0) length = 3;
        else if ((c & 0xF8) == 0xF0) length = 4;

        if (i + length > value.size()) {
            break;
        }

        if (length == 1 && std::isspace(c)) {
            if (!previous_space) {
                out.push_back(' ');
                previous_space = true;
            }
            ++i;
            continue;
        }

        const size_t before = out.size();
        AppendNormalizedCodepoint(out, bytes + i, length);
        if (out.size() > before) {
            previous_space = false;
        }
        i += length;
    }

    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

bool IsLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int DaysInMonth(int year, int month) {
    static constexpr int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 0;
    if (month == 2 && IsLeapYear(year)) return 29;
    return kDays[month - 1];
}

bool ParseDate(const std::string& value, int& year, int& month, int& day) {
    if (value.size() != 10 || value[4] != '-' || value[7] != '-') {
        return false;
    }
    for (size_t i = 0; i < value.size(); ++i) {
        if (i == 4 || i == 7) continue;
        if (!std::isdigit(static_cast<unsigned char>(value[i]))) return false;
    }
    year = std::stoi(value.substr(0, 4));
    month = std::stoi(value.substr(5, 2));
    day = std::stoi(value.substr(8, 2));
    if (year < 1900 || year > 2200 || month < 1 || month > 12) return false;
    return day >= 1 && day <= DaysInMonth(year, month);
}

int IsoWeekday(int year, int month, int day) {
    // Sakamoto algorithm: 0=Sunday ... 6=Saturday.
    static constexpr int kOffsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3) --year;
    const int sunday_zero =
        (year + year / 4 - year / 100 + year / 400 + kOffsets[month - 1] + day) % 7;
    return sunday_zero == 0 ? 7 : sunday_zero;
}

std::string FormatDate(int year, int month, int day) {
    char buffer[40] = {};
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", year, month, day);
    return std::string(buffer);
}

bool GetLocalClock(std::tm& local_tm) {
    const time_t now = std::time(nullptr);
    if (now < kMinTrustedEpoch) {
        return false;
    }
#if defined(_WIN32)
    return localtime_s(&local_tm, &now) == 0;
#else
    return localtime_r(&now, &local_tm) != nullptr;
#endif
}


std::string CanonicalPreferenceCategory(const std::string& value) {
    const std::string c = Normalize(value);
    if (c == "music" || c == "musica" || c == "musicas" || c == "cancion" ||
        c == "canciones" || c == "genero musical" || c == "musica favorita") return "music";
    if (c == "hobby" || c == "hobbies" || c == "pasatiempo" || c == "pasatiempos" ||
        c == "actividad favorita" || c == "actividades favoritas" ||
        c == "gusto" || c == "gustos" || c == "le gusta" || c == "que le gusta" ||
        c == "que le gusta hacer" || c == "le gusta hacer" ||
        c == "le encanta" || c == "encanta" || c == "que le encanta" ||
        c == "que le encanta hacer" || c == "le encanta hacer" ||
        c == "cosas que le gustan" || c == "likes") return "hobby";
    if (c == "food" || c == "comida" || c == "comidas" || c == "alimento" ||
        c == "alimentos" || c == "comida favorita") return "food";
    if (c == "television" || c == "tv" || c == "tele" || c == "programa" ||
        c == "programas" || c == "series") return "television";
    if (c == "pets" || c == "pet" || c == "mascota" || c == "mascotas" ||
        c == "animal" || c == "animales" || c == "perro" || c == "perros" ||
        c == "gato" || c == "gatos") return "pets";
    if (c == "habits" || c == "habit" || c == "habito" || c == "habitos" ||
        c == "costumbre" || c == "costumbres" || c == "rutina cotidiana" ||
        c == "rutinas cotidianas" || c == "suele hacer" || c == "que suele hacer" ||
        c == "que hace" || c == "hacer habitualmente" || c == "habitualmente" ||
        c == "siesta" || c == "dormir la siesta" || c == "duerme la siesta" ||
        c == "duerme en siesta" || c == "dormir en siesta") return "habits";
    if (c == "care" || c == "cuidado" || c == "cuidados" || c == "cuidados generales") return "care";
    if (c == "painting" || c == "pintura" || c == "pintar") return "painting";
    if (c == "crafts" || c == "artesania" || c == "artesanias" ||
        c == "manualidad" || c == "manualidades") return "crafts";
    return c;
}

bool IsProfileOwnerQuery(const std::string& owner) {
    const std::string o = Normalize(owner);
    return o.empty() || o == "profile" || o == "perfil" || o == "user" ||
           o == "usuario" || o == "usuario principal" || o == "yo" || o == "me" ||
           o == "mi" || o == "mis";
}


size_t EditDistance(const std::string& a, const std::string& b) {
    if (a.empty()) return b.size();
    if (b.empty()) return a.size();

    std::vector<size_t> previous(b.size() + 1);
    std::vector<size_t> current(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) previous[j] = j;

    for (size_t i = 1; i <= a.size(); ++i) {
        current[0] = i;
        for (size_t j = 1; j <= b.size(); ++j) {
            const size_t substitution = previous[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
            current[j] = std::min({previous[j] + 1, current[j - 1] + 1, substitution});
        }
        previous.swap(current);
    }
    return previous[b.size()];
}

struct SimilarPerson {
    CarePerson person;
    size_t distance = 0;
};

bool IsCloseName(const std::string& query, const std::string& candidate, size_t& distance) {
    const std::string q = Normalize(query);
    const std::string c = Normalize(candidate);
    if (q.empty() || c.empty() || q == c) return false;

    distance = EditDistance(q, c);
    const size_t longest = std::max(q.size(), c.size());
    size_t allowed = 1;
    if (longest >= 6) allowed = 2;
    if (longest >= 10) allowed = 3;
    return distance <= allowed;
}

std::vector<SimilarPerson> FindSimilarPeople(const std::string& query) {
    std::vector<CarePerson> all;
    if (CareMcpListPeopleCached(all) != CareError::OK) return {};

    std::vector<SimilarPerson> out;
    for (const auto& person : all) {
        if (!person.enabled) continue;
        size_t best = static_cast<size_t>(-1);
        auto consider = [&](const std::string& text) {
            size_t d = 0;
            if (IsCloseName(query, text, d)) best = std::min(best, d);
        };
        consider(person.name);
        consider(person.nickname);
        for (const auto& alias : person.aliases) consider(alias);
        if (best != static_cast<size_t>(-1)) out.push_back({person, best});
    }

    std::sort(out.begin(), out.end(), [](const SimilarPerson& a, const SimilarPerson& b) {
        if (a.distance != b.distance) return a.distance < b.distance;
        return Normalize(a.person.name) < Normalize(b.person.name);
    });
    if (out.size() > 3) out.resize(3);
    return out;
}

void AddSimilarPeopleDiagnostic(cJSON* root, const std::string& query) {
    const auto similar = FindSimilarPeople(query);
    cJSON_AddNumberToObject(root, "similar_name_count", static_cast<double>(similar.size()));
    if (similar.empty()) return;

    cJSON_AddBoolToObject(root, "clarification_required", true);
    cJSON_AddStringToObject(root, "diagnostic", "SIMILAR_NAME_FOUND");
    cJSON_AddStringToObject(root, "instruction",
                            "Do not choose automatically. Ask the user briefly if they meant one of the similar people, then retry with the confirmed exact name or nickname.");
    cJSON* candidates = cJSON_AddArrayToObject(root, "similar_name_candidates");
    for (const auto& candidate : similar) {
        cJSON* item = cJSON_CreateObject();
        AddStringIfNotEmpty(item, "name", candidate.person.name);
        AddStringIfNotEmpty(item, "nickname", candidate.person.nickname);
        AddStringIfNotEmpty(item, "relationship", candidate.person.relationship);
        cJSON_AddNumberToObject(item, "distance", static_cast<double>(candidate.distance));
        cJSON_AddItemToArray(candidates, item);
    }
}

std::string PeriodForHour(int hour) {
    if (hour >= 5 && hour <= 10) return "morning";
    if (hour >= 11 && hour <= 13) return "noon";
    if (hour >= 14 && hour <= 17) return "afternoon";
    if (hour >= 18 && hour <= 20) return "evening";
    return "night";
}

bool IsAllowedPeriod(const std::string& period) {
    return period == "morning" || period == "noon" || period == "afternoon" ||
           period == "evening" || period == "night";
}

std::string CanonicalPersonField(const std::string& field) {
    const std::string f = Normalize(field);
    if (f.empty() || f == "basic" || f == "basico") return "basic";
    if (f == "birthday" || f == "cumpleanos" || f == "fecha de nacimiento") return "birthday";
    if (f == "phone" || f == "telefono") return "phone";
    if (f == "address" || f == "direccion") return "address";
    return "";
}

std::string CanonicalProfileField(const std::string& field) {
    const std::string f = Normalize(field);
    if (f.empty() || f == "basic" || f == "basico") return "basic";
    if (f == "birthday" || f == "cumpleanos" || f == "fecha de nacimiento") return "birthday";
    if (f == "city" || f == "ciudad") return "city";
    if (f == "timezone" || f == "zona horaria") return "timezone";
    return "";
}

bool ReminderOccursOn(const CareReminder& reminder, const std::string& target_date) {
    int ty = 0, tm = 0, td = 0;
    int ry = 0, rm = 0, rd = 0;
    if (!ParseDate(target_date, ty, tm, td) || !ParseDate(reminder.date, ry, rm, rd)) {
        return false;
    }

    // Recurrences start on their configured date and do not fire before it.
    if (target_date < reminder.date) {
        return false;
    }

    if (reminder.recurrence.empty() || reminder.recurrence == "none") {
        return target_date == reminder.date;
    }
    if (reminder.recurrence == "daily") {
        return true;
    }
    if (reminder.recurrence == "weekly") {
        return IsoWeekday(ty, tm, td) == IsoWeekday(ry, rm, rd);
    }
    if (reminder.recurrence == "monthly") {
        return td == rd;
    }
    if (reminder.recurrence == "yearly") {
        return tm == rm && td == rd;
    }
    return false;
}



struct SmartReminderMatch {
    const CareReminder* reminder{nullptr};
    std::string occurrence_date;
    int days_until{0};
    int remember_before_minutes{0};
    bool from_memory_window{false};
};

int DaysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int>(doe) - 719468;
}

bool AddDaysToDate(const std::string& date, int offset, std::string& out) {
    int y = 0, m = 0, d = 0;
    if (!ParseDate(date, y, m, d)) return false;

    std::tm tm_value{};
    tm_value.tm_year = y - 1900;
    tm_value.tm_mon = m - 1;
    tm_value.tm_mday = d + offset;
    tm_value.tm_hour = 12;

    if (std::mktime(&tm_value) == static_cast<std::time_t>(-1)) {
        return false;
    }

    out = FormatDate(tm_value.tm_year + 1900, tm_value.tm_mon + 1, tm_value.tm_mday);
    return true;
}

int DaysBetweenDates(const std::string& from_date, const std::string& to_date) {
    int fy = 0, fm = 0, fd = 0;
    int ty = 0, tm = 0, td = 0;
    if (!ParseDate(from_date, fy, fm, fd) || !ParseDate(to_date, ty, tm, td)) {
        return 99999;
    }
    return DaysFromCivil(ty, static_cast<unsigned>(tm), static_cast<unsigned>(td)) -
           DaysFromCivil(fy, static_cast<unsigned>(fm), static_cast<unsigned>(fd));
}

int ReadRememberBeforeMinutes(const std::string& notes) {
    const std::string tag = "[[remember_before_minutes:";
    const size_t start = notes.find(tag);
    if (start == std::string::npos) return 0;

    size_t pos = start + tag.size();
    int value = 0;
    bool has_digit = false;

    while (pos < notes.size() && notes[pos] >= '0' && notes[pos] <= '9') {
        has_digit = true;
        value = value * 10 + (notes[pos] - '0');
        ++pos;
    }

    if (!has_digit) return 0;
    if (value < 0) return 0;
    if (value > 10080) return 10080;
    return value;
}

std::string ReminderWhenText(int days_until, const std::string& time) {
    std::string text;
    if (days_until == 0) {
        text = "hoy";
    } else if (days_until == 1) {
        text = "mañana";
    } else {
        text = "en " + std::to_string(days_until) + " días";
    }

    if (!time.empty()) {
        text += " a las " + time;
    }

    return text;
}

std::string ReminderPersonLabel(const CareReminder& reminder) {
    if (reminder.related_person_id.empty()) {
        return "";
    }

    CarePerson person;
    if (CareManager::GetInstance().GetPerson(reminder.related_person_id, person) != CareError::OK ||
        !person.enabled) {
        return "";
    }

    if (!person.nickname.empty()) {
        return person.nickname;
    }

    return person.name;
}

std::string ReminderMemoryText(const CareReminder& reminder, int days_until) {
    const std::string person_label = ReminderPersonLabel(reminder);

    std::string text = "Acordate que ";
    text += ReminderWhenText(days_until, reminder.time);

    if (!person_label.empty()) {
        text += " viene ";
        text += person_label;
        text += ". Recordatorio: ";
        text += reminder.title;
        text += ".";
        return text;
    }

    text += " ";
    text += reminder.title;
    text += ".";
    return text;
}

std::string ReminderSummaryText(const CareReminder& reminder, int days_until) {
    const std::string person_label = ReminderPersonLabel(reminder);

    if (!person_label.empty()) {
        std::string text = person_label;
        text += " ";
        text += ReminderWhenText(days_until, reminder.time);
        text += " - ";
        text += reminder.title;
        return text;
    }

    std::string text = reminder.title;
    text += " ";
    text += ReminderWhenText(days_until, reminder.time);
    return text;
}


bool ParseDailyTimeText(const std::string& value, daily::DailyTime& out) {
    if (value.size() != 5 || value[2] != ':') {
        return false;
    }
    if (!std::isdigit(static_cast<unsigned char>(value[0])) ||
        !std::isdigit(static_cast<unsigned char>(value[1])) ||
        !std::isdigit(static_cast<unsigned char>(value[3])) ||
        !std::isdigit(static_cast<unsigned char>(value[4]))) {
        return false;
    }

    const uint8_t hour = static_cast<uint8_t>((value[0] - '0') * 10 + (value[1] - '0'));
    const uint8_t minute = static_cast<uint8_t>((value[3] - '0') * 10 + (value[4] - '0'));
    out = daily::DailyTime(hour, minute);
    return out.IsValid();
}

std::string FormatDailyTimeText(const daily::DailyTime& time) {
    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "%02u:%02u",
                  static_cast<unsigned>(time.hour),
                  static_cast<unsigned>(time.minute));
    return std::string(buffer);
}

const char* RoutineTypeName(daily::RoutineType type) {
    switch (type) {
        case daily::RoutineType::Medication:
            return "medication";
        case daily::RoutineType::Hydration:
            return "hydration";
        case daily::RoutineType::Exercise:
            return "exercise";
        case daily::RoutineType::HealthMeasurement:
            return "health_measurement";
        case daily::RoutineType::Reminder:
            return "reminder";
        case daily::RoutineType::Birthday:
            return "birthday";
        case daily::RoutineType::Custom:
        default:
            return "custom";
    }
}

const char* RoutineMomentName(daily::RoutineMoment moment) {
    switch (moment) {
        case daily::RoutineMoment::Fasting:
            return "fasting";
        case daily::RoutineMoment::BeforeBreakfast:
            return "antes del desayuno";
        case daily::RoutineMoment::WithBreakfast:
            return "with_breakfast";
        case daily::RoutineMoment::AfterBreakfast:
            return "después del desayuno";
        case daily::RoutineMoment::BeforeLunch:
            return "antes del almuerzo";
        case daily::RoutineMoment::WithLunch:
            return "with_lunch";
        case daily::RoutineMoment::AfterLunch:
            return "después del almuerzo";
        case daily::RoutineMoment::BeforeDinner:
            return "antes de la cena";
        case daily::RoutineMoment::WithDinner:
            return "with_dinner";
        case daily::RoutineMoment::AfterDinner:
            return "después de la cena";
        case daily::RoutineMoment::Bedtime:
            return "bedtime";
        case daily::RoutineMoment::Anytime:
        default:
            return "anytime";
    }
}

const char* PlacementTypeName(daily::PlacementType type) {
    switch (type) {
        case daily::PlacementType::Pillbox:
            return "pillbox";
        case daily::PlacementType::Blister:
            return "blister";
        case daily::PlacementType::OriginalBox:
            return "original_box";
        case daily::PlacementType::Table:
            return "table";
        case daily::PlacementType::Shelf:
            return "shelf";
        case daily::PlacementType::Custom:
            return "custom";
        case daily::PlacementType::None:
        default:
            return "none";
    }
}

const char* ExecutionEventName(daily::RoutineExecutionEvent event) {
    switch (event) {
        case daily::RoutineExecutionEvent::Indicated:
            return "indicated";
        case daily::RoutineExecutionEvent::Confirmed:
            return "confirmed";
        case daily::RoutineExecutionEvent::Skipped:
            return "skipped";
        case daily::RoutineExecutionEvent::Missed:
            return "missed";
        case daily::RoutineExecutionEvent::Snoozed:
            return "snoozed";
        default:
            return "unknown";
    }
}

const char* ExecutionSourceName(daily::RoutineExecutionSource source) {
    switch (source) {
        case daily::RoutineExecutionSource::Engine:
            return "engine";
        case daily::RoutineExecutionSource::Voice:
            return "voice";
        case daily::RoutineExecutionSource::Web:
            return "web";
        case daily::RoutineExecutionSource::Hardware:
            return "hardware";
        case daily::RoutineExecutionSource::Test:
            return "test";
        default:
            return "unknown";
    }
}

bool ParseExecutionEvent(const std::string& value, daily::RoutineExecutionEvent& out) {
    const std::string normalized = Normalize(value);
    if (normalized == "confirmed" || normalized == "confirmada" ||
        normalized == "confirmado" || normalized == "done" ||
        normalized == "hecho" || normalized == "lo hice" ||
        normalized == "ya lo hice") {
        out = daily::RoutineExecutionEvent::Confirmed;
        return true;
    }
    if (normalized == "indicated" || normalized == "indicada" ||
        normalized == "indicado") {
        out = daily::RoutineExecutionEvent::Indicated;
        return true;
    }
    if (normalized == "skipped" || normalized == "omitida" ||
        normalized == "omitido" || normalized == "salteada") {
        out = daily::RoutineExecutionEvent::Skipped;
        return true;
    }
    if (normalized == "missed" || normalized == "perdida" ||
        normalized == "perdido" || normalized == "vencida") {
        out = daily::RoutineExecutionEvent::Missed;
        return true;
    }
    if (normalized == "snoozed" || normalized == "pospuesta" ||
        normalized == "pospuesto" || normalized == "despues" ||
        normalized == "mas tarde") {
        out = daily::RoutineExecutionEvent::Snoozed;
        return true;
    }
    return false;
}

void AddExecutionToJson(cJSON* array, const daily::RoutineExecution& execution) {
    if (array == nullptr) {
        return;
    }
    cJSON* item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "execution_id", execution.id.Str().c_str());
    cJSON_AddStringToObject(item, "routine_id", execution.routine_id.Str().c_str());
    cJSON_AddStringToObject(item, "event", ExecutionEventName(execution.event));
    cJSON_AddStringToObject(item, "source", ExecutionSourceName(execution.source));
    AddStringIfNotEmpty(item, "date", execution.iso_date);
    cJSON_AddStringToObject(item, "time", FormatDailyTimeText(execution.time).c_str());
    AddStringIfNotEmpty(item, "message", execution.message);
    cJSON_AddItemToArray(array, item);
}


constexpr const char* kProfileParticipantId = "profile";

struct FamilyParticipant {
    std::string id;
    std::string type;
    std::string name;
    std::string nickname;
    std::string relationship;
    bool has_pet = false;
    std::string pet_type;
    std::string pet_name;
};

bool IsProfileParticipantId(const std::string& id) {
    return Normalize(id) == "profile";
}

std::string ParticipantDisplayName(const FamilyParticipant& participant) {
    if (!participant.nickname.empty()) return participant.nickname;
    if (!participant.name.empty()) return participant.name;
    return participant.type == "profile" ? "usuaria principal" : "persona";
}

std::string ParticipantPetSummary(const FamilyParticipant& participant) {
    if (!participant.has_pet || participant.pet_name.empty()) return std::string();

    std::string summary;
    if (participant.pet_type == "dog") {
        summary = "El perro se llama ";
    } else if (participant.pet_type == "cat") {
        summary = "El gato se llama ";
    } else {
        summary = "La mascota se llama ";
    }

    summary += participant.pet_name;
    summary += ".";
    return summary;
}

void AddFamilyParticipantJson(cJSON* object, const char* key, const FamilyParticipant& participant) {
    cJSON* item = cJSON_AddObjectToObject(object, key);
    cJSON_AddStringToObject(item, "id", participant.id.c_str());
    cJSON_AddStringToObject(item, "type", participant.type.c_str());
    AddStringIfNotEmpty(item, "name", participant.name);
    AddStringIfNotEmpty(item, "nickname", participant.nickname);
    AddStringIfNotEmpty(item, "relationship", participant.relationship);
    cJSON_AddStringToObject(item, "display_name", ParticipantDisplayName(participant).c_str());
    cJSON_AddBoolToObject(item, "has_pet", participant.has_pet);
    AddStringIfNotEmpty(item, "pet_type", participant.pet_type);
    AddStringIfNotEmpty(item, "pet_name", participant.pet_name);
    AddStringIfNotEmpty(item, "pet_summary", ParticipantPetSummary(participant));
}

bool LoadParticipantById(const std::string& id, FamilyParticipant& participant) {
    if (IsProfileParticipantId(id)) {
        CareProfile profile;
        const CareError profile_result = CareManager::GetInstance().GetProfile(profile);
        participant.id = kProfileParticipantId;
        participant.type = "profile";
        participant.name = profile_result == CareError::OK ? profile.name : std::string();
        participant.nickname = profile_result == CareError::OK ? profile.nickname : std::string();
        participant.relationship = "usuaria principal";
        if (participant.name.empty() && participant.nickname.empty()) {
            participant.nickname = "Yolita";
        }
        return true;
    }

    CarePerson person;
    const CareError result = CareManager::GetInstance().GetPerson(id, person);
    if (result != CareError::OK || !person.enabled) {
        return false;
    }
    participant.id = person.id;
    participant.type = "person";
    participant.name = person.name;
    participant.nickname = person.nickname;
    participant.relationship = person.relationship;
    return true;
}

bool PersonTextMatches(const std::string& query, const CarePerson& person) {
    const std::string q = Normalize(query);
    if (q.empty()) return false;

    auto matches = [&](const std::string& value) {
        const std::string normalized = Normalize(value);
        if (normalized.empty()) return false;
        return normalized == q || normalized.find(q) != std::string::npos || q.find(normalized) != std::string::npos;
    };

    if (matches(person.id) || matches(person.name) || matches(person.nickname) || matches(person.relationship)) {
        return true;
    }
    for (const auto& alias : person.aliases) {
        if (matches(alias)) return true;
    }
    return false;
}

FamilyParticipant ParticipantFromPerson(const CarePerson& person) {
    FamilyParticipant candidate;
    candidate.id = person.id;
    candidate.type = "person";
    candidate.name = person.name;
    candidate.nickname = person.nickname;
    candidate.relationship = person.relationship;
    candidate.has_pet = person.has_pet;
    candidate.pet_type = person.pet_type;
    candidate.pet_name = person.pet_name;
    return candidate;
}

bool ResolveFamilyParticipant(const std::string& query,
                              FamilyParticipant& participant,
                              std::vector<FamilyParticipant>& candidates) {
    const std::string normalized_query = Normalize(query);

    CareProfile profile;
    const CareError profile_result = CareManager::GetInstance().GetProfile(profile);
    const std::string profile_name = profile_result == CareError::OK ? Normalize(profile.name) : std::string();
    const std::string profile_nickname = profile_result == CareError::OK ? Normalize(profile.nickname) : std::string();

    if (normalized_query.empty() || IsProfileOwnerQuery(query) || normalized_query == "usuaria principal" ||
        normalized_query == "usuario principal" || normalized_query == "yolita" ||
        normalized_query == "mi" || normalized_query == "yo" ||
        (!profile_name.empty() && normalized_query == profile_name) ||
        (!profile_nickname.empty() && normalized_query == profile_nickname)) {
        return LoadParticipantById(kProfileParticipantId, participant);
    }

    // DP034_R3_FIND_PERSON_CACHE
    // Resolver care.find_person usando cache corto en RAM.
    const uint32_t find_t0_ms = esp_log_timestamp();

    std::vector<CarePerson> people;
    std::vector<CarePerson> all_people;

    CareError result = CareMcpListPeopleCached(all_people);
    if (result == CareError::OK) {
        for (const auto& candidate : all_people) {
            if (!candidate.enabled) {
                continue;
            }
            if (PersonTextMatches(query, candidate)) {
                people.push_back(candidate);
            }
        }

        if (people.empty()) {
            result = CareError::NOT_FOUND;
        }
    }

    ESP_LOGI("CARE_MCP_FIND", "query=%s matches=%u elapsed_ms=%lu",
             query.c_str(),
             static_cast<unsigned>(people.size()),
             (unsigned long)(esp_log_timestamp() - find_t0_ms));
    if (result == CareError::OK && !people.empty()) {
        for (const auto& person : people) {
            if (!person.enabled) continue;
            candidates.push_back(ParticipantFromPerson(person));
        }
    }

    // Defensive fallback: some short nicknames such as "Ale" may not be
    // returned by the generic search depending on tokenization. For family
    // questions we scan enabled people directly and match name/nickname/aliases
    // with accent-insensitive partial matching.
    if (candidates.empty()) {
        std::vector<CarePerson> all_people;
        if (CareMcpListPeopleCached(all_people) == CareError::OK) {
            for (const auto& person : all_people) {
                if (!person.enabled) continue;
                if (PersonTextMatches(query, person)) {
                    candidates.push_back(ParticipantFromPerson(person));
                }
            }
        }
    }

    if (candidates.size() == 1) {
        participant = candidates.front();
        return true;
    }
    return false;
}

void AddRelationshipJson(cJSON* array, const family::FamilyRelationship& relationship) {
    if (array == nullptr || !relationship.enabled) return;

    FamilyParticipant from;
    FamilyParticipant to;
    const bool has_from = LoadParticipantById(relationship.from_person_id, from);
    const bool has_to = LoadParticipantById(relationship.to_person_id, to);
    if (!has_from || !has_to) return;

    cJSON* item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "relationship_id", relationship.id.value.c_str());
    cJSON_AddStringToObject(item, "type", relationship.type.c_str());
    AddStringIfNotEmpty(item, "label", relationship.label);
    AddFamilyParticipantJson(item, "from", from);
    AddFamilyParticipantJson(item, "to", to);

    const std::string from_name = ParticipantDisplayName(from);
    const std::string to_name = ParticipantDisplayName(to);
    const std::string relation_text = !relationship.label.empty() ? relationship.label : relationship.type;
    const std::string safe_message = from_name + " tiene una relación familiar anotada como " +
                                     relation_text + " de " + to_name + ".";
    cJSON_AddStringToObject(item, "safe_message", safe_message.c_str());
    cJSON_AddItemToArray(array, item);
}


}  // namespace

CareMcpService& CareMcpService::GetInstance() {
    static CareMcpService instance;
    return instance;
}

std::string CareMcpService::GetProfile(const std::string& field) {
    const std::string canonical = CanonicalProfileField(field);
    if (canonical.empty()) {
        return ErrorJson("INVALID_FIELD");
    }

    CareProfile profile;
    const CareError result = CareManager::GetInstance().GetProfile(profile);
    if (result == CareError::NOT_FOUND) {
        cJSON* root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "ok", true);
        cJSON_AddBoolToObject(root, "found", false);
        return Stringify(root);
    }
    if (result != CareError::OK) {
        return ErrorJson(result);
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddBoolToObject(root, "found", true);
    cJSON_AddStringToObject(root, "field", canonical.c_str());
    cJSON* data = cJSON_AddObjectToObject(root, "profile");

    if (canonical == "basic") {
        AddStringIfNotEmpty(data, "name", profile.name);
        AddStringIfNotEmpty(data, "nickname", profile.nickname);
    } else if (canonical == "birthday") {
        AddStringIfNotEmpty(data, "name", profile.name);
        AddStringIfNotEmpty(data, "birthday", profile.birthday);
    } else if (canonical == "city") {
        AddStringIfNotEmpty(data, "name", profile.name);
        AddStringIfNotEmpty(data, "city", profile.city);
    } else if (canonical == "timezone") {
        AddStringIfNotEmpty(data, "timezone", profile.timezone);
    }

    return Stringify(root);
}

std::string CareMcpService::FindPerson(const std::string& query, const std::string& field) {
    const std::string canonical = CanonicalPersonField(field);
    if (canonical.empty()) {
        return ErrorJson("INVALID_FIELD");
    }

    std::vector<CarePerson> people;
    const CareError result = CareManager::GetInstance().FindPeople(query, people);
    if (result == CareError::NOT_FOUND) {
        cJSON* root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "ok", true);
        cJSON_AddBoolToObject(root, "found", false);
        cJSON_AddStringToObject(root, "source", "xiaozhi_care");
        cJSON_AddBoolToObject(root, "do_not_infer", true);
        cJSON_AddStringToObject(root, "reason", "PERSON_NOT_FOUND");
        cJSON_AddNumberToObject(root, "match_count", 0);
        AddSimilarPeopleDiagnostic(root, query);
        const cJSON* similar_count = cJSON_GetObjectItemCaseSensitive(root, "similar_name_count");
        ESP_LOGI("CARE_MCP", "Person query: exact_matches=0 similar=%d field=%s",
                 cJSON_IsNumber(similar_count) ? similar_count->valueint : 0, canonical.c_str());
        return Stringify(root);
    }
    if (result != CareError::OK) {
        return ErrorJson(result);
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddBoolToObject(root, "found", true);
    cJSON_AddStringToObject(root, "field", canonical.c_str());
    cJSON_AddNumberToObject(root, "match_count", static_cast<double>(people.size()));
    const bool truncated = people.size() > kMaxMcpItems;
    cJSON_AddBoolToObject(root, "truncated", truncated);

    std::vector<CarePreference> all_preferences;
    const CareError pref_result = CareMcpListPreferencesCached(all_preferences);
    if (pref_result != CareError::OK) {
        all_preferences.clear();
    }

    cJSON* matches = cJSON_AddArrayToObject(root, "matches");
    const size_t count = std::min(people.size(), kMaxMcpItems);
    for (size_t i = 0; i < count; ++i) {
        const auto& person = people[i];
        cJSON* item = cJSON_CreateObject();
        AddStringIfNotEmpty(item, "name", person.name);
        AddStringIfNotEmpty(item, "nickname", person.nickname);
        AddStringIfNotEmpty(item, "relationship", person.relationship);

        std::string pet_preference;
        for (const auto& pref : all_preferences) {
            if (!pref.enabled || pref.owner_id != person.id) continue;
            if (CanonicalPreferenceCategory(pref.category) == "pets" && !pref.value.empty()) {
                pet_preference = pref.value;
                break;
            }
        }

        AddStringIfNotEmpty(item, "pet_preference", pet_preference);
        if (!pet_preference.empty()) {
            AddStringIfNotEmpty(item, "pet_summary", pet_preference);
        }

        cJSON_AddBoolToObject(item, "has_pet", person.has_pet);
        AddStringIfNotEmpty(item, "pet_type", person.pet_type);
        AddStringIfNotEmpty(item, "pet_name", person.pet_name);

        if (pet_preference.empty() && person.has_pet && !person.pet_name.empty()) {
            std::string pet_summary;
            if (person.pet_type == "dog") {
                pet_summary = "El perro se llama ";
            } else if (person.pet_type == "cat") {
                pet_summary = "El gato se llama ";
            } else {
                pet_summary = "La mascota se llama ";
            }
            pet_summary += person.pet_name;
            pet_summary += ".";
            AddStringIfNotEmpty(item, "pet_summary", pet_summary);
        }

        if (canonical == "birthday") {
            AddStringIfNotEmpty(item, "birthday", person.birthday);
        } else if (canonical == "phone") {
            AddStringIfNotEmpty(item, "phone", person.phone);
        } else if (canonical == "address") {
            AddStringIfNotEmpty(item, "address", person.address);
        }

        cJSON_AddItemToArray(matches, item);
    }
    return Stringify(root);
}

std::string CareMcpService::GetPreference(const std::string& category,
                                              const std::string& owner) {
    const std::string canonical_category = CanonicalPreferenceCategory(category);
    if (canonical_category.empty()) {
        return ErrorJson(CareError::INVALID_ARGUMENT);
    }

    std::string owner_id = "profile";
    std::string owner_type = "profile";
    CarePerson owner_person;
    bool has_owner_person = false;

    if (!IsProfileOwnerQuery(owner)) {
        // DP034_R2_OWNER_CACHE
        // Resolver persona/owner usando el cache corto en RAM, evitando FindPeople() directo.
        const uint32_t owner_t0_ms = esp_log_timestamp();

        std::vector<CarePerson> people;
        std::vector<CarePerson> all_people;

        CareError owner_result = CareMcpListPeopleCached(all_people);
        if (owner_result == CareError::OK) {
            for (const auto& candidate : all_people) {
                if (!candidate.enabled) {
                    continue;
                }
                if (PersonTextMatches(owner, candidate)) {
                    people.push_back(candidate);
                }
            }

            if (people.empty()) {
                owner_result = CareError::NOT_FOUND;
            }
        }

        ESP_LOGI("CARE_MCP_OWNER", "query=%s matches=%u elapsed_ms=%lu",
                 owner.c_str(),
                 static_cast<unsigned>(people.size()),
                 (unsigned long)(esp_log_timestamp() - owner_t0_ms));
        if (owner_result == CareError::NOT_FOUND) {
            cJSON* root = cJSON_CreateObject();
            cJSON_AddBoolToObject(root, "ok", true);
            cJSON_AddBoolToObject(root, "found", false);
            cJSON_AddStringToObject(root, "source", "xiaozhi_care");
            cJSON_AddBoolToObject(root, "do_not_infer", true);
            cJSON_AddStringToObject(root, "reason", "OWNER_NOT_FOUND");
            cJSON_AddStringToObject(root, "category", canonical_category.c_str());
            cJSON_AddNumberToObject(root, "match_count", 0);
            AddSimilarPeopleDiagnostic(root, owner);
            const cJSON* similar_count = cJSON_GetObjectItemCaseSensitive(root, "similar_name_count");
            ESP_LOGI("CARE_MCP", "Preference query: owner=not_found category=%s matches=0 similar=%d",
                     canonical_category.c_str(), cJSON_IsNumber(similar_count) ? similar_count->valueint : 0);
            return Stringify(root);
        }
        if (owner_result != CareError::OK) {
            return ErrorJson(owner_result);
        }
        if (people.size() != 1) {
            ESP_LOGI("CARE_MCP", "Preference query: owner=ambiguous category=%s owner_matches=%u",
                     canonical_category.c_str(), static_cast<unsigned>(people.size()));
            cJSON* root = cJSON_CreateObject();
            cJSON_AddBoolToObject(root, "ok", true);
            cJSON_AddBoolToObject(root, "found", false);
            cJSON_AddBoolToObject(root, "ambiguous_owner", true);
            cJSON_AddStringToObject(root, "source", "xiaozhi_care");
            cJSON_AddBoolToObject(root, "do_not_infer", true);
            cJSON_AddStringToObject(root, "category", canonical_category.c_str());
            cJSON_AddNumberToObject(root, "owner_match_count", static_cast<double>(people.size()));
            cJSON* candidates = cJSON_AddArrayToObject(root, "owner_candidates");
            const size_t count = std::min(people.size(), kMaxMcpItems);
            for (size_t i = 0; i < count; ++i) {
                cJSON* item = cJSON_CreateObject();
                AddStringIfNotEmpty(item, "name", people[i].name);
                AddStringIfNotEmpty(item, "nickname", people[i].nickname);
                AddStringIfNotEmpty(item, "relationship", people[i].relationship);
                cJSON_AddItemToArray(candidates, item);
            }
            return Stringify(root);
        }
        owner_person = people.front();
        owner_id = owner_person.id;
        owner_type = "person";
        has_owner_person = true;
    }

    std::vector<CarePreference> values;
    const CareError result = CareMcpListPreferencesCached(values);
    if (result != CareError::OK) {
        return ErrorJson(result);
    }

    std::vector<const CarePreference*> matches;
    for (const auto& value : values) {
        if (!value.enabled || value.owner_id != owner_id) continue;
        if (CanonicalPreferenceCategory(value.category) == canonical_category) {
            matches.push_back(&value);
        }
    }

    if (matches.empty() && owner_type == "person" && canonical_category == "pets" && has_owner_person &&
        owner_person.has_pet && !owner_person.pet_name.empty()) {
        cJSON* legacy_root = cJSON_CreateObject();
        cJSON_AddBoolToObject(legacy_root, "ok", true);
        cJSON_AddBoolToObject(legacy_root, "found", true);
        cJSON_AddStringToObject(legacy_root, "source", "xiaozhi_care");
        cJSON_AddBoolToObject(legacy_root, "do_not_infer", true);
        cJSON_AddStringToObject(legacy_root, "category", canonical_category.c_str());
        cJSON_AddStringToObject(legacy_root, "owner_type", owner_type.c_str());
        cJSON_AddBoolToObject(legacy_root, "legacy_pet_fallback", true);
        cJSON_AddBoolToObject(legacy_root, "not_a_general_like", true);
        cJSON_AddStringToObject(legacy_root, "use_only_when_user_asks_about_pets", "true");

        cJSON* owner_json = cJSON_AddObjectToObject(legacy_root, "owner");
        AddStringIfNotEmpty(owner_json, "name", owner_person.name);
        AddStringIfNotEmpty(owner_json, "nickname", owner_person.nickname);
        AddStringIfNotEmpty(owner_json, "relationship", owner_person.relationship);

        cJSON_AddBoolToObject(legacy_root, "has_pet", owner_person.has_pet);
        AddStringIfNotEmpty(legacy_root, "pet_type", owner_person.pet_type);
        AddStringIfNotEmpty(legacy_root, "pet_name", owner_person.pet_name);

        std::string safe_message;
        if (owner_person.pet_type == "dog") {
            safe_message = "El perro se llama ";
        } else if (owner_person.pet_type == "cat") {
            safe_message = "El gato se llama ";
        } else {
            safe_message = "La mascota se llama ";
        }
        safe_message += owner_person.pet_name;
        safe_message += ".";
        AddStringIfNotEmpty(legacy_root, "safe_message", safe_message);

        cJSON_AddNumberToObject(legacy_root, "match_count", 1);
        cJSON_AddBoolToObject(legacy_root, "truncated", false);

        cJSON* array = cJSON_AddArrayToObject(legacy_root, "preferences");
        cJSON* item = cJSON_CreateObject();
        AddStringIfNotEmpty(item, "category", "pets");
        AddStringIfNotEmpty(item, "value", safe_message);
        cJSON_AddBoolToObject(item, "not_a_general_like", true);
        cJSON_AddStringToObject(item, "use_only_when_user_asks_about_pets", "true");
        cJSON_AddItemToArray(array, item);

        ESP_LOGI("CARE_MCP", "Preference query: owner=person category=pets legacy_fallback=1");
        return Stringify(legacy_root);
    }

    ESP_LOGI("CARE_MCP", "Preference query: owner=%s category=%s matches=%u",
             owner_type.c_str(), canonical_category.c_str(),
             static_cast<unsigned>(matches.size()));

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddBoolToObject(root, "found", !matches.empty());
    cJSON_AddStringToObject(root, "source", "xiaozhi_care");
    cJSON_AddBoolToObject(root, "do_not_infer", true);
    cJSON_AddStringToObject(root, "category", canonical_category.c_str());
    cJSON_AddStringToObject(root, "owner_type", owner_type.c_str());
    if (canonical_category == "pets") {
        cJSON_AddBoolToObject(root, "not_a_general_like", true);
        cJSON_AddStringToObject(root, "use_only_when_user_asks_about_pets", "true");
    }
    if (has_owner_person) {
        cJSON* owner_json = cJSON_AddObjectToObject(root, "owner");
        AddStringIfNotEmpty(owner_json, "name", owner_person.name);
        AddStringIfNotEmpty(owner_json, "nickname", owner_person.nickname);
        AddStringIfNotEmpty(owner_json, "relationship", owner_person.relationship);
    }
    cJSON_AddNumberToObject(root, "match_count", static_cast<double>(matches.size()));
    cJSON_AddBoolToObject(root, "truncated", matches.size() > kMaxMcpItems);
    cJSON* array = cJSON_AddArrayToObject(root, "preferences");

    const size_t count = std::min(matches.size(), kMaxMcpItems);
    for (size_t i = 0; i < count; ++i) {
        cJSON* item = cJSON_CreateObject();
        AddStringIfNotEmpty(item, "category", matches[i]->category);
        AddStringIfNotEmpty(item, "value", matches[i]->value);
        cJSON_AddItemToArray(array, item);
    }
    return Stringify(root);
}

std::string CareMcpService::GetPillbox(int weekday,
                                       const std::string& period,
                                       bool include_color) {
    if (weekday < 0 || weekday > 7) {
        return ErrorJson(CareError::INVALID_ARGUMENT);
    }

    std::string resolved_period = Normalize(period);
    if (!resolved_period.empty() && !IsAllowedPeriod(resolved_period)) {
        return ErrorJson("INVALID_PERIOD");
    }

    int resolved_weekday = weekday;
    bool used_device_clock = false;
    std::tm local_tm{};
    if (resolved_weekday == 0) {
        if (!GetLocalClock(local_tm)) {
            return ErrorJson("DEVICE_CLOCK_UNAVAILABLE");
        }
        resolved_weekday = local_tm.tm_wday == 0 ? 7 : local_tm.tm_wday;
        used_device_clock = true;
        if (resolved_period.empty()) {
            resolved_period = PeriodForHour(local_tm.tm_hour);
        }
    }

    std::vector<CarePillboxEntry> values;
    const CareError result = CareManager::GetInstance().ListPillboxEntries(values);
    if (result != CareError::OK) {
        return ErrorJson(result);
    }

    std::vector<const CarePillboxEntry*> matches;
    for (const auto& entry : values) {
        if (!entry.enabled || entry.weekday != resolved_weekday) {
            continue;
        }
        if (!resolved_period.empty() && Normalize(entry.period) != resolved_period) {
            continue;
        }
        matches.push_back(&entry);
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddBoolToObject(root, "found", !matches.empty());
    cJSON_AddNumberToObject(root, "weekday", resolved_weekday);
    if (!resolved_period.empty()) {
        cJSON_AddStringToObject(root, "period", resolved_period.c_str());
    }
    cJSON_AddBoolToObject(root, "used_device_clock", used_device_clock);
    if (used_device_clock) {
        cJSON_AddStringToObject(root, "clock_period", PeriodForHour(local_tm.tm_hour).c_str());
        char time_buffer[32] = {};
        std::snprintf(time_buffer, sizeof(time_buffer), "%02d:%02d", local_tm.tm_hour, local_tm.tm_min);
        cJSON_AddStringToObject(root, "device_time", time_buffer);
    }
    cJSON_AddNumberToObject(root, "match_count", static_cast<double>(matches.size()));
    cJSON_AddBoolToObject(root, "truncated", matches.size() > kMaxMcpItems);
    cJSON* array = cJSON_AddArrayToObject(root, "entries");

    const size_t count = std::min(matches.size(), kMaxMcpItems);
    for (size_t i = 0; i < count; ++i) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "weekday", matches[i]->weekday);
        AddStringIfNotEmpty(item, "period", matches[i]->period);
        AddStringIfNotEmpty(item, "time", matches[i]->time);
        AddStringIfNotEmpty(item, "compartment", matches[i]->compartment);
        if (include_color) {
            AddStringIfNotEmpty(item, "pill_color", matches[i]->pill_color);
        }
        cJSON_AddItemToArray(array, item);
    }
    return Stringify(root);
}

std::string CareMcpService::GetReminders(const std::string& date) {
    std::string resolved_date = date;
    bool used_device_clock = false;

    if (resolved_date.empty()) {
        std::tm local_tm{};
        if (!GetLocalClock(local_tm)) {
            return ErrorJson("DEVICE_CLOCK_UNAVAILABLE");
        }
        resolved_date = FormatDate(local_tm.tm_year + 1900, local_tm.tm_mon + 1, local_tm.tm_mday);
        used_device_clock = true;
    }

    int year = 0, month = 0, day = 0;
    if (!ParseDate(resolved_date, year, month, day)) {
        return ErrorJson(CareError::INVALID_DATE);
    }

    std::vector<CareReminder> values;
    const CareError result = CareManager::GetInstance().ListReminders(values);
    if (result != CareError::OK) {
        return ErrorJson(result);
    }

    std::vector<SmartReminderMatch> matches;

    for (const auto& reminder : values) {
        if (!reminder.enabled) {
            continue;
        }

        const int remember_before = ReadRememberBeforeMinutes(reminder.notes);

        for (int offset = 0; offset <= 7; ++offset) {
            std::string occurrence_date;
            if (!AddDaysToDate(resolved_date, offset, occurrence_date)) {
                continue;
            }

            if (!ReminderOccursOn(reminder, occurrence_date)) {
                continue;
            }

            const int days_until = DaysBetweenDates(resolved_date, occurrence_date);
            if (days_until < 0) {
                continue;
            }

            const bool is_today = days_until == 0;
            const bool inside_memory_window =
                remember_before > 0 && (days_until * 1440) <= remember_before;

            if (!is_today && !inside_memory_window) {
                continue;
            }

            SmartReminderMatch match;
            match.reminder = &reminder;
            match.occurrence_date = occurrence_date;
            match.days_until = days_until;
            match.remember_before_minutes = remember_before;
            match.from_memory_window = !is_today;
            matches.push_back(match);
            break;
        }
    }

    std::sort(matches.begin(), matches.end(), [](const SmartReminderMatch& a, const SmartReminderMatch& b) {
        if (a.occurrence_date != b.occurrence_date) return a.occurrence_date < b.occurrence_date;
        const std::string at = a.reminder ? a.reminder->time : "";
        const std::string bt = b.reminder ? b.reminder->time : "";
        if (at != bt) return at < bt;
        const std::string an = a.reminder ? a.reminder->title : "";
        const std::string bn = b.reminder ? b.reminder->title : "";
        return an < bn;
    });

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddBoolToObject(root, "found", !matches.empty());
    cJSON_AddStringToObject(root, "date", resolved_date.c_str());
    cJSON_AddNumberToObject(root, "weekday", IsoWeekday(year, month, day));
    cJSON_AddBoolToObject(root, "used_device_clock", used_device_clock);
    cJSON_AddNumberToObject(root, "match_count", static_cast<double>(matches.size()));
    cJSON_AddBoolToObject(root, "truncated", matches.size() > kMaxMcpItems);
    cJSON_AddStringToObject(root, "memory_rule", "incluye hoy y proximos recordatorios dentro de empezar_a_recordar");

    if (matches.empty()) {
        cJSON_AddStringToObject(root, "safe_message", "No tengo recordatorios cercanos para esa fecha.");
    } else if (matches.size() == 1 && matches[0].reminder != nullptr) {
        const std::string safe = ReminderMemoryText(*matches[0].reminder, matches[0].days_until);
        cJSON_AddStringToObject(root, "safe_message", safe.c_str());
    } else {
        std::string safe = "Tengo " + std::to_string(matches.size()) + " recordatorios cercanos: ";
        const size_t count_for_text = std::min(matches.size(), kMaxMcpItems);
        for (size_t i = 0; i < count_for_text; ++i) {
            if (i > 0) safe += "; ";
            const CareReminder& reminder = *matches[i].reminder;
            safe += ReminderSummaryText(reminder, matches[i].days_until);
        }
        safe += ".";
        cJSON_AddStringToObject(root, "safe_message", safe.c_str());
    }

    cJSON* array = cJSON_AddArrayToObject(root, "reminders");

    const size_t count = std::min(matches.size(), kMaxMcpItems);
    for (size_t i = 0; i < count; ++i) {
        const SmartReminderMatch& match = matches[i];
        const CareReminder& reminder = *match.reminder;

        cJSON* item = cJSON_CreateObject();
        AddStringIfNotEmpty(item, "title", reminder.title);
        AddStringIfNotEmpty(item, "date", reminder.date);
        AddStringIfNotEmpty(item, "occurrence_date", match.occurrence_date);
        AddStringIfNotEmpty(item, "time", reminder.time);
        AddStringIfNotEmpty(item, "recurrence", reminder.recurrence);
        cJSON_AddNumberToObject(item, "days_until", match.days_until);
        cJSON_AddNumberToObject(item, "remember_before_minutes", match.remember_before_minutes);
        cJSON_AddBoolToObject(item, "from_memory_window", match.from_memory_window);

        const std::string when_text = ReminderWhenText(match.days_until, reminder.time);
        const std::string memory_text = ReminderMemoryText(reminder, match.days_until);
        cJSON_AddStringToObject(item, "when_text", when_text.c_str());
        cJSON_AddStringToObject(item, "memory_text", memory_text.c_str());

        if (!reminder.related_person_id.empty()) {
            CarePerson person;
            if (CareManager::GetInstance().GetPerson(reminder.related_person_id, person) == CareError::OK &&
                person.enabled) {
                cJSON* related = cJSON_AddObjectToObject(item, "related_person");
                AddStringIfNotEmpty(related, "name", person.name);
                AddStringIfNotEmpty(related, "nickname", person.nickname);
                AddStringIfNotEmpty(related, "relationship", person.relationship);
            }
        }

        cJSON_AddItemToArray(array, item);
    }

    return Stringify(root);
}

std::string CareMcpService::AddTodoReminder(const std::string& text,
                                            const std::string& related_person_id) {
    auto fail_json = [](const std::string& code, const std::string& message) -> std::string {
        cJSON* root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "ok", false);
        cJSON_AddBoolToObject(root, "created", false);
        cJSON_AddStringToObject(root, "error", code.c_str());
        cJSON_AddStringToObject(root, "safe_message", message.c_str());
        cJSON_AddStringToObject(root, "instruction", "Responder al usuario con safe_message. No digas que quedo anotado si ok=false.");
        return Stringify(root);
    };

    auto trim = [](const std::string& input) -> std::string {
        size_t start = 0;
        while (start < input.size() && static_cast<unsigned char>(input[start]) <= 32) {
            ++start;
        }

        size_t end = input.size();
        while (end > start && static_cast<unsigned char>(input[end - 1]) <= 32) {
            --end;
        }

        return input.substr(start, end - start);
    };

    std::string title = trim(text);

    if (title.empty()) {
        ESP_LOGW("CARE_MCP", "AddTodoReminder rejected: empty text");
        return fail_json("EMPTY_TEXT", "No entendí qué querías que deje anotado.");
    }

    ESP_LOGI("CARE_MCP", "AddTodoReminder request text='%s'", title.c_str());

    if (title.size() > 96) {
        title = title.substr(0, 96);
        ESP_LOGW("CARE_MCP", "AddTodoReminder title truncated='%s'", title.c_str());
    }

    std::tm local_tm{};
    if (!GetLocalClock(local_tm)) {
        ESP_LOGE("CARE_MCP", "AddTodoReminder failed: device clock unavailable");
        return fail_json("DEVICE_CLOCK_UNAVAILABLE", "No pude dejarlo anotado porque todavía no tengo bien la hora del dispositivo.");
    }

    const std::string today = FormatDate(local_tm.tm_year + 1900, local_tm.tm_mon + 1, local_tm.tm_mday);

    std::vector<CareReminder> existing_reminders;
    CareError list_result = CareManager::GetInstance().ListReminders(existing_reminders);
    if (list_result != CareError::OK) {
        ESP_LOGE("CARE_MCP", "AddTodoReminder ListReminders failed error=%s", CareErrorToString(list_result));
        return fail_json("LIST_FAILED", "No pude revisar los recordatorios guardados.");
    }

    for (const CareReminder& existing : existing_reminders) {
        if (!existing.enabled) {
            continue;
        }

        if (Normalize(existing.title) == Normalize(title) &&
            existing.notes.find("[[voice_todo:1]]") != std::string::npos) {
            ESP_LOGI("CARE_MCP", "AddTodoReminder duplicate id=%s title='%s'", existing.id.c_str(), title.c_str());

            cJSON* root = cJSON_CreateObject();
            cJSON_AddBoolToObject(root, "ok", true);
            cJSON_AddBoolToObject(root, "created", false);
            cJSON_AddBoolToObject(root, "already_exists", true);
            cJSON_AddStringToObject(root, "id", existing.id.c_str());
            cJSON_AddStringToObject(root, "type", "voice_todo");
            cJSON_AddStringToObject(root, "title", title.c_str());
            const std::string safe = "Ya lo tenía anotado: " + title + ".";
            cJSON_AddStringToObject(root, "safe_message", safe.c_str());
            cJSON_AddStringToObject(root, "instruction", "Responder al usuario con safe_message. No volver a llamar care.add_todo_reminder para esta misma frase.");
            return Stringify(root);
        }
    }

    CareReminder reminder;
    reminder.title = title;
    reminder.date = today;
    reminder.time = "";
    reminder.recurrence = "daily";
    reminder.related_person_id = related_person_id;
    reminder.notes = "[[voice_todo:1]] Creado por voz. Cosa para hacer.";
    reminder.enabled = true;

    ESP_LOGI("CARE_MCP", "AddTodoReminder saving title='%s' date=%s recurrence=%s",
             reminder.title.c_str(), reminder.date.c_str(), reminder.recurrence.c_str());

    std::string id;
    CareError result = CareManager::GetInstance().AddReminder(std::move(reminder), id);
    if (result != CareError::OK) {
        ESP_LOGE("CARE_MCP", "AddTodoReminder AddReminder failed text='%s' error=%s",
                 title.c_str(), CareErrorToString(result));
        return fail_json("SAVE_FAILED", "No pude dejarlo anotado. Necesito que la familia revise el panel.");
    }

    CareReminder saved;
    CareError get_result = CareManager::GetInstance().GetReminder(id, saved);
    if (get_result != CareError::OK) {
        ESP_LOGE("CARE_MCP", "AddTodoReminder saved id=%s but GetReminder failed error=%s",
                 id.c_str(), CareErrorToString(get_result));
        return fail_json("VERIFY_FAILED", "Intenté dejarlo anotado, pero no pude verificar que haya quedado guardado.");
    }

    ESP_LOGI("CARE_MCP", "AddTodoReminder saved id=%s title='%s' date=%s recurrence=%s",
             id.c_str(), saved.title.c_str(), saved.date.c_str(), saved.recurrence.c_str());

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddBoolToObject(root, "created", true);
    cJSON_AddStringToObject(root, "id", id.c_str());
    cJSON_AddStringToObject(root, "type", "voice_todo");
    cJSON_AddStringToObject(root, "title", saved.title.c_str());
    cJSON_AddStringToObject(root, "date", saved.date.c_str());
    cJSON_AddStringToObject(root, "recurrence", saved.recurrence.c_str());
    const std::string safe = "Sí, lo dejo anotado: " + saved.title + ".";
    cJSON_AddStringToObject(root, "safe_message", safe.c_str());
    cJSON_AddStringToObject(root, "web_panel", "Aparece en la pestaña Recordatorios.");
    cJSON_AddStringToObject(root, "instruction", "Responder al usuario con safe_message. No volver a llamar care.add_todo_reminder para esta misma frase.");
    return Stringify(root);
}


std::string CareMcpService::GetDueRoutines(int weekday,
                                           const std::string& time,
                                           const std::string& mode) {
    if (weekday < 0 || weekday > 7) {
        return ErrorJson(CareError::INVALID_ARGUMENT);
    }

    std::string query_mode = Normalize(mode);
    if (query_mode.empty()) {
        query_mode = "now";
    }
    if (query_mode == "ahora") {
        query_mode = "now";
    } else if (query_mode == "hoy") {
        query_mode = "today";
    } else if (query_mode == "próxima" || query_mode == "proximo" ||
               query_mode == "siguiente") {
        query_mode = "next";
    }
    if (query_mode != "now" && query_mode != "today" && query_mode != "next") {
        return ErrorJson("INVALID_MODE");
    }

    daily::RoutineEvaluationContext context;
    bool used_device_clock = false;
    std::tm local_tm{};

    if (weekday == 0 || time.empty()) {
        if (!GetLocalClock(local_tm)) {
            return ErrorJson("DEVICE_CLOCK_UNAVAILABLE");
        }
        used_device_clock = true;
    }

    if (weekday == 0) {
        context.iso_weekday = local_tm.tm_wday == 0 ? 7 : static_cast<uint8_t>(local_tm.tm_wday);
    } else {
        context.iso_weekday = static_cast<uint8_t>(weekday);
    }

    if (time.empty()) {
        context.now = daily::DailyTime(static_cast<uint8_t>(local_tm.tm_hour),
                                       static_cast<uint8_t>(local_tm.tm_min));
    } else if (!ParseDailyTimeText(time, context.now)) {
        return ErrorJson("INVALID_TIME");
    }

    daily::NvsRoutineRepository repository;
    if (!repository.Init()) {
        return ErrorJson("ROUTINE_REPOSITORY_INIT_FAILED");
    }

    // Las consultas generales MCP son exclusivamente NO medicinales.
    // La medicacion se consulta por care.get_pillbox_plan / DP-036.
    const std::vector<daily::CareRoutine> all_routines = repository.List();
    std::vector<daily::CareRoutine> routines;
    routines.reserve(all_routines.size());

    for (const auto& routine : all_routines) {
        if (routine.type == daily::RoutineType::Medication) {
            continue;
        }
        routines.push_back(routine);
    }

    daily::DailyRoutineEngine engine;

    auto routine_for_weekday = [](const daily::CareRoutine& routine, uint8_t iso_weekday) -> bool {
        if (iso_weekday < 1 || iso_weekday > 7 || !routine.IsValid()) {
            return false;
        }
        if (routine.state != daily::RoutineState::Active) {
            return false;
        }
        if (routine.schedule.repeat == daily::RoutineRepeatType::AsNeeded ||
            routine.schedule.repeat == daily::RoutineRepeatType::EveryNDays) {
            return false;
        }
        if (routine.schedule.repeat == daily::RoutineRepeatType::Daily) {
            return true;
        }
        if (routine.schedule.repeat == daily::RoutineRepeatType::SpecificWeekdays) {
            return routine.schedule.weekdays[static_cast<size_t>(iso_weekday - 1)];
        }
        return false;
    };

    auto next_weekday = [](uint8_t iso_weekday, uint8_t offset_days) -> uint8_t {
        return static_cast<uint8_t>(((static_cast<unsigned>(iso_weekday) - 1u + offset_days) % 7u) + 1u);
    };

    auto daily_minutes = [](const daily::DailyTime& value) -> int {
        return static_cast<int>(value.hour) * 60 + static_cast<int>(value.minute);
    };

    auto friendly_moment_text = [](const std::string& moment) -> std::string {
        std::string raw = moment;
        for (char& ch : raw) {
            if (ch == '_' || ch == '-') {
                ch = ' ';
            }
        }

        const std::string m = Normalize(raw);

        if (m.empty()) return "";

        if (m == "fasting" || m == "empty stomach" || m == "ayunas") {
            return "en ayunas";
        }

        if (m == "morning" || m == "manana" || m == "mañana") {
            return "a la mañana";
        }

        if (m == "noon" || m == "midday" || m == "mediodia" || m == "mediodía") {
            return "al mediodía";
        }

        if (m == "after breakfast" || m == "despues del desayuno" || m == "después del desayuno") {
            return "después del desayuno";
        }

        if (m == "before breakfast" || m == "antes del desayuno") {
            return "antes del desayuno";
        }

        if (m == "before lunch" || m == "antes del almuerzo") {
            return "antes del almuerzo";
        }

        if (m == "after lunch" || m == "despues del almuerzo" || m == "después del almuerzo") {
            return "después del almuerzo";
        }

        if (m == "afternoon" || m == "tarde") {
            return "a la tarde";
        }

        if (m == "before dinner" || m == "antes de la cena") {
            return "antes de la cena";
        }

        if (m == "after dinner" || m == "despues de la cena" || m == "después de la cena") {
            return "después de la cena";
        }

        if (m == "evening" || m == "night" || m == "noche") {
            return "a la noche";
        }

        return raw;
    };

    auto moment_instruction = [&](const std::string& moment) -> std::string {
        const std::string friendly = friendly_moment_text(moment);
        const std::string normalized = Normalize(friendly);

        if (normalized.empty()) {
            return "";
        }

        if (normalized.find("ayunas") != std::string::npos) {
            return "Tenés que hacerlo en ayunas.";
        }

        if (normalized.find("despues") != std::string::npos ||
            normalized.find("después") != std::string::npos ||
            normalized.find("antes") != std::string::npos) {
            return "Tenés que hacerlo " + friendly + ".";
        }

        return "Tenés que hacerlo " + friendly + ".";
    };

    auto weekday_name = [](uint8_t iso_weekday) -> std::string {
        switch (iso_weekday) {
            case 1: return "lunes";
            case 2: return "martes";
            case 3: return "miércoles";
            case 4: return "jueves";
            case 5: return "viernes";
            case 6: return "sábado";
            case 7: return "domingo";
            default: return "hoy";
        }
    };

    auto compartment_phrase = [](const std::string& compartment) -> std::string {
        const std::string c = Normalize(compartment);

        if (c == "primero" || c == "primer" || c == "1" || c == "uno") {
            return "primer casillero";
        }
        if (c == "segundo" || c == "2" || c == "dos") {
            return "segundo casillero";
        }
        if (c == "tercero" || c == "3" || c == "tres") {
            return "tercer casillero";
        }
        if (c == "cuarto" || c == "4" || c == "cuatro") {
            return "cuarto casillero";
        }
        if (c == "quinto" || c == "5" || c == "cinco") {
            return "quinto casillero";
        }
        if (c == "sexto" || c == "6" || c == "seis") {
            return "sexto casillero";
        }
        if (c.find("casillero") != std::string::npos) {
            return compartment;
        }

        return compartment + " casillero";
    };

    // DP035_R3A_PILLBOX_SAFE_DETAILS
    //
    // Solo se permite exponer:
    //   - cantidad
    //   - descripcion estrictamente visual
    //
    // Nunca title, description ni private_notes.
    auto read_pillbox_safe_data =
        [](const daily::CareRoutine& item,
           int& quantity,
           std::string& visual) {
            quantity = 1;
            visual.clear();

            if (item.data.empty()) {
                return;
            }

            cJSON* root = cJSON_Parse(item.data.c_str());
            if (!cJSON_IsObject(root)) {
                if (root != nullptr) {
                    cJSON_Delete(root);
                }
                return;
            }

            const cJSON* q =
                cJSON_GetObjectItemCaseSensitive(root, "quantity");

            if (cJSON_IsNumber(q) &&
                q->valueint >= 1 &&
                q->valueint <= 20) {
                quantity = q->valueint;
            }

            const cJSON* v =
                cJSON_GetObjectItemCaseSensitive(
                    root, "visual_description");

            // Compatibilidad con datos antiguos.
            if (!cJSON_IsString(v) ||
                v->valuestring == nullptr ||
                v->valuestring[0] == '\0') {
                v = cJSON_GetObjectItemCaseSensitive(root, "visual");
            }

            if (cJSON_IsString(v) &&
                v->valuestring != nullptr) {
                visual = v->valuestring;
            }

            cJSON_Delete(root);
        };

    auto pillbox_group_safe_details =
        [&](const daily::CareRoutine& base,
            int& total_quantity,
            std::string& visual_summary) {
            total_quantity = 0;
            visual_summary.clear();

            std::vector<std::string> seen_visuals;

            for (const daily::CareRoutine& candidate : routines) {
                if (!routine_for_weekday(
                        candidate, context.iso_weekday)) {
                    continue;
                }

                if (candidate.type !=
                    daily::RoutineType::Medication) {
                    continue;
                }

                if (candidate.placement.compartment !=
                    base.placement.compartment) {
                    continue;
                }

                if (candidate.schedule.moment !=
                    base.schedule.moment) {
                    continue;
                }

                if (!(candidate.schedule.time ==
                      base.schedule.time)) {
                    continue;
                }

                int quantity = 1;
                std::string visual;

                read_pillbox_safe_data(
                    candidate, quantity, visual);

                total_quantity += quantity;

                if (!visual.empty() &&
                    std::find(
                        seen_visuals.begin(),
                        seen_visuals.end(),
                        visual) == seen_visuals.end()) {
                    seen_visuals.push_back(visual);
                }
            }

            if (total_quantity <= 0) {
                total_quantity = 1;
            }

            for (size_t i = 0;
                 i < seen_visuals.size();
                 ++i) {
                if (!visual_summary.empty()) {
                    visual_summary += "; ";
                }
                visual_summary += seen_visuals[i];
            }
        };
    auto safe_routine_message = [&](const daily::CareRoutine& routine,
                                    int minutes_until) -> std::string {
        const std::string moment = friendly_moment_text(RoutineMomentName(routine.schedule.moment));
        const std::string compartment = routine.placement.compartment;
        const std::string time_text = FormatDailyTimeText(routine.schedule.time);

        if (!compartment.empty()) {
            const std::string comp = compartment_phrase(compartment);

            std::string msg;

            if (!time_text.empty()) {
                msg += "A las ";
                msg += time_text;
                msg += ", ";
            }

            msg += "te toca tomar los remedios del ";
            msg += comp;
            msg += " del pastillero.";

            msg += " Acordate que hoy es ";
            msg += weekday_name(context.iso_weekday);
            msg += ".";

            int pill_quantity = 1;
            std::string visual_summary;

            pillbox_group_safe_details(
                routine,
                pill_quantity,
                visual_summary);

            msg += " Este casillero contiene ";
            msg += std::to_string(pill_quantity);
            msg += pill_quantity == 1
                ? " pastilla."
                : " pastillas.";

            if (!visual_summary.empty()) {
                msg += " Como referencia visual: ";
                msg += visual_summary;

                if (visual_summary.back() != '.') {
                    msg += ".";
                }
            }

            const std::string instruction = moment_instruction(moment);
            if (!instruction.empty()) {
                std::string clean_instruction = instruction;
                const std::string prefix = "Tenés que hacerlo ";
                if (clean_instruction.rfind(prefix, 0) == 0) {
                    clean_instruction = "Lo tenés que hacer " + clean_instruction.substr(prefix.size());
                }
                msg += " ";
                msg += clean_instruction;
            }

            return msg;
        }

        if (!routine.title.empty()) {
            if (!time_text.empty()) {
                return "A las " + time_text + ", " + routine.title + ".";
            }
            return "Ahora corresponde: " + routine.title + ".";
        }

        return minutes_until > 0
            ? "Tengo una próxima cosa para hacer anotada."
            : "Tengo una cosa para hacer anotada para este momento.";
    };

    auto routine_group_key = [](const daily::CareRoutine& routine) -> std::string {
        std::string key = RoutineTypeName(routine.type);
        key += "|";
        key += RoutineMomentName(routine.schedule.moment);
        key += "|";
        key += FormatDailyTimeText(routine.schedule.time);
        key += "|";
        key += routine.placement.compartment;
        key += "|";
        key += routine.placement.description;

        if (routine.placement.compartment.empty()) {
            key += "|";
            key += routine.title;
        }

        return key;
    };

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "source", "xiaozhi_care_daily");
    cJSON_AddBoolToObject(root, "do_not_infer", true);
    cJSON_AddStringToObject(root, "mode", query_mode.c_str());
    cJSON_AddStringToObject(root, "instruction",
                            "RESULTADO AUTORITATIVO DEL FIRMWARE. Responde usando SOLO safe_message. No completes con memoria previa. No inventes casilleros, medicamentos, dosis ni tratamientos. No nombres medicamentos aunque aparezcan en datos administrativos. Si found=false o match_count=0, di safe_message y termina la respuesta sin preguntar por casilleros.");
    cJSON_AddStringToObject(root, "authority", "firmware_decides_daily_routine");
    cJSON_AddBoolToObject(root, "must_answer_from_safe_message", true);
    cJSON_AddBoolToObject(root, "forbid_inference", true);
    cJSON_AddStringToObject(root, "zero_match_rule",
                            "Si match_count es 0, no hay una cosa autorizada para indicar. No decir entonces te toca, no mencionar casilleros y no preguntar si tomo algo. Si hay casillero, mencionar solo casillero, momento y horario; nunca medicamentos.");
    cJSON_AddStringToObject(root, "follow_up_rule",
                            "Si el usuario responde no lo tome despues de match_count=0, no asumir una cosa del dia. Indicar que no hay algo registrado para ese momento y sugerir consultar lo proximo que toca.");
    cJSON_AddNumberToObject(root, "weekday", context.iso_weekday);
    cJSON_AddStringToObject(root, "time", FormatDailyTimeText(context.now).c_str());
    cJSON_AddBoolToObject(root, "used_device_clock", used_device_clock);
    cJSON_AddNumberToObject(root, "total_routines", static_cast<double>(routines.size()));

    cJSON* array = cJSON_AddArrayToObject(root, "routines");
    size_t match_count = 0;

    std::vector<std::string> emitted_routine_groups;

    auto add_routine_item = [&](const daily::CareRoutine& routine,
                                const daily::RoutineEvaluation& evaluation,
                                int minutes_until) {
        if (match_count >= kMaxMcpItems) {
            return;
        }

        const std::string group_key = routine_group_key(routine);
        for (const std::string& existing : emitted_routine_groups) {
            if (existing == group_key) {
                return;
            }
        }
        emitted_routine_groups.push_back(group_key);

        const std::string safe = safe_routine_message(routine, minutes_until);

        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "routine_id", routine.id.Str().c_str());
        cJSON_AddStringToObject(item, "type", RoutineTypeName(routine.type));

        // IMPORTANTE:
        // No exponer title en rutinas de pastillero con casillero,
        // porque puede contener nombres de medicamentos.
        if (routine.placement.compartment.empty()) {
            const bool hide_medical_title =
                routine.type == daily::RoutineType::Medication ||
                !routine.placement.compartment.empty();

            if (!hide_medical_title) {
                AddStringIfNotEmpty(item, "title", routine.title);
            } else {
                cJSON_AddBoolToObject(item, "medicine_names_hidden", true);
            }
        } else {
            AddStringIfNotEmpty(item, "title", routine.placement.compartment);
        }

        cJSON_AddStringToObject(item, "moment", RoutineMomentName(routine.schedule.moment));
        cJSON_AddStringToObject(item, "scheduled_time", FormatDailyTimeText(routine.schedule.time).c_str());
        cJSON_AddNumberToObject(item, "minutes_from_schedule", evaluation.minutes_from_schedule);
        cJSON_AddNumberToObject(item, "minutes_until", minutes_until);
        cJSON_AddStringToObject(item, "safe_message", safe.c_str());

        if (routine.type == daily::RoutineType::Medication &&
            !routine.placement.compartment.empty()) {
            int pill_quantity = 1;
            std::string visual_summary;

            pillbox_group_safe_details(
                routine,
                pill_quantity,
                visual_summary);

            cJSON_AddNumberToObject(
                item,
                "pill_count",
                pill_quantity);

            AddStringIfNotEmpty(
                item,
                "visual_description",
                visual_summary);
        }
        cJSON_AddBoolToObject(item, "medicine_names_hidden", !routine.placement.compartment.empty());

        cJSON* placement = cJSON_AddObjectToObject(item, "placement");
        cJSON_AddStringToObject(placement, "type", PlacementTypeName(routine.placement.type));
        AddStringIfNotEmpty(placement, "description", routine.placement.description);
        AddStringIfNotEmpty(placement, "compartment", routine.placement.compartment);

        cJSON_AddItemToArray(array, item);
        ++match_count;
    };

    if (query_mode == "now") {
        const std::vector<daily::RoutineEvaluation> evaluations = engine.EvaluateAll(routines, context);
        for (size_t i = 0; i < evaluations.size() && match_count < kMaxMcpItems; ++i) {
            if (evaluations[i].IsDue()) {
                add_routine_item(routines[i], evaluations[i], 0);
            }
        }
    } else if (query_mode == "today") {
        for (const daily::CareRoutine& routine : routines) {
            if (match_count >= kMaxMcpItems) {
                break;
            }
            if (!routine_for_weekday(routine, context.iso_weekday)) {
                continue;
            }
            daily::RoutineEvaluationContext scheduled_context;
            scheduled_context.iso_weekday = context.iso_weekday;
            scheduled_context.now = routine.schedule.time;
            daily::RoutineEvaluation evaluation = engine.Evaluate(routine, scheduled_context);
            if (!evaluation.safe_message.empty()) {
                const int minutes_until = static_cast<int>(routine.schedule.time.ToMinutes()) -
                                          static_cast<int>(context.now.ToMinutes());
                add_routine_item(routine, evaluation, minutes_until);
            }
        }
    } else if (query_mode == "next") {
        const daily::CareRoutine* best_routine = nullptr;
        daily::RoutineEvaluation best_evaluation;
        int best_minutes_until = 999999;
        uint8_t best_weekday = context.iso_weekday;

        const int now_minutes = daily_minutes(context.now);

        for (uint8_t offset_days = 0; offset_days <= 7; ++offset_days) {
            const uint8_t candidate_weekday = next_weekday(context.iso_weekday, offset_days);

            for (const daily::CareRoutine& routine : routines) {
                if (!routine_for_weekday(routine, candidate_weekday)) {
                    continue;
                }

                const int scheduled_minutes = daily_minutes(routine.schedule.time);
                const int minutes_until = static_cast<int>(offset_days) * 1440 + scheduled_minutes - now_minutes;

                // Si es hoy y ya paso la hora, no puede ser "lo proximo".
                if (minutes_until <= 0) {
                    continue;
                }

                if (minutes_until >= best_minutes_until) {
                    continue;
                }

                daily::RoutineEvaluationContext scheduled_context;
                scheduled_context.iso_weekday = candidate_weekday;
                scheduled_context.now = routine.schedule.time;

                daily::RoutineEvaluation evaluation = engine.Evaluate(routine, scheduled_context);
                if (evaluation.safe_message.empty()) {
                    continue;
                }

                best_routine = &routine;
                best_evaluation = evaluation;
                best_minutes_until = minutes_until;
                best_weekday = candidate_weekday;
            }
        }

        if (best_routine != nullptr) {
            cJSON_AddNumberToObject(root, "next_weekday", best_weekday);
            add_routine_item(*best_routine, best_evaluation, best_minutes_until);
        }
    }

    cJSON_AddBoolToObject(root, "found", match_count > 0);
    cJSON_AddNumberToObject(root, "match_count", static_cast<double>(match_count));
    cJSON_AddBoolToObject(root, "truncated", match_count >= kMaxMcpItems);

    if (match_count == 0) {
        if (query_mode == "today") {
            cJSON_AddStringToObject(root, "safe_message", "No tengo cosas para hacer registradas para hoy. No puedo indicar ningun casillero para hoy.");
        } else if (query_mode == "next") {
            cJSON_AddStringToObject(root, "safe_message", "No tengo una próxima cosa para hacer registrada.");
        } else {
            cJSON_AddStringToObject(root, "safe_message", "No tengo algo registrado para este momento. No puedo indicar ningun casillero ahora.");
        }
    } else if (match_count == 1) {
        const cJSON* first = cJSON_GetArrayItem(array, 0);
        const cJSON* safe_message = first != nullptr ? cJSON_GetObjectItem(first, "safe_message") : nullptr;
        if (cJSON_IsString(safe_message) && safe_message->valuestring != nullptr) {
            cJSON_AddStringToObject(root, "safe_message", safe_message->valuestring);
        }
    }

    ESP_LOGI("CARE_MCP", "Due routine query: mode=%s weekday=%u time=%s matches=%u",
             query_mode.c_str(),
             static_cast<unsigned>(context.iso_weekday),
             FormatDailyTimeText(context.now).c_str(),
             static_cast<unsigned>(match_count));

    return Stringify(root);
}


std::string CareMcpService::RecordRoutineExecution(const std::string& routine_id_text,
                                                   const std::string& event_text,
                                                   const std::string& message) {
    daily::RoutineId routine_id(routine_id_text);
    if (routine_id.Empty()) {
        return ErrorJson("INVALID_ROUTINE_ID");
    }

    daily::RoutineExecutionEvent event = daily::RoutineExecutionEvent::Confirmed;
    if (!ParseExecutionEvent(event_text, event)) {
        return ErrorJson("INVALID_EXECUTION_EVENT");
    }

    std::tm local_tm{};
    if (!GetLocalClock(local_tm)) {
        return ErrorJson("DEVICE_CLOCK_UNAVAILABLE");
    }

    daily::NvsRoutineRepository routine_repository;
    if (!routine_repository.Init()) {
        return ErrorJson("ROUTINE_REPOSITORY_INIT_FAILED");
    }

    const std::optional<daily::CareRoutine> routine = routine_repository.FindById(routine_id);
    if (!routine.has_value()) {
        cJSON* root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "ok", true);
        cJSON_AddBoolToObject(root, "recorded", false);
        cJSON_AddBoolToObject(root, "found", false);
        cJSON_AddStringToObject(root, "source", "xiaozhi_care_daily");
        cJSON_AddBoolToObject(root, "do_not_infer", true);
        cJSON_AddStringToObject(root, "reason", "ROUTINE_NOT_FOUND");
        cJSON_AddStringToObject(root, "safe_message", "No encontré esa cosa para hacer registrada. No puedo anotar algo que no está registrado.");
        return Stringify(root);
    }

    daily::NvsRoutineExecutionRepository execution_repository;
    if (!execution_repository.Init()) {
        return ErrorJson("EXECUTION_REPOSITORY_INIT_FAILED");
    }

    daily::RoutineExecution execution;
    execution.id = execution_repository.GenerateId();
    execution.routine_id = routine_id;
    execution.event = event;
    execution.source = daily::RoutineExecutionSource::Voice;
    execution.iso_date = FormatDate(local_tm.tm_year + 1900, local_tm.tm_mon + 1, local_tm.tm_mday);
    execution.time = daily::DailyTime(static_cast<uint8_t>(local_tm.tm_hour),
                                      static_cast<uint8_t>(local_tm.tm_min));
    execution.message = message.empty() ? routine->title : message;

    if (!execution_repository.Save(execution)) {
        return ErrorJson("EXECUTION_SAVE_FAILED");
    }

    ESP_LOGI("CARE_MCP", "Recorded routine execution: routine=%s event=%s execution=%s",
             routine_id.Str().c_str(),
             ExecutionEventName(event),
             execution.id.Str().c_str());

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddBoolToObject(root, "recorded", true);
    cJSON_AddBoolToObject(root, "found", true);
    cJSON_AddStringToObject(root, "source", "xiaozhi_care_daily");
    cJSON_AddBoolToObject(root, "do_not_infer", true);
    cJSON_AddStringToObject(root, "routine_id", routine_id.Str().c_str());
    cJSON_AddStringToObject(root, "execution_id", execution.id.Str().c_str());
    cJSON_AddStringToObject(root, "event", ExecutionEventName(event));
    cJSON_AddStringToObject(root, "date", execution.iso_date.c_str());
    cJSON_AddStringToObject(root, "time", FormatDailyTimeText(execution.time).c_str());
    cJSON_AddStringToObject(root, "safe_message", "Listo, deje anotado que ya lo hiciste.");
    cJSON_AddStringToObject(root, "medical_safety_rule",
                            "No afirmar ingesta medica. Decir solo que se dejo anotado que la persona ya lo hizo.");
    return Stringify(root);
}

std::string CareMcpService::GetRoutineExecutionHistory(const std::string& routine_id_text) {
    daily::NvsRoutineExecutionRepository repository;
    if (!repository.Init()) {
        return ErrorJson("EXECUTION_REPOSITORY_INIT_FAILED");
    }

    std::vector<daily::RoutineExecution> executions;
    if (routine_id_text.empty()) {
        executions = repository.List();
    } else {
        executions = repository.ListByRoutine(daily::RoutineId(routine_id_text));
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddBoolToObject(root, "found", !executions.empty());
    cJSON_AddStringToObject(root, "source", "xiaozhi_care_daily");
    cJSON_AddBoolToObject(root, "do_not_infer", true);
    cJSON_AddNumberToObject(root, "match_count", static_cast<double>(executions.size()));
    cJSON_AddBoolToObject(root, "truncated", executions.size() > kMaxMcpItems);
    cJSON_AddStringToObject(root, "safe_message", executions.empty()
                                ? "No tengo cosas anotadas todavia."
                                : "Encontré cosas anotadas.");

    cJSON* array = cJSON_AddArrayToObject(root, "executions");
    const size_t count = std::min(executions.size(), kMaxMcpItems);
    for (size_t i = 0; i < count; ++i) {
        AddExecutionToJson(array, executions[i]);
    }

    ESP_LOGI("CARE_MCP", "Execution history query: routine=%s matches=%u",
             routine_id_text.empty() ? "all" : routine_id_text.c_str(),
             static_cast<unsigned>(executions.size()));

    return Stringify(root);
}


std::string CareMcpService::GetDailyStatus(const std::string& date,
                                           int weekday,
                                           const std::string& time,
                                           const std::string& focus) {
    if (weekday < 0 || weekday > 7) {
        return ErrorJson(CareError::INVALID_ARGUMENT);
    }

    std::string status_focus = Normalize(focus);
    if (status_focus.empty()) {
        status_focus = "all";
    }
    if (status_focus == "faltan" || status_focus == "pendientes" ||
        status_focus == "pendiente") {
        status_focus = "pending";
    } else if (status_focus == "hechas" || status_focus == "hecho" ||
               status_focus == "confirmadas" || status_focus == "confirmado") {
        status_focus = "completed";
    }
    if (status_focus != "all" && status_focus != "pending" &&
        status_focus != "completed") {
        return ErrorJson("INVALID_STATUS_FOCUS");
    }

    std::tm local_tm{};
    bool used_device_clock = false;
    if (date.empty() || weekday == 0 || time.empty()) {
        if (!GetLocalClock(local_tm)) {
            return ErrorJson("DEVICE_CLOCK_UNAVAILABLE");
        }
        used_device_clock = true;
    }

    std::string resolved_date = date;
    if (resolved_date.empty()) {
        resolved_date = FormatDate(local_tm.tm_year + 1900, local_tm.tm_mon + 1, local_tm.tm_mday);
    }

    int year = 0;
    int month = 0;
    int day = 0;
    if (!ParseDate(resolved_date, year, month, day)) {
        return ErrorJson(CareError::INVALID_DATE);
    }

    uint8_t resolved_weekday = 0;
    if (weekday == 0) {
        resolved_weekday = static_cast<uint8_t>(IsoWeekday(year, month, day));
    } else {
        resolved_weekday = static_cast<uint8_t>(weekday);
    }

    daily::DailyTime resolved_time{};
    if (time.empty()) {
        resolved_time = daily::DailyTime(static_cast<uint8_t>(local_tm.tm_hour),
                                         static_cast<uint8_t>(local_tm.tm_min));
    } else if (!ParseDailyTimeText(time, resolved_time)) {
        return ErrorJson("INVALID_TIME");
    }

    daily::NvsRoutineRepository routine_repository;
    if (!routine_repository.Init()) {
        return ErrorJson("ROUTINE_REPOSITORY_INIT_FAILED");
    }

    daily::NvsRoutineExecutionRepository execution_repository;
    if (!execution_repository.Init()) {
        return ErrorJson("EXECUTION_REPOSITORY_INIT_FAILED");
    }

    // El resumen cotidiano y los pendientes NO incluyen medicacion.
    // El estado del pastillero vive exclusivamente en DP-036.
    const std::vector<daily::CareRoutine> all_routines =
        routine_repository.List();

    std::vector<daily::CareRoutine> routines;
    routines.reserve(all_routines.size());

    for (const auto& routine : all_routines) {
        if (routine.type == daily::RoutineType::Medication) {
            continue;
        }
        routines.push_back(routine);
    }

    const std::vector<daily::RoutineExecution> executions =
        execution_repository.List();

    daily::DailyRoutineEngine engine;

    auto routine_for_weekday = [](const daily::CareRoutine& routine, uint8_t iso_weekday) -> bool {
        if (iso_weekday < 1 || iso_weekday > 7 || !routine.IsValid()) {
            return false;
        }
        if (routine.state != daily::RoutineState::Active) {
            return false;
        }
        if (routine.schedule.repeat == daily::RoutineRepeatType::AsNeeded ||
            routine.schedule.repeat == daily::RoutineRepeatType::EveryNDays) {
            return false;
        }
        if (routine.schedule.repeat == daily::RoutineRepeatType::Daily) {
            return true;
        }
        if (routine.schedule.repeat == daily::RoutineRepeatType::SpecificWeekdays) {
            return routine.schedule.weekdays[static_cast<size_t>(iso_weekday - 1)];
        }
        return false;
    };

    auto latest_execution_for = [&](const daily::RoutineId& routine_id) -> const daily::RoutineExecution* {
        const daily::RoutineExecution* latest = nullptr;
        for (const auto& execution : executions) {
            if (execution.iso_date != resolved_date || execution.routine_id != routine_id) {
                continue;
            }
            if (latest == nullptr || latest->time <= execution.time) {
                latest = &execution;
            }
        }
        return latest;
    };

    auto status_from_execution = [](const daily::RoutineExecution* execution) -> const char* {
        if (execution == nullptr) {
            return "pending";
        }
        switch (execution->event) {
            case daily::RoutineExecutionEvent::Confirmed:
                return "confirmed";
            case daily::RoutineExecutionEvent::Skipped:
                return "skipped";
            case daily::RoutineExecutionEvent::Snoozed:
                return "snoozed";
            case daily::RoutineExecutionEvent::Missed:
                return "missed";
            case daily::RoutineExecutionEvent::Indicated:
                return "indicated";
            default:
                return "pending";
        }
    };

    auto include_status_in_view = [&](const char* status) -> bool {
        if (status_focus == "all") {
            return true;
        }
        const std::string s(status != nullptr ? status : "pending");
        if (status_focus == "pending") {
            return s == "pending" || s == "indicated" || s == "snoozed";
        }
        if (status_focus == "completed") {
            return s == "confirmed";
        }
        return true;
    };

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "source", "xiaozhi_care_daily");
    cJSON_AddBoolToObject(root, "do_not_infer", true);
    cJSON_AddStringToObject(root, "mode", "daily_status");
    cJSON_AddStringToObject(root, "view", status_focus.c_str());
    cJSON_AddStringToObject(root, "date", resolved_date.c_str());
    cJSON_AddNumberToObject(root, "weekday", resolved_weekday);
    cJSON_AddStringToObject(root, "time", FormatDailyTimeText(resolved_time).c_str());
    cJSON_AddBoolToObject(root, "used_device_clock", used_device_clock);
    cJSON_AddStringToObject(root, "instruction",
                            "RESULTADO AUTORITATIVO DEL FIRMWARE. Responde usando safe_message y el arreglo routines. No afirmes ingesta medica. Para hablar con Yolita evita la palabra rutina: usa cosas para hacer hoy, lo que te toca, ya lo hiciste o deje anotado. Si view=pending, enumera solo lo pendiente. Si view=completed, enumera solo lo hecho. No contradigas los contadores.");
    cJSON_AddStringToObject(root, "authority", "firmware_decides_daily_status");

    cJSON* array = cJSON_AddArrayToObject(root, "routines");

    size_t total = 0;
    size_t confirmed = 0;
    size_t pending = 0;
    size_t skipped = 0;
    size_t snoozed = 0;
    size_t missed = 0;
    size_t indicated = 0;
    size_t view_count = 0;

    for (const auto& routine : routines) {
        if (!routine_for_weekday(routine, resolved_weekday)) {
            continue;
        }

        const daily::RoutineExecution* latest = latest_execution_for(routine.id);
        const char* status = status_from_execution(latest);

        if (std::string(status) == "confirmed") ++confirmed;
        else if (std::string(status) == "skipped") ++skipped;
        else if (std::string(status) == "snoozed") ++snoozed;
        else if (std::string(status) == "missed") ++missed;
        else if (std::string(status) == "indicated") ++indicated;
        else ++pending;

        if (include_status_in_view(status) && view_count < kMaxMcpItems) {
            daily::RoutineEvaluationContext scheduled_context;
            scheduled_context.iso_weekday = resolved_weekday;
            scheduled_context.now = routine.schedule.time;
            const daily::RoutineEvaluation evaluation = engine.Evaluate(routine, scheduled_context);

            cJSON* item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "routine_id", routine.id.Str().c_str());
            cJSON_AddStringToObject(item, "type", RoutineTypeName(routine.type));
            const bool hide_medical_title =
                routine.type == daily::RoutineType::Medication ||
                !routine.placement.compartment.empty();

            if (!hide_medical_title) {
                AddStringIfNotEmpty(item, "title", routine.title);
            } else {
                cJSON_AddBoolToObject(item, "medicine_names_hidden", true);
            }
            cJSON_AddStringToObject(item, "scheduled_time", FormatDailyTimeText(routine.schedule.time).c_str());
            cJSON_AddStringToObject(item, "moment", RoutineMomentName(routine.schedule.moment));
            cJSON_AddStringToObject(item, "status", status);
            AddStringIfNotEmpty(item, "safe_message", evaluation.safe_message);

            if (latest != nullptr) {
                cJSON* event = cJSON_AddObjectToObject(item, "latest_event");
                cJSON_AddStringToObject(event, "execution_id", latest->id.Str().c_str());
                cJSON_AddStringToObject(event, "event", ExecutionEventName(latest->event));
                cJSON_AddStringToObject(event, "source", ExecutionSourceName(latest->source));
                cJSON_AddStringToObject(event, "time", FormatDailyTimeText(latest->time).c_str());
            }

            cJSON_AddItemToArray(array, item);
            ++view_count;
        }
        ++total;
    }

    cJSON_AddBoolToObject(root, "found", total > 0);
    cJSON_AddNumberToObject(root, "routine_count", static_cast<double>(total));
    cJSON_AddNumberToObject(root, "confirmed_count", static_cast<double>(confirmed));
    cJSON_AddNumberToObject(root, "pending_count", static_cast<double>(pending));
    cJSON_AddNumberToObject(root, "indicated_count", static_cast<double>(indicated));
    cJSON_AddNumberToObject(root, "snoozed_count", static_cast<double>(snoozed));
    cJSON_AddNumberToObject(root, "skipped_count", static_cast<double>(skipped));
    cJSON_AddNumberToObject(root, "missed_count", static_cast<double>(missed));
    cJSON_AddNumberToObject(root, "view_count", static_cast<double>(view_count));
    cJSON_AddBoolToObject(root, "truncated", view_count >= kMaxMcpItems && total > view_count);

    const size_t remaining = pending + indicated + snoozed;

    char summary[512] = {};
    if (total == 0) {
        std::snprintf(summary, sizeof(summary),
                      "No tengo cosas para hacer registradas para hoy.");
    } else if (status_focus == "pending") {
        if (remaining == 0) {
            std::snprintf(summary, sizeof(summary),
                          "Por ahora no te quedan cosas pendientes para hacer hoy.");
        } else {
            std::snprintf(summary, sizeof(summary),
                          "Hoy tenias %u cosas para hacer. Ya deje anotado que hiciste %u. Te quedan %u pendientes.",
                          static_cast<unsigned>(total),
                          static_cast<unsigned>(confirmed),
                          static_cast<unsigned>(remaining));
        }
    } else if (status_focus == "completed") {
        if (confirmed == 0) {
            std::snprintf(summary, sizeof(summary),
                          "Todavía no tengo anotada ninguna cosa hecha hoy.");
        } else {
            std::snprintf(summary, sizeof(summary),
                          "Hoy ya deje anotado que hiciste %u de %u cosas para hacer.",
                          static_cast<unsigned>(confirmed),
                          static_cast<unsigned>(total));
        }
    } else if (confirmed > 0 || skipped > 0 || missed > 0) {
        std::snprintf(summary, sizeof(summary),
                      "Hoy tenias %u cosas para hacer. Ya deje anotado que hiciste %u y te quedan %u pendientes.",
                      static_cast<unsigned>(total),
                      static_cast<unsigned>(confirmed),
                      static_cast<unsigned>(remaining));
    } else {
        std::snprintf(summary, sizeof(summary),
                      "Hoy tenes %u cosas para hacer. Te quedan %u pendientes.",
                      static_cast<unsigned>(total),
                      static_cast<unsigned>(remaining));
    }
    cJSON_AddStringToObject(root, "safe_message", summary);
    cJSON_AddStringToObject(root, "medical_safety_rule",
                            "No afirmar ingesta medica. Informar solo el estado de las cosas registradas para hacer hoy.");

    ESP_LOGI("CARE_MCP", "Daily status query: view=%s date=%s weekday=%u routines=%u confirmed=%u pending=%u remaining=%u",
             status_focus.c_str(),
             resolved_date.c_str(),
             static_cast<unsigned>(resolved_weekday),
             static_cast<unsigned>(total),
             static_cast<unsigned>(confirmed),
             static_cast<unsigned>(pending),
             static_cast<unsigned>(remaining));

    return Stringify(root);
}



// -----------------------------------------------------------------------------
// DP-036 - Pastillero contextual
//
// Reglas:
//   TODAY:
//     - solo casilleros todavia pendientes
//     - agrupa medicamentos por horario + casillero
//     - describe solamente el primer casillero que ya corresponde
//     - del siguiente solo recuerda horario + casillero
//
//   TOMORROW:
//     - enumera todos los casilleros programados
//     - no describe su contenido
//
// Nunca devuelve nombres de medicamentos.
// -----------------------------------------------------------------------------
std::string CareMcpService::GetPillboxPlan(const std::string& day) {
    std::string day_key = Normalize(day);

    if (day_key.empty() || day_key == "hoy") {
        day_key = "today";
    } else if (day_key == "manana") {
        day_key = "tomorrow";
    }

    if (day_key != "today" && day_key != "tomorrow") {
        return ErrorJson("INVALID_PILLBOX_DAY");
    }

    std::tm local_tm{};
    if (!GetLocalClock(local_tm)) {
        return ErrorJson("DEVICE_CLOCK_UNAVAILABLE");
    }

    std::tm target_tm = local_tm;

    if (day_key == "tomorrow") {
        target_tm.tm_mday += 1;
        target_tm.tm_isdst = -1;

        if (std::mktime(&target_tm) == static_cast<std::time_t>(-1)) {
            return ErrorJson("DATE_CALCULATION_FAILED");
        }
    }

    const std::string resolved_date =
        FormatDate(target_tm.tm_year + 1900,
                   target_tm.tm_mon + 1,
                   target_tm.tm_mday);

    const uint8_t resolved_weekday =
        static_cast<uint8_t>(
            IsoWeekday(target_tm.tm_year + 1900,
                       target_tm.tm_mon + 1,
                       target_tm.tm_mday));

    daily::NvsRoutineRepository routine_repository;
    if (!routine_repository.Init()) {
        return ErrorJson("ROUTINE_REPOSITORY_INIT_FAILED");
    }

    daily::NvsRoutineExecutionRepository execution_repository;
    if (!execution_repository.Init()) {
        return ErrorJson("EXECUTION_REPOSITORY_INIT_FAILED");
    }

    const std::vector<daily::CareRoutine> routines =
        routine_repository.List();

    const std::vector<daily::RoutineExecution> executions =
        execution_repository.List();

    auto routine_for_weekday =
        [](const daily::CareRoutine& routine,
           uint8_t iso_weekday) -> bool {

        if (iso_weekday < 1 || iso_weekday > 7 ||
            !routine.IsValid()) {
            return false;
        }

        if (routine.state != daily::RoutineState::Active) {
            return false;
        }

        if (routine.schedule.repeat ==
            daily::RoutineRepeatType::Daily) {
            return true;
        }

        if (routine.schedule.repeat ==
            daily::RoutineRepeatType::SpecificWeekdays) {

            return routine.schedule.weekdays[
                static_cast<size_t>(iso_weekday - 1)];
        }

        return false;
    };

    auto latest_execution_for =
        [&](const daily::RoutineId& routine_id)
            -> const daily::RoutineExecution* {

        const daily::RoutineExecution* latest = nullptr;

        for (const auto& execution : executions) {
            if (execution.iso_date != resolved_date ||
                execution.routine_id != routine_id) {
                continue;
            }

            if (latest == nullptr ||
                latest->time <= execution.time) {
                latest = &execution;
            }
        }

        return latest;
    };

    auto routine_is_pending =
        [&](const daily::CareRoutine& routine) -> bool {

        if (day_key == "tomorrow") {
            return true;
        }

        const daily::RoutineExecution* execution =
            latest_execution_for(routine.id);

        if (execution == nullptr) {
            return true;
        }

        switch (execution->event) {
            case daily::RoutineExecutionEvent::Confirmed:
            case daily::RoutineExecutionEvent::Skipped:
            case daily::RoutineExecutionEvent::Missed:
                return false;

            case daily::RoutineExecutionEvent::Indicated:
            case daily::RoutineExecutionEvent::Snoozed:
                return true;

            default:
                return true;
        }
    };

    struct PillboxGroup {
        daily::DailyTime time{};
        std::string compartment;
        std::string compartment_key;
        std::vector<const daily::CareRoutine*> routines;
        bool pending{false};
    };

    std::vector<PillboxGroup> groups;

    for (const auto& routine : routines) {
        if (!routine_for_weekday(routine, resolved_weekday)) {
            continue;
        }

        if (routine.type != daily::RoutineType::Medication) {
            continue;
        }

        if (routine.placement.type !=
            daily::PlacementType::Pillbox) {
            continue;
        }

        if (routine.placement.compartment.empty()) {
            continue;
        }

        const std::string compartment_key =
            Normalize(routine.placement.compartment);

        PillboxGroup* group = nullptr;

        for (auto& candidate : groups) {
            if (candidate.time.hour ==
                    routine.schedule.time.hour &&
                candidate.time.minute ==
                    routine.schedule.time.minute &&
                candidate.compartment_key ==
                    compartment_key) {

                group = &candidate;
                break;
            }
        }

        if (group == nullptr) {
            PillboxGroup created;
            created.time = routine.schedule.time;
            created.compartment =
                routine.placement.compartment;
            created.compartment_key =
                compartment_key;

            groups.push_back(created);
            group = &groups.back();
        }

        group->routines.push_back(&routine);

        if (routine_is_pending(routine)) {
            group->pending = true;
        }
    }

    std::sort(groups.begin(), groups.end(),
        [](const PillboxGroup& a,
           const PillboxGroup& b) {

        const int am =
            static_cast<int>(a.time.hour) * 60 +
            static_cast<int>(a.time.minute);

        const int bm =
            static_cast<int>(b.time.hour) * 60 +
            static_cast<int>(b.time.minute);

        return am < bm;
    });

    std::vector<const PillboxGroup*> visible;

    for (const auto& group : groups) {
        if (day_key == "tomorrow" || group.pending) {
            visible.push_back(&group);
        }
    }

    auto compartment_phrase =
        [](const std::string& compartment) -> std::string {

        const std::string c = Normalize(compartment);

        if (c == "primero" || c == "primer" ||
            c == "1" || c == "uno") {
            return "primer casillero";
        }

        if (c == "segundo" || c == "2" ||
            c == "dos") {
            return "segundo casillero";
        }

        if (c == "tercero" || c == "tercer" ||
            c == "3" || c == "tres") {
            return "tercer casillero";
        }

        if (c == "cuarto" || c == "4" ||
            c == "cuatro") {
            return "cuarto casillero";
        }

        if (c == "quinto" || c == "5" ||
            c == "cinco") {
            return "quinto casillero";
        }

        if (c == "sexto" || c == "6" ||
            c == "seis") {
            return "sexto casillero";
        }

        if (c.find("casillero") != std::string::npos) {
            return c;
        }

        return c + " casillero";
    };

    auto number_text =
        [](int value) -> std::string {

        switch (value) {
            case 1: return "una";
            case 2: return "dos";
            case 3: return "tres";
            case 4: return "cuatro";
            case 5: return "cinco";
            case 6: return "seis";
            case 7: return "siete";
            case 8: return "ocho";
            case 9: return "nueve";
            case 10: return "diez";
            default:
                return std::to_string(value);
        }
    };

    // DP-036: formato pensado para voz.
    // No decir "09:00" porque algunos TTS separan ":" y pronuncian "00".
    auto spoken_time =
        [](const daily::DailyTime& value) -> std::string {

        const int hour =
            static_cast<int>(value.hour);

        const int minute =
            static_cast<int>(value.minute);

        std::string result =
            std::to_string(hour);

        if (minute != 0) {
            result += " y ";
            result += std::to_string(minute);
        }

        return result;
    };

    auto read_visual =
        [](const daily::CareRoutine& routine,
           int& quantity,
           std::string& visual) {

        quantity = 1;
        visual.clear();

        if (routine.data.empty()) {
            return;
        }

        cJSON* json =
            cJSON_Parse(routine.data.c_str());

        if (json == nullptr) {
            return;
        }

        const cJSON* q =
            cJSON_GetObjectItemCaseSensitive(
                json, "quantity");

        if (cJSON_IsNumber(q) &&
            q->valueint > 0) {
            quantity = q->valueint;
        }

        const cJSON* v =
            cJSON_GetObjectItemCaseSensitive(
                json, "visual_description");

        if (!cJSON_IsString(v) ||
            v->valuestring == nullptr ||
            v->valuestring[0] == '\0') {

            v = cJSON_GetObjectItemCaseSensitive(
                json, "visual");
        }

        if (cJSON_IsString(v) &&
            v->valuestring != nullptr) {
            visual = Normalize(v->valuestring);
        }

        cJSON_Delete(json);
    };

    auto describe_visual =
        [&](const std::string& visual,
            int quantity) -> std::string {

        const std::string v = Normalize(visual);

        std::string shape;
        std::string color;

        if (v.find("redond") != std::string::npos) {
            shape = quantity == 1 ?
                "redonda" : "redondas";
        } else if (v.find("ovalad") != std::string::npos) {
            shape = quantity == 1 ?
                "ovalada" : "ovaladas";
        } else if (v.find("alargad") != std::string::npos) {
            shape = quantity == 1 ?
                "alargada" : "alargadas";
        } else if (v.find("cuadrad") != std::string::npos) {
            shape = quantity == 1 ?
                "cuadrada" : "cuadradas";
        }

        if (v.find("blanc") != std::string::npos) {
            color = quantity == 1 ?
                "blanca" : "blancas";
        } else if (v.find("amarill") != std::string::npos) {
            color = quantity == 1 ?
                "amarilla" : "amarillas";
        } else if (v.find("roj") != std::string::npos) {
            color = quantity == 1 ?
                "roja" : "rojas";
        } else if (v.find("negr") != std::string::npos) {
            color = quantity == 1 ?
                "negra" : "negras";
        } else if (v.find("azul") != std::string::npos) {
            color = quantity == 1 ?
                "azul" : "azules";
        } else if (v.find("verde") != std::string::npos) {
            color = quantity == 1 ?
                "verde" : "verdes";
        } else if (v.find("naranja") != std::string::npos) {
            color = quantity == 1 ?
                "naranja" : "naranjas";
        } else if (v.find("rosa") != std::string::npos) {
            color = quantity == 1 ?
                "rosa" : "rosas";
        }

        std::string out = number_text(quantity);

        if (!shape.empty()) {
            out += " ";
            out += shape;

            if (!color.empty()) {
                out += " ";
                out += color;
            }

            return out;
        }

        out += quantity == 1 ?
            " pastilla" : " pastillas";

        if (!v.empty()) {
            out += " de aspecto ";
            out += v;
        }

        return out;
    };

    auto join_natural =
        [](const std::vector<std::string>& parts)
            -> std::string {

        if (parts.empty()) {
            return {};
        }

        if (parts.size() == 1) {
            return parts[0];
        }

        std::string out;

        for (size_t i = 0; i < parts.size(); ++i) {
            if (i > 0) {
                if (i + 1 == parts.size()) {
                    out += " y ";
                } else {
                    out += ", ";
                }
            }

            out += parts[i];
        }

        return out;
    };

    auto build_content =
        [&](const PillboxGroup& group)
            -> std::string {

        struct VisualItem {
            std::string visual;
            int quantity{0};
        };

        std::vector<VisualItem> items;
        int total_quantity = 0;

        for (const auto* routine : group.routines) {
            if (routine == nullptr) {
                continue;
            }

            int quantity = 1;
            std::string visual;

            read_visual(*routine, quantity, visual);

            total_quantity += quantity;

            bool merged = false;

            for (auto& item : items) {
                if (Normalize(item.visual) ==
                    Normalize(visual)) {

                    item.quantity += quantity;
                    merged = true;
                    break;
                }
            }

            if (!merged) {
                VisualItem item;
                item.visual = visual;
                item.quantity = quantity;
                items.push_back(item);
            }
        }

        std::vector<std::string> descriptions;

        for (const auto& item : items) {
            descriptions.push_back(
                describe_visual(
                    item.visual,
                    item.quantity));
        }

        std::string message =
            "Contiene ";

        message += number_text(total_quantity);

        message += total_quantity == 1 ?
            " pastilla" : " pastillas";

        if (!descriptions.empty()) {
            message += ": ";
            message += join_natural(descriptions);
        }

        message += ".";

        return message;
    };

    const int now_minutes =
        static_cast<int>(local_tm.tm_hour) * 60 +
        static_cast<int>(local_tm.tm_min);

    int current_index = -1;

    // DP-036 / DP-035 corregida:
    //
    // "Que me toca?" NUNCA retrocede en el reloj.
    //
    // current_index:
    //   primer casillero pendiente cuyo horario sea >= ahora.
    //
    // review_group:
    //   casillero pasado mas reciente que sigue sin verificar.
    //
    // Un horario pasado sin confirmacion NO significa missed.
    // Primero debe resolverse mediante confirmacion visual.
    const PillboxGroup* review_group = nullptr;

    if (day_key == "today") {

        for (size_t i = 0;
             i < visible.size();
             ++i) {

            const int scheduled =
                static_cast<int>(
                    visible[i]->time.hour) * 60 +
                static_cast<int>(
                    visible[i]->time.minute);

            if (scheduled >= now_minutes) {

                if (current_index < 0) {
                    current_index =
                        static_cast<int>(i);
                }

            } else {

                // visible esta ordenado cronologicamente.
                // Al sobrescribir conservamos el horario pasado
                // mas reciente aun sin verificar.
                review_group =
                    visible[i];
            }
        }
    }

    // DP-036 - Orientacion temporal:
    // repetir siempre solo el dia de la semana.
    auto spoken_weekday =
        [](const std::tm& value) -> std::string {

        static const char* kDays[] = {
            "domingo",
            "lunes",
            "martes",
            "miercoles",
            "jueves",
            "viernes",
            "sabado"
        };

        if (value.tm_wday < 0 ||
            value.tm_wday > 6) {
            return {};
        }

        return kDays[value.tm_wday];
    };

    std::string orientation_message =
        "Hoy es ";

    orientation_message +=
        spoken_weekday(local_tm);

    orientation_message += ". ";

    if (day_key == "tomorrow") {
        orientation_message +=
            "Manana es ";

        orientation_message +=
            spoken_weekday(target_tm);

        orientation_message += ". ";
    }

    std::string safe_message;

    if (day_key == "tomorrow") {

        if (visible.empty()) {
            safe_message =
                "Manana no tenes casilleros del pastillero programados.";

        } else {
            std::vector<std::string> schedule_parts;

            for (const auto* group : visible) {
                if (group == nullptr) {
                    continue;
                }

                std::string part =
                    "el ";

                part +=
                    compartment_phrase(
                        group->compartment);

                part +=
                    " a las ";

                part +=
                    spoken_time(
                        group->time);

                schedule_parts.push_back(
                    part);
            }

            safe_message =
                "Manana te corresponden ";

            safe_message +=
                join_natural(
                    schedule_parts);

            safe_message +=
                ".";
        }

    } else {

        // --------------------------------------------------
        // PRIMERO: informar solamente la proxima toma valida.
        // --------------------------------------------------
        if (current_index >= 0 &&
            static_cast<size_t>(
                current_index) < visible.size()) {

            const PillboxGroup& current =
                *visible[
                    static_cast<size_t>(
                        current_index)];

            safe_message =
                "A las ";

            safe_message +=
                spoken_time(
                    current.time);

            safe_message +=
                " te corresponde el ";

            safe_message +=
                compartment_phrase(
                    current.compartment);

            safe_message +=
                ". ";

            safe_message +=
                build_content(
                    current);

        } else {

            // No existe ningun horario pendiente hacia adelante.
            safe_message =
                "Por hoy no te toca nada mas. "
                "La proxima toma es manana.";
        }

        // --------------------------------------------------
        // SEGUNDO: revisar UNA toma pasada sin verificar.
        //
        // Nunca decimos que debe tomarla ahora.
        // Solo preguntamos que ocurrio.
        // --------------------------------------------------
        if (review_group != nullptr) {

            safe_message +=
                " El ";

            safe_message +=
                compartment_phrase(
                    review_group->compartment);

            safe_message +=
                " de las ";

            safe_message +=
                spoken_time(
                    review_group->time);

            safe_message +=
                " quedo sin confirmar. "
                "¿Lo tomaste?";
        }
    }

    safe_message =
        orientation_message +
        safe_message;

    cJSON* root = cJSON_CreateObject();

    cJSON_AddBoolToObject(
        root, "ok", true);

    cJSON_AddStringToObject(
        root, "source",
        "xiaozhi_care_pillbox");

    cJSON_AddStringToObject(
        root, "mode",
        "contextual_pillbox");

    cJSON_AddStringToObject(
        root, "day",
        day_key.c_str());

    cJSON_AddStringToObject(
        root, "date",
        resolved_date.c_str());

    cJSON_AddNumberToObject(
        root, "weekday",
        resolved_weekday);

    cJSON_AddStringToObject(
        root, "system_time",
        FormatDailyTimeText(
            daily::DailyTime(
                static_cast<uint8_t>(
                    local_tm.tm_hour),
                static_cast<uint8_t>(
                    local_tm.tm_min))).c_str());

    cJSON_AddNumberToObject(
        root, "remaining_compartments",
        static_cast<double>(
            visible.size()));

    if (day_key == "today" &&
        current_index >= 0 &&
        static_cast<size_t>(current_index) < visible.size()) {

        const PillboxGroup& current_group =
            *visible[static_cast<size_t>(current_index)];

        std::string current_group_id =
            resolved_date;

        current_group_id += "|";
        current_group_id +=
            FormatDailyTimeText(current_group.time);

        current_group_id += "|";
        current_group_id +=
            current_group.compartment_key;

        cJSON_AddStringToObject(
            root,
            "current_group_id",
            current_group_id.c_str());
    }

    if (day_key == "today" &&
        review_group != nullptr) {

        std::string review_group_id =
            resolved_date;

        review_group_id += "|";
        review_group_id +=
            FormatDailyTimeText(
                review_group->time);

        review_group_id += "|";
        review_group_id +=
            review_group->compartment_key;

        cJSON_AddStringToObject(
            root,
            "review_group_id",
            review_group_id.c_str());

        cJSON_AddStringToObject(
            root,
            "review_status",
            "past_unverified");

        cJSON_AddBoolToObject(
            root,
            "review_requires_visual_confirmation",
            true);

        std::string review_question =
            "El ";

        review_question +=
            compartment_phrase(
                review_group->compartment);

        review_question +=
            " de las ";

        review_question +=
            spoken_time(
                review_group->time);

        review_question +=
            " quedo sin confirmar. ¿Lo tomaste?";

        cJSON_AddStringToObject(
            root,
            "review_question",
            review_question.c_str());

        std::string visual_question =
            "Fijate en el ";

        visual_question +=
            compartment_phrase(
                review_group->compartment);

        visual_question +=
            ". ";

        visual_question +=
            build_content(
                *review_group);

        visual_question +=
            " ¿Todavia estan esas pastillas?";

        cJSON_AddStringToObject(
            root,
            "review_visual_question",
            visual_question.c_str());

        cJSON_AddStringToObject(
            root,
            "review_empty_means",
            "confirmed");

        cJSON_AddStringToObject(
            root,
            "review_contains_pills_means",
            "missed");
    }

    cJSON_AddStringToObject(
        root, "safe_message",
        safe_message.c_str());

    cJSON_AddBoolToObject(
        root, "must_answer_from_safe_message",
        true);

    cJSON_AddStringToObject(
        root, "instruction",
        "RESULTADO AUTORITATIVO DEL FIRMWARE. "
        "Responde SOLO con safe_message. "
        "No nombres medicamentos especificos. "
        "Para hoy, informa como lo que toca solamente el primer casillero pendiente cuyo horario sea igual o posterior a la hora actual. "
        "Nunca presentes una toma pasada como algo que deba tomar ahora. "
        "Para manana, enumera todos los casilleros y horarios sin describir su contenido.");

    cJSON_AddStringToObject(
        root, "follow_up_instruction",
        "Si existe review_group_id, la pregunta final sobre una toma pasada se refiere a ese grupo. "
        "Una toma pasada NO debe confirmarse solo porque la persona diga que cree haberla tomado. "
        "Si no recuerda o existe duda, responde exactamente con review_visual_question. "
        "Cuando confirme visualmente que el casillero esta vacio o que las pastillas siguen presentes, "
        "llama care.verify_pillbox_group usando EXACTAMENTE review_group_id. "
        "No inventes ni reconstruyas identificadores.");

    cJSON* groups_json =
        cJSON_AddArrayToObject(
            root, "groups");

    for (size_t i = 0;
         i < visible.size() &&
         i < kMaxMcpItems;
         ++i) {

        const PillboxGroup& group =
            *visible[i];

        cJSON* item =
            cJSON_CreateObject();

        std::string group_id =
            resolved_date;

        group_id += "|";
        group_id +=
            FormatDailyTimeText(group.time);

        group_id += "|";
        group_id +=
            group.compartment_key;

        cJSON_AddStringToObject(
            item,
            "group_id",
            group_id.c_str());

        cJSON_AddStringToObject(
            item, "time",
            FormatDailyTimeText(
                group.time).c_str());

        cJSON_AddStringToObject(
            item, "compartment",
            compartment_phrase(
                group.compartment).c_str());

        const bool is_current =
            day_key == "today" &&
            current_index >= 0 &&
            static_cast<size_t>(
                current_index) == i;

        cJSON_AddBoolToObject(
            item, "current_group",
            is_current);

        cJSON_AddBoolToObject(
            item, "content_may_be_spoken",
            is_current);

        cJSON* ids =
            cJSON_AddArrayToObject(
                item, "routine_ids");

        for (const auto* routine :
             group.routines) {

            if (routine == nullptr) {
                continue;
            }

            cJSON_AddItemToArray(
                ids,
                cJSON_CreateString(
                    routine->id.Str().c_str()));
        }

        if (is_current) {
            cJSON_AddStringToObject(
                item, "content_description",
                build_content(group).c_str());
        }

        cJSON_AddItemToArray(
            groups_json, item);
    }

    ESP_LOGI(
        "CARE_MCP",
        "Pillbox plan: day=%s date=%s weekday=%u groups=%u current=%d",
        day_key.c_str(),
        resolved_date.c_str(),
        static_cast<unsigned>(
            resolved_weekday),
        static_cast<unsigned>(
            visible.size()),
        current_index);

    return Stringify(root);
}



// -----------------------------------------------------------------------------
// DP-036 - Confirmacion atomica de un casillero
//
// group_id:
//     YYYY-MM-DD|HH:MM|casillero-normalizado
//
// El identificador DEBE provenir de GetPillboxPlan().
// Solo se permite confirmar un grupo correspondiente al dia actual.
// Todas las rutinas de medicacion pertenecientes al mismo horario/casillero
// se registran como confirmed.
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// DP-036 - Verificacion visual de una toma pasada
//
// result:
//   empty      -> casillero vacio -> confirmed
//   contains   -> las pastillas siguen presentes -> missed
//   uncertain  -> no modificar historial
//
// Esta herramienta SOLO resuelve grupos cuyo horario ya paso.
// -----------------------------------------------------------------------------
std::string CareMcpService::VerifyPillboxGroup(
    const std::string& group_id,
    const std::string& result) {

    if (group_id.empty()) {
        return ErrorJson(
            "PILLBOX_GROUP_ID_REQUIRED");
    }

    std::string verification =
        Normalize(result);

    if (verification == "vacio" ||
        verification == "vacia" ||
        verification == "sin pastillas" ||
        verification == "no tiene") {

        verification = "empty";

    } else if (
        verification == "tiene" ||
        verification == "hay" ||
        verification == "con pastillas" ||
        verification == "tiene pastillas") {

        verification = "contains";

    } else if (
        verification == "no se" ||
        verification == "no sé" ||
        verification == "duda" ||
        verification == "parcial" ||
        verification == "algunas") {

        verification = "uncertain";
    }

    if (verification != "empty" &&
        verification != "contains" &&
        verification != "uncertain") {

        return ErrorJson(
            "INVALID_PILLBOX_VISUAL_RESULT");
    }

    const size_t first_sep =
        group_id.find('|');

    const size_t second_sep =
        first_sep == std::string::npos
            ? std::string::npos
            : group_id.find(
                '|',
                first_sep + 1);

    if (first_sep == std::string::npos ||
        second_sep == std::string::npos ||
        second_sep + 1 >= group_id.size()) {

        return ErrorJson(
            "INVALID_PILLBOX_GROUP_ID");
    }

    const std::string requested_date =
        group_id.substr(
            0,
            first_sep);

    const std::string requested_time =
        group_id.substr(
            first_sep + 1,
            second_sep - first_sep - 1);

    const std::string requested_compartment =
        Normalize(
            group_id.substr(
                second_sep + 1));

    daily::DailyTime scheduled_time;

    if (!ParseDailyTimeText(
            requested_time,
            scheduled_time)) {

        return ErrorJson(
            "INVALID_PILLBOX_GROUP_TIME");
    }

    std::tm local_tm{};

    if (!GetLocalClock(
            local_tm)) {

        return ErrorJson(
            "DEVICE_CLOCK_UNAVAILABLE");
    }

    const std::string today =
        FormatDate(
            local_tm.tm_year + 1900,
            local_tm.tm_mon + 1,
            local_tm.tm_mday);

    if (requested_date != today) {
        return ErrorJson(
            "PILLBOX_REVIEW_NOT_TODAY");
    }

    const int now_minutes =
        local_tm.tm_hour * 60 +
        local_tm.tm_min;

    const int scheduled_minutes =
        static_cast<int>(
            scheduled_time.hour) * 60 +
        static_cast<int>(
            scheduled_time.minute);

    // Una comprobacion visual solo tiene sentido
    // para un horario que YA PASO.
    if (scheduled_minutes >= now_minutes) {
        return ErrorJson(
            "PILLBOX_REVIEW_NOT_PAST");
    }

    static const char* kDays[] = {
        "domingo",
        "lunes",
        "martes",
        "miercoles",
        "jueves",
        "viernes",
        "sabado"
    };

    std::string day_name =
        "hoy";

    if (local_tm.tm_wday >= 0 &&
        local_tm.tm_wday <= 6) {

        day_name =
            kDays[local_tm.tm_wday];
    }

    if (verification == "uncertain") {

        cJSON* root =
            cJSON_CreateObject();

        cJSON_AddBoolToObject(
            root,
            "ok",
            true);

        cJSON_AddBoolToObject(
            root,
            "recorded",
            false);

        cJSON_AddStringToObject(
            root,
            "state",
            "past_unverified");

        std::string safe =
            "Hoy es ";

        safe += day_name;

        safe +=
            ". No voy a marcar esa toma. "
            "La dejamos sin verificar.";

        cJSON_AddStringToObject(
            root,
            "safe_message",
            safe.c_str());

        cJSON_AddBoolToObject(
            root,
            "must_answer_from_safe_message",
            true);

        return Stringify(root);
    }

    const uint8_t weekday =
        static_cast<uint8_t>(
            IsoWeekday(
                local_tm.tm_year + 1900,
                local_tm.tm_mon + 1,
                local_tm.tm_mday));

    daily::NvsRoutineRepository
        routine_repository;

    if (!routine_repository.Init()) {
        return ErrorJson(
            "ROUTINE_REPOSITORY_INIT_FAILED");
    }

    daily::NvsRoutineExecutionRepository
        execution_repository;

    if (!execution_repository.Init()) {
        return ErrorJson(
            "EXECUTION_REPOSITORY_INIT_FAILED");
    }

    const std::vector<daily::CareRoutine>
        routines =
            routine_repository.List();

    const std::vector<daily::RoutineExecution>
        executions =
            execution_repository.List();

    auto routine_for_weekday =
        [](const daily::CareRoutine& routine,
           uint8_t iso_weekday) -> bool {

        if (iso_weekday < 1 ||
            iso_weekday > 7 ||
            !routine.IsValid()) {

            return false;
        }

        if (routine.state !=
            daily::RoutineState::Active) {

            return false;
        }

        if (routine.schedule.repeat ==
            daily::RoutineRepeatType::Daily) {

            return true;
        }

        if (routine.schedule.repeat ==
            daily::RoutineRepeatType::SpecificWeekdays) {

            return routine.schedule.weekdays[
                static_cast<size_t>(
                    iso_weekday - 1)];
        }

        return false;
    };

    auto latest_execution_for =
        [&](const daily::RoutineId& routine_id)
            -> const daily::RoutineExecution* {

        const daily::RoutineExecution* latest =
            nullptr;

        for (const auto& execution :
             executions) {

            if (execution.iso_date != today ||
                execution.routine_id !=
                    routine_id) {

                continue;
            }

            if (latest == nullptr ||
                latest->time <=
                    execution.time) {

                latest =
                    &execution;
            }
        }

        return latest;
    };

    const std::string target_event =
        verification == "empty"
            ? "confirmed"
            : "missed";

    size_t matched = 0;
    size_t recorded = 0;
    size_t already_resolved = 0;

    std::string spoken_compartment;

    for (const auto& routine :
         routines) {

        if (!routine_for_weekday(
                routine,
                weekday)) {

            continue;
        }

        if (routine.type !=
            daily::RoutineType::Medication) {

            continue;
        }

        if (routine.placement.type !=
            daily::PlacementType::Pillbox) {

            continue;
        }

        if (Normalize(
                routine.placement.compartment) !=
            requested_compartment) {

            continue;
        }

        if (FormatDailyTimeText(
                routine.schedule.time) !=
            requested_time) {

            continue;
        }

        ++matched;

        if (spoken_compartment.empty()) {

            const std::string c =
                requested_compartment;

            if (c == "primero" ||
                c == "primer" ||
                c == "1" ||
                c == "uno") {

                spoken_compartment =
                    "primer casillero";

            } else if (
                c == "segundo" ||
                c == "2" ||
                c == "dos") {

                spoken_compartment =
                    "segundo casillero";

            } else if (
                c == "tercero" ||
                c == "tercer" ||
                c == "3" ||
                c == "tres") {

                spoken_compartment =
                    "tercer casillero";

            } else if (
                c == "cuarto" ||
                c == "4" ||
                c == "cuatro") {

                spoken_compartment =
                    "cuarto casillero";

            } else {

                spoken_compartment =
                    requested_compartment;

                if (spoken_compartment.find(
                        "casillero") ==
                    std::string::npos) {

                    spoken_compartment +=
                        " casillero";
                }
            }
        }

        const daily::RoutineExecution* latest =
            latest_execution_for(
                routine.id);

        // Si ya fue resuelta por alarma, voz, web
        // o hardware, una consulta vieja no la pisa.
        if (latest != nullptr &&
            (latest->event ==
                 daily::RoutineExecutionEvent::Confirmed ||
             latest->event ==
                 daily::RoutineExecutionEvent::Missed ||
             latest->event ==
                 daily::RoutineExecutionEvent::Skipped)) {

            ++already_resolved;
            continue;
        }

        const std::string record_result =
            RecordRoutineExecution(
                routine.id.Str(),
                target_event,
                verification == "empty"
                    ? "Verificacion visual: casillero vacio."
                    : "Verificacion visual: pastillas presentes.");

        cJSON* record_json =
            cJSON_Parse(
                record_result.c_str());

        bool record_ok = false;

        if (record_json != nullptr) {

            const cJSON* ok =
                cJSON_GetObjectItemCaseSensitive(
                    record_json,
                    "ok");

            record_ok =
                cJSON_IsTrue(ok);

            cJSON_Delete(
                record_json);
        }

        if (record_ok) {
            ++recorded;
        }
    }

    if (matched == 0) {
        return ErrorJson(
            "PILLBOX_GROUP_NOT_FOUND");
    }

    if (spoken_compartment.empty()) {
        spoken_compartment =
            "casillero indicado";
    }

    cJSON* root =
        cJSON_CreateObject();

    cJSON_AddBoolToObject(
        root,
        "ok",
        true);

    cJSON_AddStringToObject(
        root,
        "group_id",
        group_id.c_str());

    cJSON_AddStringToObject(
        root,
        "visual_result",
        verification.c_str());

    cJSON_AddStringToObject(
        root,
        "resulting_state",
        verification == "empty"
            ? "confirmed"
            : "missed");

    cJSON_AddNumberToObject(
        root,
        "routine_count",
        static_cast<double>(
            matched));

    cJSON_AddNumberToObject(
        root,
        "recorded_count",
        static_cast<double>(
            recorded));

    cJSON_AddNumberToObject(
        root,
        "already_resolved_count",
        static_cast<double>(
            already_resolved));

    std::string safe =
        "Hoy es ";

    safe +=
        day_name;

    safe += ". ";

    if (already_resolved == matched) {

        safe +=
            "Ese casillero ya estaba resuelto.";

    } else if (verification == "empty") {

        safe +=
            "Perfecto. Dejo anotado que la toma del ";

        safe +=
            spoken_compartment;

        safe +=
            " fue realizada.";

    } else {

        safe +=
            "Entendido. Dejo anotado que la toma del ";

        safe +=
            spoken_compartment;

        safe +=
            " no se realizo.";
    }

    cJSON_AddStringToObject(
        root,
        "safe_message",
        safe.c_str());

    cJSON_AddBoolToObject(
        root,
        "must_answer_from_safe_message",
        true);

    cJSON_AddStringToObject(
        root,
        "instruction",
        "Responde SOLO con safe_message. "
        "No nombres medicamentos. "
        "No indiques tomar ahora una medicacion cuyo horario ya paso.");

    ESP_LOGI(
        "CARE_MCP",
        "Pillbox visual verification: id=%s result=%s matched=%u recorded=%u already=%u",
        group_id.c_str(),
        verification.c_str(),
        static_cast<unsigned>(
            matched),
        static_cast<unsigned>(
            recorded),
        static_cast<unsigned>(
            already_resolved));

    return Stringify(root);
}


std::string CareMcpService::ConfirmPillboxGroup(
    const std::string& group_id) {

    if (group_id.empty()) {
        return ErrorJson("PILLBOX_GROUP_ID_REQUIRED");
    }

    const size_t first_sep =
        group_id.find('|');

    const size_t second_sep =
        first_sep == std::string::npos
            ? std::string::npos
            : group_id.find('|', first_sep + 1);

    if (first_sep == std::string::npos ||
        second_sep == std::string::npos ||
        second_sep + 1 >= group_id.size()) {

        return ErrorJson("INVALID_PILLBOX_GROUP_ID");
    }

    const std::string requested_date =
        group_id.substr(0, first_sep);

    const std::string requested_time =
        group_id.substr(
            first_sep + 1,
            second_sep - first_sep - 1);

    const std::string requested_compartment =
        Normalize(
            group_id.substr(second_sep + 1));

    if (requested_time.size() != 5 ||
        requested_time[2] != ':') {

        return ErrorJson("INVALID_PILLBOX_GROUP_TIME");
    }

    std::tm local_tm{};

    if (!GetLocalClock(local_tm)) {
        return ErrorJson("DEVICE_CLOCK_UNAVAILABLE");
    }

    const std::string today =
        FormatDate(
            local_tm.tm_year + 1900,
            local_tm.tm_mon + 1,
            local_tm.tm_mday);

    // Seguridad: esta herramienta solo confirma tomas de HOY.
    if (requested_date != today) {
        return ErrorJson("PILLBOX_CONFIRMATION_NOT_TODAY");
    }

    const uint8_t weekday =
        static_cast<uint8_t>(
            IsoWeekday(
                local_tm.tm_year + 1900,
                local_tm.tm_mon + 1,
                local_tm.tm_mday));

    daily::NvsRoutineRepository routine_repository;

    if (!routine_repository.Init()) {
        return ErrorJson(
            "ROUTINE_REPOSITORY_INIT_FAILED");
    }

    daily::NvsRoutineExecutionRepository execution_repository;

    if (!execution_repository.Init()) {
        return ErrorJson(
            "EXECUTION_REPOSITORY_INIT_FAILED");
    }

    const std::vector<daily::CareRoutine> routines =
        routine_repository.List();

    const std::vector<daily::RoutineExecution> executions =
        execution_repository.List();

    auto routine_for_weekday =
        [](const daily::CareRoutine& routine,
           uint8_t iso_weekday) -> bool {

        if (iso_weekday < 1 ||
            iso_weekday > 7 ||
            !routine.IsValid()) {
            return false;
        }

        if (routine.state !=
            daily::RoutineState::Active) {
            return false;
        }

        if (routine.schedule.repeat ==
            daily::RoutineRepeatType::Daily) {
            return true;
        }

        if (routine.schedule.repeat ==
            daily::RoutineRepeatType::SpecificWeekdays) {

            return routine.schedule.weekdays[
                static_cast<size_t>(
                    iso_weekday - 1)];
        }

        return false;
    };

    auto already_confirmed_today =
        [&](const daily::RoutineId& routine_id)
            -> bool {

        const daily::RoutineExecution* latest =
            nullptr;

        for (const auto& execution : executions) {

            if (execution.iso_date != today ||
                execution.routine_id != routine_id) {
                continue;
            }

            if (latest == nullptr ||
                latest->time <= execution.time) {

                latest = &execution;
            }
        }

        return latest != nullptr &&
               latest->event ==
                   daily::RoutineExecutionEvent::Confirmed;
    };

    size_t matched = 0;
    size_t confirmed_now = 0;
    size_t already_confirmed = 0;

    std::string spoken_compartment;

    for (const auto& routine : routines) {

        if (!routine_for_weekday(
                routine,
                weekday)) {
            continue;
        }

        if (routine.type !=
            daily::RoutineType::Medication) {
            continue;
        }

        if (routine.placement.type !=
            daily::PlacementType::Pillbox) {
            continue;
        }

        if (Normalize(
                routine.placement.compartment) !=
            requested_compartment) {
            continue;
        }

        if (FormatDailyTimeText(
                routine.schedule.time) !=
            requested_time) {
            continue;
        }

        ++matched;

        if (spoken_compartment.empty()) {
            const std::string c =
                requested_compartment;

            if (c == "primero" ||
                c == "primer" ||
                c == "1" ||
                c == "uno") {

                spoken_compartment =
                    "primer casillero";

            } else if (
                c == "segundo" ||
                c == "2" ||
                c == "dos") {

                spoken_compartment =
                    "segundo casillero";

            } else if (
                c == "tercero" ||
                c == "tercer" ||
                c == "3" ||
                c == "tres") {

                spoken_compartment =
                    "tercer casillero";

            } else if (
                c == "cuarto" ||
                c == "4" ||
                c == "cuatro") {

                spoken_compartment =
                    "cuarto casillero";

            } else {
                spoken_compartment =
                    requested_compartment;

                if (spoken_compartment.find(
                        "casillero") ==
                    std::string::npos) {

                    spoken_compartment +=
                        " casillero";
                }
            }
        }

        if (already_confirmed_today(
                routine.id)) {

            ++already_confirmed;
            continue;
        }

        const std::string result =
            RecordRoutineExecution(
                routine.id.Str(),
                "confirmed",
                "Confirmacion del casillero por voz.");

        bool record_ok = false;

        cJSON* result_json =
            cJSON_Parse(result.c_str());

        if (result_json != nullptr) {

            const cJSON* ok =
                cJSON_GetObjectItemCaseSensitive(
                    result_json,
                    "ok");

            record_ok =
                cJSON_IsTrue(ok);

            cJSON_Delete(result_json);
        }

        if (record_ok) {
            ++confirmed_now;
        }
    }

    if (matched == 0) {
        return ErrorJson(
            "PILLBOX_GROUP_NOT_FOUND");
    }

    if (spoken_compartment.empty()) {
        spoken_compartment =
            "casillero indicado";
    }

    cJSON* root =
        cJSON_CreateObject();

    cJSON_AddBoolToObject(
        root,
        "ok",
        true);

    cJSON_AddStringToObject(
        root,
        "group_id",
        group_id.c_str());

    cJSON_AddStringToObject(
        root,
        "date",
        today.c_str());

    cJSON_AddStringToObject(
        root,
        "time",
        requested_time.c_str());

    cJSON_AddStringToObject(
        root,
        "compartment",
        spoken_compartment.c_str());

    cJSON_AddNumberToObject(
        root,
        "routine_count",
        static_cast<double>(matched));

    cJSON_AddNumberToObject(
        root,
        "confirmed_now",
        static_cast<double>(confirmed_now));

    cJSON_AddNumberToObject(
        root,
        "already_confirmed",
        static_cast<double>(
            already_confirmed));

    std::string safe_message;

    if (confirmed_now == 0 &&
        already_confirmed == matched) {

        safe_message =
            "Ese casillero ya estaba anotado como tomado.";

    } else {
        safe_message =
            "Listo, deje anotado que ya tomaste el ";

        safe_message +=
            spoken_compartment;

        safe_message += ".";
    }

    cJSON_AddStringToObject(
        root,
        "safe_message",
        safe_message.c_str());

    cJSON_AddBoolToObject(
        root,
        "must_answer_from_safe_message",
        true);

    cJSON_AddStringToObject(
        root,
        "instruction",
        "Responde SOLO con safe_message. "
        "No nombres medicamentos ni agregues informacion.");

    ESP_LOGI(
        "CARE_MCP",
        "Pillbox group confirmed: id=%s matched=%u confirmed_now=%u already=%u",
        group_id.c_str(),
        static_cast<unsigned>(matched),
        static_cast<unsigned>(confirmed_now),
        static_cast<unsigned>(already_confirmed));

    return Stringify(root);
}


std::string CareMcpService::GetPendingToday(
    const std::string& date,
    int weekday,
    const std::string& time) {

    // ========================================================
    // 1. Obtener cuidados cotidianos pendientes.
    //
    // GetDailyStatus ya excluye medicacion en la version
    // actual de XiaoZhi Care.
    // ========================================================

    const std::string daily_json =
        GetDailyStatus(date, weekday, time, "pending");

    cJSON* daily_root =
        cJSON_Parse(daily_json.c_str());

    if (daily_root == nullptr) {
        return ErrorJson("PENDING_DAILY_JSON_PARSE_FAILED");
    }


    // Fecha autoritativa que resolvio GetDailyStatus.
    std::string resolved_date;

    const cJSON* date_item =
        cJSON_GetObjectItemCaseSensitive(
            daily_root,
            "date");

    if (cJSON_IsString(date_item) &&
        date_item->valuestring != nullptr) {

        resolved_date = date_item->valuestring;
    }

    if (resolved_date.empty()) {
        cJSON_Delete(daily_root);
        return ErrorJson("PENDING_DATE_MISSING");
    }


    // ========================================================
    // 2. Construir lista unica de actividades.
    // ========================================================

    std::vector<std::string> phrases;
    std::vector<std::string> keys;


    auto friendly_time =
        [](const std::string& value) -> std::string {

        // 20:00 -> 20
        if (value.size() == 5 &&
            value[2] == ':' &&
            value[3] == '0' &&
            value[4] == '0') {

            std::string result =
                value.substr(0, 2);

            if (result.size() == 2 &&
                result[0] == '0') {

                result.erase(0, 1);
            }

            return result;
        }

        return value;
    };


    auto add_phrase =
        [&](const std::string& title,
            const std::string& event_time) {

        if (title.empty()) {
            return;
        }

        // Evitar duplicados.
        std::string key =
            Normalize(title) +
            "|" +
            event_time;

        if (std::find(
                keys.begin(),
                keys.end(),
                key) != keys.end()) {

            return;
        }

        keys.push_back(key);

        std::string phrase = title;

        if (!event_time.empty()) {
            phrase += " a las ";
            phrase += friendly_time(event_time);
        }

        phrases.push_back(phrase);
    };


    // ========================================================
    // 3. Agregar cuidados cotidianos pendientes.
    // ========================================================

    size_t care_count = 0;

    const cJSON* routines =
        cJSON_GetObjectItemCaseSensitive(
            daily_root,
            "routines");

    if (cJSON_IsArray(routines)) {

        const int count =
            cJSON_GetArraySize(routines);

        for (int i = 0; i < count; ++i) {

            const cJSON* item =
                cJSON_GetArrayItem(routines, i);

            if (!cJSON_IsObject(item)) {
                continue;
            }


            // Blindaje adicional contra medicacion.
            const cJSON* type =
                cJSON_GetObjectItemCaseSensitive(
                    item,
                    "type");

            if (cJSON_IsString(type) &&
                type->valuestring != nullptr &&
                Normalize(type->valuestring) ==
                    "medication") {

                continue;
            }


            const cJSON* title =
                cJSON_GetObjectItemCaseSensitive(
                    item,
                    "title");

            if (!cJSON_IsString(title) ||
                title->valuestring == nullptr) {

                continue;
            }


            std::string scheduled_time;

            const cJSON* scheduled =
                cJSON_GetObjectItemCaseSensitive(
                    item,
                    "scheduled_time");

            if (cJSON_IsString(scheduled) &&
                scheduled->valuestring != nullptr) {

                scheduled_time =
                    scheduled->valuestring;
            }


            add_phrase(
                title->valuestring,
                scheduled_time);

            ++care_count;
        }
    }


    // ========================================================
    // 4. Agregar TODOS los recordatorios que corresponden hoy.
    //
    // IMPORTANTE:
    // aca NO usamos la ventana "Empezar a recordar".
    //
    // Esa ventana sirve para avisos anticipados.
    // "Que me falta hacer hoy" debe mostrar todo el dia.
    // ========================================================

    std::vector<CareReminder> reminders;

    const CareError reminder_result =
        CareManager::GetInstance().ListReminders(
            reminders);

    if (reminder_result != CareError::OK) {
        cJSON_Delete(daily_root);
        return ErrorJson(reminder_result);
    }


    size_t reminder_count = 0;

    for (const auto& reminder : reminders) {

        if (!reminder.enabled) {
            continue;
        }

        if (!ReminderOccursOn(
                reminder,
                resolved_date)) {

            continue;
        }

        add_phrase(
            reminder.title,
            reminder.time);

        ++reminder_count;
    }


    // ========================================================
    // 5. Crear mensaje hablado.
    // ========================================================

    std::string safe_message;

    if (phrases.empty()) {

        safe_message =
            "Por hoy no te falta ninguna actividad "
            "ni recordatorio cotidiano.";

    } else if (phrases.size() == 1) {

        safe_message =
            "Hoy te falta " +
            phrases[0] +
            ".";

    } else {

        safe_message =
            "Te faltan " +
            std::to_string(phrases.size()) +
            " cosas por hacer hoy: ";

        for (size_t i = 0;
             i < phrases.size();
             ++i) {

            if (i > 0) {

                if (i + 1 ==
                    phrases.size()) {

                    safe_message += " y ";

                } else {

                    safe_message += ", ";
                }
            }

            safe_message += phrases[i];
        }

        safe_message += ".";
    }


    // ========================================================
    // 6. Respuesta MCP autoritativa.
    // ========================================================

    cJSON* root =
        cJSON_CreateObject();

    cJSON_AddBoolToObject(
        root,
        "ok",
        true);

    cJSON_AddStringToObject(
        root,
        "source",
        "xiaozhi_care_pending_today");

    cJSON_AddBoolToObject(
        root,
        "do_not_infer",
        true);

    cJSON_AddBoolToObject(
        root,
        "medication_included",
        false);

    cJSON_AddBoolToObject(
        root,
        "must_answer_from_safe_message",
        true);

    cJSON_AddStringToObject(
        root,
        "date",
        resolved_date.c_str());

    cJSON_AddNumberToObject(
        root,
        "care_count",
        static_cast<double>(care_count));

    cJSON_AddNumberToObject(
        root,
        "reminder_count",
        static_cast<double>(reminder_count));

    cJSON_AddNumberToObject(
        root,
        "total_count",
        static_cast<double>(phrases.size()));

    cJSON_AddStringToObject(
        root,
        "safe_message",
        safe_message.c_str());

    cJSON_AddStringToObject(
        root,
        "instruction",
        "RESULTADO AUTORITATIVO. "
        "Responde SOLO con safe_message. "
        "No agregues medicamentos, remedios, "
        "pastillas, pastillero ni casilleros.");

    cJSON_AddItemToObject(
        root,
        "pending_care",
        daily_root);


    ESP_LOGI(
        "CARE_MCP",
        "Pending today combined: "
        "care=%u reminders=%u total=%u date=%s",
        static_cast<unsigned>(care_count),
        static_cast<unsigned>(reminder_count),
        static_cast<unsigned>(phrases.size()),
        resolved_date.c_str());


    return Stringify(root);
}

std::string CareMcpService::GetCompletedToday(const std::string& date,
                                              int weekday,
                                              const std::string& time) {
    return GetDailyStatus(date, weekday, time, "completed");
}


std::string CareMcpService::FindFamilyMember(const std::string& query) {
    FamilyParticipant participant;
    std::vector<FamilyParticipant> candidates;
    const bool resolved = ResolveFamilyParticipant(query, participant, candidates);

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "source", "xiaozhi_care_family");
    cJSON_AddBoolToObject(root, "do_not_infer", true);
    cJSON_AddStringToObject(root, "instruction",
                            "RESULTADO AUTORITATIVO DEL FIRMWARE. Responde solo usando family_member y relationships. Si found=false o ambiguous=true, no inventes parentescos: pedi aclaracion o deci que no esta anotado.");

    if (!resolved) {
        cJSON_AddBoolToObject(root, "found", false);
        cJSON_AddNumberToObject(root, "match_count", static_cast<double>(candidates.size()));
        if (candidates.size() > 1) {
            cJSON_AddBoolToObject(root, "ambiguous", true);
            cJSON* array = cJSON_AddArrayToObject(root, "candidates");
            for (const auto& candidate : candidates) {
                cJSON* item = cJSON_CreateObject();
                cJSON_AddStringToObject(item, "id", candidate.id.c_str());
                AddStringIfNotEmpty(item, "name", candidate.name);
                AddStringIfNotEmpty(item, "nickname", candidate.nickname);
                AddStringIfNotEmpty(item, "relationship", candidate.relationship);
                cJSON_AddItemToArray(array, item);
            }
            cJSON_AddStringToObject(root, "safe_message", "Encontré más de una persona posible. Necesito que me confirmes a cuál te referís.");
        } else {
            cJSON_AddBoolToObject(root, "ambiguous", false);
            cJSON_AddStringToObject(root, "safe_message", "No tengo esa persona anotada en la familia.");
        }
        ESP_LOGI("CARE_MCP", "Family member query: query=%s found=0 candidates=%u",
                 query.c_str(), static_cast<unsigned>(candidates.size()));
        return Stringify(root);
    }

    family::NvsFamilyRelationshipRepository& repo = family::GetFamilyRelationshipRepository();
    if (!repo.IsReady() && !repo.Init()) {
        return ErrorJson("FAMILY_REPOSITORY_NOT_READY");
    }
    const std::vector<family::FamilyRelationship> relationships = repo.List();

    cJSON_AddBoolToObject(root, "found", true);
    cJSON_AddBoolToObject(root, "ambiguous", false);
    AddFamilyParticipantJson(root, "family_member", participant);
    AddStringIfNotEmpty(root, "pet_summary", ParticipantPetSummary(participant));
    cJSON* array = cJSON_AddArrayToObject(root, "relationships");

    size_t count = 0;
    for (const auto& relationship : relationships) {
        if (!relationship.enabled) continue;
        if (relationship.from_person_id == participant.id || relationship.to_person_id == participant.id) {
            if (count < kMaxMcpItems) {
                AddRelationshipJson(array, relationship);
            }
            ++count;
        }
    }

    cJSON_AddNumberToObject(root, "relationship_count", static_cast<double>(count));
    cJSON_AddBoolToObject(root, "truncated", count > kMaxMcpItems);

    std::string safe_message;
    if (count == 0) {
        safe_message = ParticipantDisplayName(participant) +
                       " está anotada, pero no tengo relaciónes familiares cargadas para esa persona.";
    } else {
        safe_message = ParticipantDisplayName(participant) + " está anotada en la familia y tiene " +
                       std::to_string(count) +
                       (count == 1 ? " relación familiar registrada." : " relaciónes familiares registradas.");
    }
    cJSON_AddStringToObject(root, "safe_message", safe_message.c_str());

    ESP_LOGI("CARE_MCP", "Family member query: query=%s id=%s relationships=%u",
             query.c_str(), participant.id.c_str(), static_cast<unsigned>(count));
    return Stringify(root);
}

std::string CareMcpService::GetFamilyRelationships(const std::string& person) {
    family::NvsFamilyRelationshipRepository& repo = family::GetFamilyRelationshipRepository();
    if (!repo.IsReady() && !repo.Init()) {
        return ErrorJson("FAMILY_REPOSITORY_NOT_READY");
    }

    FamilyParticipant filter;
    std::vector<FamilyParticipant> candidates;
    const bool has_filter = !Normalize(person).empty();
    bool resolved_filter = false;
    if (has_filter) {
        resolved_filter = ResolveFamilyParticipant(person, filter, candidates);
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "source", "xiaozhi_care_family");
    cJSON_AddBoolToObject(root, "do_not_infer", true);
    cJSON_AddStringToObject(root, "instruction",
                            "RESULTADO AUTORITATIVO DEL FIRMWARE. Responde solo con las relaciónes devueltas. No inventes parentescos. Si found=false, di que no esta anotado.");

    if (has_filter && !resolved_filter) {
        cJSON_AddBoolToObject(root, "found", false);
        cJSON_AddStringToObject(root, "safe_message", "No tengo relaciónes familiares anotadas para esa persona.");
        cJSON_AddNumberToObject(root, "relationship_count", 0);
        return Stringify(root);
    }

    if (resolved_filter) {
        AddFamilyParticipantJson(root, "participant", filter);
    }

    const std::vector<family::FamilyRelationship> relationships = repo.List();
    cJSON* array = cJSON_AddArrayToObject(root, "relationships");
    size_t count = 0;
    for (const auto& relationship : relationships) {
        if (!relationship.enabled) continue;
        if (resolved_filter && relationship.from_person_id != filter.id && relationship.to_person_id != filter.id) {
            continue;
        }
        if (count < kMaxMcpItems) {
            AddRelationshipJson(array, relationship);
        }
        ++count;
    }

    cJSON_AddBoolToObject(root, "found", count > 0);
    cJSON_AddNumberToObject(root, "relationship_count", static_cast<double>(count));
    cJSON_AddBoolToObject(root, "truncated", count > kMaxMcpItems);

    std::string safe_message;
    if (count == 0) {
        safe_message = resolved_filter ?
            "No tengo relaciónes familiares anotadas para esa persona." :
            "No tengo relaciónes familiares anotadas.";
    } else if (resolved_filter) {
        safe_message = "Tengo " + std::to_string(count) +
                       (count == 1 ? " relación familiar anotada para " : " relaciónes familiares anotadas para ") +
                       ParticipantDisplayName(filter) + ".";
    } else {
        safe_message = "Tengo " + std::to_string(count) +
                       (count == 1 ? " relación familiar anotada." : " relaciónes familiares anotadas.");
    }
    cJSON_AddStringToObject(root, "safe_message", safe_message.c_str());

    ESP_LOGI("CARE_MCP", "Family relationships query: person=%s matches=%u",
             person.c_str(), static_cast<unsigned>(count));
    return Stringify(root);
}

std::string CareMcpService::GetPersonConnections(const std::string& person) {
    return GetFamilyRelationships(person);
}

std::string CareMcpService::WhoIsFamilyMember(const std::string& query) {
    return FindFamilyMember(query);
}

namespace {

bool AddUniqueId(std::vector<std::string>& ids, const std::string& id) {
    if (id.empty()) return false;
    if (std::find(ids.begin(), ids.end(), id) != ids.end()) return false;
    ids.push_back(id);
    return true;
}

std::string JoinDisplayNames(const std::vector<std::string>& names) {
    if (names.empty()) return {};
    if (names.size() == 1) return names.front();

    std::string out;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i > 0) {
            out += (i + 1 == names.size()) ? " y " : ", ";
        }
        out += names[i];
    }
    return out;
}

std::string BuildRelativeQuery(const std::string& person, const std::string& kind) {
    family::NvsFamilyRelationshipRepository& repo = family::GetFamilyRelationshipRepository();
    if (!repo.IsReady() && !repo.Init()) {
        return ErrorJson("FAMILY_REPOSITORY_NOT_READY");
    }

    FamilyParticipant base;
    std::vector<FamilyParticipant> candidates;
    const std::string person_query = Normalize(person).empty() ? std::string("profile") : person;
    const bool resolved = ResolveFamilyParticipant(person_query, base, candidates);

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "source", "xiaozhi_care_family");
    cJSON_AddBoolToObject(root, "do_not_infer", true);
    cJSON_AddStringToObject(root, "relative_kind", kind.c_str());
    cJSON_AddStringToObject(root, "instruction",
                            "RESULTADO AUTORITATIVO DEL FIRMWARE. Responde solo con relatives y safe_message. Si found=false o relative_count=0, no inventes familiares ni parentescos.");

    if (!resolved) {
        cJSON_AddBoolToObject(root, "found", false);
        cJSON_AddBoolToObject(root, "ambiguous", candidates.size() > 1);
        cJSON_AddNumberToObject(root, "relative_count", 0);
        cJSON_AddStringToObject(root, "safe_message", "No tengo esa persona anotada en Familia.");
        return Stringify(root);
    }

    AddFamilyParticipantJson(root, "person", base);
    cJSON* relatives = cJSON_AddArrayToObject(root, "relatives");
    const std::vector<family::FamilyRelationship> relationships = repo.List();
    std::vector<std::string> seen_ids;
    std::vector<std::string> names;
    size_t total = 0;

    auto add_relative = [&](const family::FamilyRelationship& relationship, const std::string& relative_id) {
        if (!AddUniqueId(seen_ids, relative_id)) return;
        FamilyParticipant relative;
        if (!LoadParticipantById(relative_id, relative)) return;

        if (total < kMaxMcpItems) {
            cJSON* item = cJSON_CreateObject();
            AddFamilyParticipantJson(item, "relative", relative);
            cJSON_AddStringToObject(item, "relationship_id", relationship.id.value.c_str());
            cJSON_AddStringToObject(item, "type", relationship.type.c_str());
            AddStringIfNotEmpty(item, "label", relationship.label);
            cJSON_AddItemToArray(relatives, item);
        }
        names.push_back(ParticipantDisplayName(relative));
        ++total;
    };

    for (const auto& relationship : relationships) {
        if (!relationship.enabled) continue;
        const std::string type = Normalize(relationship.type);

        if (kind == "children") {
            if (type == "child" && relationship.to_person_id == base.id) {
                add_relative(relationship, relationship.from_person_id);
            } else if (type == "parent" && relationship.from_person_id == base.id) {
                add_relative(relationship, relationship.to_person_id);
            }
        } else if (kind == "grandchildren") {
            if (type == "grandchild" && relationship.to_person_id == base.id) {
                add_relative(relationship, relationship.from_person_id);
            } else if (type == "grandparent" && relationship.from_person_id == base.id) {
                add_relative(relationship, relationship.to_person_id);
            }
        } else if (kind == "partner") {
            if (type == "spouse" || type == "partner" || type == "pareja") {
                if (relationship.from_person_id == base.id) {
                    add_relative(relationship, relationship.to_person_id);
                } else if (relationship.to_person_id == base.id) {
                    add_relative(relationship, relationship.from_person_id);
                }
            }
        }
    }

    cJSON_AddBoolToObject(root, "found", total > 0);
    cJSON_AddNumberToObject(root, "relative_count", static_cast<double>(total));
    cJSON_AddBoolToObject(root, "truncated", total > kMaxMcpItems);

    const std::string base_name = ParticipantDisplayName(base);
    const std::string joined_names = JoinDisplayNames(names);
    std::string safe_message;
    if (total == 0) {
        if (kind == "children") {
            safe_message = "No tengo hijos anotados para " + base_name + ".";
        } else if (kind == "grandchildren") {
            safe_message = "No tengo nietos anotados para " + base_name + ".";
        } else {
            safe_message = "No tengo pareja anotada para " + base_name + ".";
        }
    } else if (kind == "children") {
        safe_message = "Tengo anotado que los hijos de " + base_name + " son " + joined_names + ".";
    } else if (kind == "grandchildren") {
        safe_message = "Tengo anotado que los nietos de " + base_name + " son " + joined_names + ".";
    } else {
        safe_message = "Tengo anotado que la pareja de " + base_name + " es " + joined_names + ".";
    }
    cJSON_AddStringToObject(root, "safe_message", safe_message.c_str());

    ESP_LOGI("CARE_MCP", "Family relative query: kind=%s person=%s matches=%u",
             kind.c_str(), person_query.c_str(), static_cast<unsigned>(total));
    return Stringify(root);
}


constexpr int kWeatherHttpTimeoutMs = 8000;
constexpr size_t kWeatherMaxResponseBytes = 8192;

esp_err_t WeatherHttpEventHandler(esp_http_client_event_t* event) {
    if (event == nullptr) {
        return ESP_OK;
    }
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data != nullptr && event->data_len > 0) {
        auto* body = static_cast<std::string*>(event->user_data);
        if (body == nullptr) {
            return ESP_FAIL;
        }
        if (body->size() + static_cast<size_t>(event->data_len) > kWeatherMaxResponseBytes) {
            ESP_LOGW("CARE_MCP", "Weather response too large");
            return ESP_FAIL;
        }
        body->append(static_cast<const char*>(event->data), static_cast<size_t>(event->data_len));
    }
    return ESP_OK;
}

bool ParseDoubleText(const std::string& value, double& out) {
    if (value.empty()) return false;
    char* end = nullptr;
    const double parsed = std::strtod(value.c_str(), &end);
    if (end == value.c_str() || (end != nullptr && *end != '\0')) return false;
    out = parsed;
    return true;
}

bool ResolveWeatherLocation(const std::string& city,
                            const std::string& latitude_text,
                            const std::string& longitude_text,
                            std::string& resolved_city,
                            double& latitude,
                            double& longitude) {
    double custom_lat = 0.0;
    double custom_lon = 0.0;
    if (ParseDoubleText(latitude_text, custom_lat) && ParseDoubleText(longitude_text, custom_lon) &&
        custom_lat >= -90.0 && custom_lat <= 90.0 && custom_lon >= -180.0 && custom_lon <= 180.0) {
        latitude = custom_lat;
        longitude = custom_lon;
        resolved_city = city.empty() ? "ubicacion configurada" : city;
        return true;
    }

    const std::string normalized_city = Normalize(city);
    if (normalized_city.empty() || normalized_city == "buenos aires" || normalized_city == "caba" ||
        normalized_city == "capital federal" || normalized_city == "ciudad de buenos aires" ||
        normalized_city == "bs as") {
        resolved_city = "Buenos Aires";
        latitude = -34.6037;
        longitude = -58.3816;
        return true;
    }

    return false;
}

std::string UrlForOpenMeteo(double latitude, double longitude) {
    char url[420] = {};
    std::snprintf(url, sizeof(url),
                  "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
                  "&current=temperature_2m,relative_humidity_2m,apparent_temperature,precipitation,weather_code,wind_speed_10m"
                  "&hourly=precipitation_probability&forecast_days=1&timezone=America%%2FArgentina%%2FBuenos_Aires",
                  latitude, longitude);
    return std::string(url);
}

bool HttpGetText(const std::string& url, std::string& body, int& status_code) {
    body.clear();
    status_code = 0;

    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.timeout_ms = kWeatherHttpTimeoutMs;
    config.event_handler = WeatherHttpEventHandler;
    config.user_data = &body;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        return false;
    }

    const esp_err_t err = esp_http_client_perform(client);
    status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGW("CARE_MCP", "Open-Meteo request failed: %s", esp_err_to_name(err));
        return false;
    }
    return status_code >= 200 && status_code < 300 && !body.empty();
}

bool JsonNumber(const cJSON* object, const char* key, double& out) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(item)) return false;
    out = item->valuedouble;
    return true;
}

const char* WeatherCodeSpanish(int code) {
    switch (code) {
        case 0: return "despejado";
        case 1: return "principalmente despejado";
        case 2: return "parcialmente nublado";
        case 3: return "nublado";
        case 45:
        case 48: return "con niebla";
        case 51:
        case 53:
        case 55: return "con llovizna";
        case 56:
        case 57: return "con llovizna helada";
        case 61:
        case 63:
        case 65: return "con lluvia";
        case 66:
        case 67: return "con lluvia helada";
        case 71:
        case 73:
        case 75: return "con nieve";
        case 77: return "con granos de nieve";
        case 80:
        case 81:
        case 82: return "con chaparrones";
        case 85:
        case 86: return "con chaparrones de nieve";
        case 95: return "con tormenta";
        case 96:
        case 99: return "con tormenta y granizo";
        default: return "con condiciones no especificadas";
    }
}

int RoundedInt(double value) {
    return value >= 0.0 ? static_cast<int>(value + 0.5) : static_cast<int>(value - 0.5);
}

std::string BuildWeatherSafeMessage(const std::string& city,
                                    double temperature,
                                    double apparent,
                                    double humidity,
                                    double precipitation,
                                    int weather_code,
                                    int rain_probability) {
    char buffer[260] = {};
    const char* condition = WeatherCodeSpanish(weather_code);
    if (rain_probability >= 0) {
        std::snprintf(buffer, sizeof(buffer),
                      "En %s hay %d grados. Esta %s. La sensacion termica es de %d grados, la humedad es %d por ciento y la probabilidad de lluvia es %d por ciento.",
                      city.c_str(), RoundedInt(temperature), condition, RoundedInt(apparent),
                      RoundedInt(humidity), rain_probability);
    } else if (precipitation > 0.0) {
        std::snprintf(buffer, sizeof(buffer),
                      "En %s hay %d grados. Esta %s. La sensacion termica es de %d grados y esta precipitando.",
                      city.c_str(), RoundedInt(temperature), condition, RoundedInt(apparent));
    } else {
        std::snprintf(buffer, sizeof(buffer),
                      "En %s hay %d grados. Esta %s. La sensacion termica es de %d grados y la humedad es %d por ciento.",
                      city.c_str(), RoundedInt(temperature), condition, RoundedInt(apparent),
                      RoundedInt(humidity));
    }
    return std::string(buffer);
}

}  // namespace

std::string CareMcpService::GetChildrenOf(const std::string& person) {
    return BuildRelativeQuery(person.empty() ? std::string("profile") : person, "children");
}

std::string CareMcpService::GetGrandchildrenOf(const std::string& person) {
    return BuildRelativeQuery(person.empty() ? std::string("profile") : person, "grandchildren");
}

std::string CareMcpService::GetPartnerOf(const std::string& person) {
    return BuildRelativeQuery(person, "partner");
}


std::string CareMcpService::GetWeather(const std::string& city,
                                       const std::string& latitude_text,
                                       const std::string& longitude_text) {
    std::string resolved_city;
    double latitude = 0.0;
    double longitude = 0.0;
    if (!ResolveWeatherLocation(city, latitude_text, longitude_text, resolved_city, latitude, longitude)) {
        cJSON* root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "ok", false);
        cJSON_AddStringToObject(root, "error", "LOCATION_NOT_SUPPORTED");
        cJSON_AddStringToObject(root, "source", "open_meteo");
        cJSON_AddStringToObject(root, "safe_message", "Por ahora tengo configurado el clima local de Buenos Aires. Para otra ciudad hace falta cargar latitud y longitud.");
        cJSON_AddStringToObject(root, "instruction", "Responde en espanol y no uses el clima chino ni otras fuentes si esta herramienta devuelve error.");
        return Stringify(root);
    }

    const std::string url = UrlForOpenMeteo(latitude, longitude);
    std::string body;
    int status_code = 0;
    if (!HttpGetText(url, body, status_code)) {
        cJSON* root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "ok", false);
        cJSON_AddStringToObject(root, "error", "WEATHER_HTTP_ERROR");
        cJSON_AddNumberToObject(root, "http_status", status_code);
        cJSON_AddStringToObject(root, "source", "open_meteo");
        cJSON_AddStringToObject(root, "safe_message", "Perdon, no pude consultar el clima local en este momento.");
        cJSON_AddStringToObject(root, "instruction", "Responde en espanol con safe_message. No digas que el clima solo sirve para China.");
        ESP_LOGW("CARE_MCP", "Weather query failed city=%s status=%d", resolved_city.c_str(), status_code);
        return Stringify(root);
    }

    cJSON* parsed = cJSON_Parse(body.c_str());
    if (parsed == nullptr) {
        return ErrorJson("WEATHER_JSON_ERROR");
    }

    const cJSON* current = cJSON_GetObjectItemCaseSensitive(parsed, "current");
    if (!cJSON_IsObject(current)) {
        cJSON_Delete(parsed);
        return ErrorJson("WEATHER_JSON_ERROR");
    }

    double temperature = 0.0;
    double humidity = 0.0;
    double apparent = 0.0;
    double precipitation = 0.0;
    double wind = 0.0;
    double code_value = -1.0;
    if (!JsonNumber(current, "temperature_2m", temperature) ||
        !JsonNumber(current, "relative_humidity_2m", humidity) ||
        !JsonNumber(current, "apparent_temperature", apparent) ||
        !JsonNumber(current, "weather_code", code_value)) {
        cJSON_Delete(parsed);
        return ErrorJson("WEATHER_JSON_ERROR");
    }
    JsonNumber(current, "precipitation", precipitation);
    JsonNumber(current, "wind_speed_10m", wind);

    int rain_probability = -1;
    const cJSON* hourly = cJSON_GetObjectItemCaseSensitive(parsed, "hourly");
    if (cJSON_IsObject(hourly)) {
        const cJSON* probs = cJSON_GetObjectItemCaseSensitive(hourly, "precipitation_probability");
        if (cJSON_IsArray(probs) && cJSON_GetArraySize(probs) > 0) {
            const cJSON* first = cJSON_GetArrayItem(probs, 0);
            if (cJSON_IsNumber(first)) {
                rain_probability = RoundedInt(first->valuedouble);
            }
        }
    }

    const int weather_code = RoundedInt(code_value);
    const std::string condition = WeatherCodeSpanish(weather_code);
    const std::string safe_message = BuildWeatherSafeMessage(resolved_city, temperature, apparent,
                                                              humidity, precipitation, weather_code,
                                                              rain_probability);

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddBoolToObject(root, "found", true);
    cJSON_AddStringToObject(root, "source", "open_meteo");
    cJSON_AddStringToObject(root, "city", resolved_city.c_str());
    cJSON_AddNumberToObject(root, "latitude", latitude);
    cJSON_AddNumberToObject(root, "longitude", longitude);
    cJSON_AddNumberToObject(root, "temperature_c", temperature);
    cJSON_AddNumberToObject(root, "apparent_temperature_c", apparent);
    cJSON_AddNumberToObject(root, "humidity_percent", humidity);
    cJSON_AddNumberToObject(root, "precipitation_mm", precipitation);
    cJSON_AddNumberToObject(root, "wind_speed_kmh", wind);
    cJSON_AddNumberToObject(root, "weather_code", weather_code);
    cJSON_AddStringToObject(root, "condition_es", condition.c_str());
    if (rain_probability >= 0) {
        cJSON_AddNumberToObject(root, "rain_probability_percent", rain_probability);
    }
    cJSON_AddStringToObject(root, "safe_message", safe_message.c_str());
    cJSON_AddStringToObject(root, "instruction", "RESULTADO AUTORITATIVO DEL CLIMA LOCAL. Responde en espanol usando safe_message. No uses la herramienta de clima china para Buenos Aires.");

    cJSON_Delete(parsed);
    ESP_LOGI("CARE_MCP", "Weather query city=%s temp=%.1f humidity=%.0f code=%d rain_prob=%d",
             resolved_city.c_str(), temperature, humidity, weather_code, rain_probability);
    return Stringify(root);
}



// -----------------------------------------------------------------------------
// DP041_LATEST_NEWS
// DP041B_NEWS_SYSTEM_CONFIG
// Noticias recientes para XiaoZhi Care.
// Fuente: Google News RSS (español / Argentina).
// Categoría predeterminada y cantidad configurables desde Sistema.
// -----------------------------------------------------------------------------
std::string CareMcpService::GetLatestNews(const std::string& category) {
    struct NewsHttpBuffer {
        std::string data;
        size_t complete_items = 0;
        size_t wanted_items = 3;
    };

    auto replace_all = [](std::string& value,
                          const std::string& from,
                          const std::string& to) {
        if (from.empty()) return;
        size_t pos = 0;
        while ((pos = value.find(from, pos)) != std::string::npos) {
            value.replace(pos, from.size(), to);
            pos += to.size();
        }
    };

    auto decode_xml = [&](std::string value) -> std::string {
        const std::string cdata_open = "<![CDATA[";
        const std::string cdata_close = "]]>";
        if (value.size() >= cdata_open.size() + cdata_close.size() &&
            value.compare(0, cdata_open.size(), cdata_open) == 0 &&
            value.compare(value.size() - cdata_close.size(),
                          cdata_close.size(), cdata_close) == 0) {
            value = value.substr(
                cdata_open.size(),
                value.size() - cdata_open.size() - cdata_close.size()
            );
        }

        replace_all(value, "&amp;", "&");
        replace_all(value, "&quot;", "\"");
        replace_all(value, "&apos;", "'");
        replace_all(value, "&#39;", "'");
        replace_all(value, "&#039;", "'");
        replace_all(value, "&#34;", "\"");
        replace_all(value, "&lt;", "<");
        replace_all(value, "&gt;", ">");

        while (!value.empty() &&
               (value.front() == ' ' || value.front() == '\n' ||
                value.front() == '\r' || value.front() == '\t')) {
            value.erase(value.begin());
        }
        while (!value.empty() &&
               (value.back() == ' ' || value.back() == '\n' ||
                value.back() == '\r' || value.back() == '\t')) {
            value.pop_back();
        }
        return value;
    };

    auto extract_tag = [&](const std::string& block,
                           const std::string& tag) -> std::string {
        const std::string open = "<" + tag;
        const std::string close = "</" + tag + ">";

        const size_t start = block.find(open);
        if (start == std::string::npos) return "";

        const size_t content_start = block.find('>', start);
        if (content_start == std::string::npos) return "";

        const size_t end = block.find(close, content_start + 1);
        if (end == std::string::npos) return "";

        return decode_xml(
            block.substr(content_start + 1, end - content_start - 1)
        );
    };

    auto lower_ascii = [](std::string value) -> std::string {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        return value;
    };

    auto& news_settings = news::NewsSettings::GetInstance();
    news_settings.Init();

    std::string requested = lower_ascii(category);
    std::string normalized;

    if (requested.empty() || requested == "default" ||
        requested == "general" || requested == "actualidad") {
        normalized = news_settings.GetDefaultCategoryName();
    } else {
        news::NewsCategory parsed_category;
        if (news::NewsSettings::ParseCategory(requested, parsed_category)) {
            normalized = news::NewsSettings::CategoryText(parsed_category);
        } else {
            normalized = news_settings.GetDefaultCategoryName();
        }
    }

    const size_t headline_limit =
        static_cast<size_t>(news_settings.GetHeadlineCount());

    const char* url = nullptr;
    if (normalized == "mundo") {
        url =
            "https://news.google.com/rss/headlines/section/topic/WORLD"
            "?hl=es-419&gl=AR&ceid=AR:es-419";
    } else if (normalized == "tecnologia") {
        url =
            "https://news.google.com/rss/headlines/section/topic/TECHNOLOGY"
            "?hl=es-419&gl=AR&ceid=AR:es-419";
    } else if (normalized == "deportes") {
        url =
            "https://news.google.com/rss/headlines/section/topic/SPORTS"
            "?hl=es-419&gl=AR&ceid=AR:es-419";
    } else {
        url =
            "https://news.google.com/rss"
            "?hl=es-419&gl=AR&ceid=AR:es-419";
    }

    static std::mutex cache_mutex;
    static std::string cache_category;
    static std::string cache_json;
    static size_t cache_count = 0;
    static uint32_t cache_ms = 0;

    const uint32_t now_ms = esp_log_timestamp();
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        if (!cache_json.empty() &&
            cache_category == normalized &&
            cache_count == headline_limit &&
            static_cast<uint32_t>(now_ms - cache_ms) < 600000U) {
            ESP_LOGI("CARE_MCP",
                     "News cache hit category=%s count=%u",
                     normalized.c_str(),
                     static_cast<unsigned>(headline_limit));
            return cache_json;
        }
    }

    NewsHttpBuffer response;
    response.wanted_items = headline_limit;
    response.data.reserve(16384);

    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = 10000;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.user_data = &response;
    config.event_handler = [](esp_http_client_event_t* evt) -> esp_err_t {
        if (evt == nullptr ||
            evt->event_id != HTTP_EVENT_ON_DATA ||
            evt->user_data == nullptr ||
            evt->data == nullptr ||
            evt->data_len <= 0) {
            return ESP_OK;
        }

        auto* out = static_cast<NewsHttpBuffer*>(evt->user_data);
        if (out->complete_items >= out->wanted_items ||
            out->data.size() >= 32768U) {
            return ESP_OK;
        }

        const size_t room = 32768U - out->data.size();
        const size_t wanted = static_cast<size_t>(evt->data_len);
        const size_t take = std::min(room, wanted);

        out->data.append(static_cast<const char*>(evt->data), take);

        size_t count = 0;
        size_t scan = 0;
        while ((scan = out->data.find("</item>", scan)) != std::string::npos) {
            ++count;
            scan += 7;
            if (count >= out->wanted_items) break;
        }
        out->complete_items = count;
        return ESP_OK;
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        cJSON* root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "ok", false);
        cJSON_AddStringToObject(root, "source", "google_news_rss");
        cJSON_AddStringToObject(
            root, "safe_message",
            "Perdón, no pude consultar las noticias en este momento."
        );
        cJSON_AddStringToObject(
            root, "instruction",
            "Responde en español usando safe_message. No inventes titulares."
        );
        return Stringify(root);
    }

    esp_http_client_set_header(client, "User-Agent", "XiaoZhi-Care/0.3");
    esp_http_client_set_header(
        client, "Accept", "application/rss+xml, application/xml, text/xml"
    );

    const esp_err_t err = esp_http_client_perform(client);
    const int status =
        (err == ESP_OK) ? esp_http_client_get_status_code(client) : -1;

    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        std::lock_guard<std::mutex> lock(cache_mutex);
        if (!cache_json.empty() &&
            cache_category == normalized &&
            cache_count == headline_limit) {
            ESP_LOGW("CARE_MCP",
                     "News query failed category=%s status=%d; using cache",
                     normalized.c_str(), status);
            return cache_json;
        }

        cJSON* root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "ok", false);
        cJSON_AddNumberToObject(root, "http_status", status);
        cJSON_AddStringToObject(root, "source", "google_news_rss");
        cJSON_AddStringToObject(
            root, "safe_message",
            "Perdón, no pude consultar las noticias en este momento."
        );
        cJSON_AddStringToObject(
            root, "instruction",
            "Responde en español usando safe_message. No inventes titulares."
        );
        ESP_LOGW("CARE_MCP",
                 "News query failed category=%s status=%d err=%s",
                 normalized.c_str(),
                 status,
                 esp_err_to_name(err));
        return Stringify(root);
    }

    struct Headline {
        std::string title;
        std::string source;
        std::string published;
    };

    std::vector<Headline> headlines;
    headlines.reserve(headline_limit);

    size_t cursor = 0;
    while (headlines.size() < headline_limit) {
        const size_t item_start = response.data.find("<item", cursor);
        if (item_start == std::string::npos) break;

        const size_t body_start = response.data.find('>', item_start);
        if (body_start == std::string::npos) break;

        const size_t item_end =
            response.data.find("</item>", body_start + 1);
        if (item_end == std::string::npos) break;

        const std::string block =
            response.data.substr(
                body_start + 1,
                item_end - body_start - 1
            );

        Headline h;
        h.title = extract_tag(block, "title");
        h.source = extract_tag(block, "source");
        h.published = extract_tag(block, "pubDate");

        if (!h.title.empty()) {
            if (!h.source.empty()) {
                const std::string suffix = " - " + h.source;
                if (h.title.size() > suffix.size() &&
                    h.title.compare(h.title.size() - suffix.size(),
                                    suffix.size(),
                                    suffix) == 0) {
                    h.title.erase(h.title.size() - suffix.size());
                }
            } else {
                const size_t sep = h.title.rfind(" - ");
                if (sep != std::string::npos &&
                    sep > 10 &&
                    h.title.size() - sep < 80) {
                    h.source = h.title.substr(sep + 3);
                    h.title.erase(sep);
                }
            }
            headlines.push_back(h);
        }

        cursor = item_end + 7;
    }

    if (headlines.empty()) {
        cJSON* root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "ok", false);
        cJSON_AddStringToObject(root, "source", "google_news_rss");
        cJSON_AddStringToObject(
            root, "safe_message",
            "Encontré el servicio de noticias, pero no pude leer los titulares."
        );
        cJSON_AddStringToObject(
            root, "instruction",
            "Responde en español usando safe_message. No inventes titulares."
        );
        ESP_LOGW("CARE_MCP",
                 "News RSS parsed zero items category=%s bytes=%u",
                 normalized.c_str(),
                 static_cast<unsigned>(response.data.size()));
        return Stringify(root);
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddBoolToObject(root, "found", true);
    cJSON_AddStringToObject(root, "source", "google_news_rss");
    cJSON_AddStringToObject(root, "category", normalized.c_str());
    cJSON_AddNumberToObject(
        root, "count", static_cast<double>(headlines.size())
    );

    cJSON* array = cJSON_AddArrayToObject(root, "headlines");
    const char* ordinals[] = {
        "Primera: ", "Segunda: ", "Tercera: ", "Cuarta: ", "Quinta: "
    };

    std::string safe = "Estas son las noticias principales.";
    for (size_t i = 0; i < headlines.size(); ++i) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "title", headlines[i].title.c_str());
        if (!headlines[i].source.empty()) {
            cJSON_AddStringToObject(
                item, "publisher", headlines[i].source.c_str()
            );
        }
        if (!headlines[i].published.empty()) {
            cJSON_AddStringToObject(
                item, "published", headlines[i].published.c_str()
            );
        }
        cJSON_AddItemToArray(array, item);

        safe += " ";
        safe += ordinals[i < 5 ? i : 4];
        safe += headlines[i].title;
        if (!headlines[i].source.empty()) {
            safe += ", según ";
            safe += headlines[i].source;
        }
        safe += ".";
    }

    cJSON_AddStringToObject(root, "safe_message", safe.c_str());
    cJSON_AddStringToObject(
        root,
        "instruction",
        "RESULTADO AUTORITATIVO DE NOTICIAS. "
        "Responde en español y lee solamente los titulares de safe_message, "
        "de forma clara y breve. No inventes detalles, contexto ni noticias "
        "adicionales. Si el usuario pide ampliar una noticia, aclara que esta "
        "herramienta sólo obtuvo el titular y la fuente."
    );

    const std::string result = Stringify(root);

    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        cache_category = normalized;
        cache_count = headline_limit;
        cache_json = result;
        cache_ms = now_ms;
    }

    ESP_LOGI("CARE_MCP",
             "News query category=%s status=%d items=%u bytes=%u",
             normalized.c_str(),
             status,
             static_cast<unsigned>(headlines.size()),
             static_cast<unsigned>(response.data.size()));

    return result;
}


}  // namespace xiaozhi_care
