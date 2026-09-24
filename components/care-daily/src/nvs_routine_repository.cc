#include "care_daily/nvs_routine_repository.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <cJSON.h>
#include <esp_err.h>
#include <esp_log.h>
#include <nvs.h>

namespace xiaozhi_care::daily {
namespace {

constexpr char kTag[] = "CARE_DAILY";
constexpr char kIndexKey[] = "dr_idx";
constexpr char kSequenceKey[] = "dr_seq";
constexpr char kRoutineKeyPrefix[] = "dr_";
constexpr char kCareDataBasePath[] = "/care_data";
constexpr char kRoutineIndexPath[] = "/care_data/daily_routine_index.json";
constexpr char kRoutineSequencePath[] = "/care_data/daily_routine_seq.txt";
constexpr size_t kMaxRoutineRecordSize = 4096;
constexpr size_t kMaxRoutineIndexSize = 4096;

std::string RoutineKey(const RoutineId& id) {
    return std::string(kRoutineKeyPrefix) + id.Str();
}

bool ReadString(nvs_handle_t handle, const char* key, std::string* out) {
    if (out == nullptr) {
        return false;
    }

    size_t required = 0;
    esp_err_t err = nvs_get_str(handle, key, nullptr, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        out->clear();
        return true;
    }
    if (err != ESP_OK || required == 0) {
        return false;
    }

    std::vector<char> buffer(required, '\0');
    err = nvs_get_str(handle, key, buffer.data(), &required);
    if (err != ESP_OK) {
        return false;
    }

    *out = buffer.data();
    return true;
}

bool WriteString(nvs_handle_t handle, const char* key, const std::string& value) {
    return nvs_set_str(handle, key, value.c_str()) == ESP_OK;
}

std::string JsonPrint(cJSON* root) {
    if (root == nullptr) {
        return {};
    }

    char* raw = cJSON_PrintUnformatted(root);
    if (raw == nullptr) {
        return {};
    }

    std::string result(raw);
    cJSON_free(raw);
    return result;
}

void AddString(cJSON* root, const char* name, const std::string& value) {
    cJSON_AddStringToObject(root, name, value.c_str());
}

std::string GetString(cJSON* root, const char* name) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!cJSON_IsString(item) || item->valuestring == nullptr) {
        return {};
    }
    return item->valuestring;
}

int GetInt(cJSON* root, const char* name, int fallback) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!cJSON_IsNumber(item)) {
        return fallback;
    }
    return item->valueint;
}

bool GetBool(cJSON* root, const char* name, bool fallback) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!cJSON_IsBool(item)) {
        return fallback;
    }
    return cJSON_IsTrue(item);
}

std::string GetStringAny(cJSON* root, const char* short_name, const char* long_name) {
    std::string value = GetString(root, short_name);
    return value.empty() ? GetString(root, long_name) : value;
}

int GetIntAny(cJSON* root, const char* short_name, const char* long_name, int fallback) {
    cJSON* short_item = cJSON_GetObjectItemCaseSensitive(root, short_name);
    if (cJSON_IsNumber(short_item)) {
        return short_item->valueint;
    }
    return GetInt(root, long_name, fallback);
}

bool GetBoolAny(cJSON* root, const char* short_name, const char* long_name, bool fallback) {
    cJSON* short_item = cJSON_GetObjectItemCaseSensitive(root, short_name);
    if (cJSON_IsBool(short_item)) {
        return cJSON_IsTrue(short_item);
    }
    return GetBool(root, long_name, fallback);
}

cJSON* GetObjectAny(cJSON* root, const char* short_name, const char* long_name) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, short_name);
    return cJSON_IsObject(item) ? item : cJSON_GetObjectItemCaseSensitive(root, long_name);
}

uint8_t WeekdayMask(const std::array<bool, 7>& weekdays) {
    uint8_t mask = 0;
    for (size_t i = 0; i < weekdays.size(); ++i) {
        if (weekdays[i]) {
            mask |= static_cast<uint8_t>(1u << i);
        }
    }
    return mask;
}

void ApplyWeekdayMask(uint8_t mask, std::array<bool, 7>& weekdays) {
    for (size_t i = 0; i < weekdays.size(); ++i) {
        weekdays[i] = (mask & static_cast<uint8_t>(1u << i)) != 0;
    }
}

std::string SerializeIndex(const std::vector<RoutineId>& ids) {
    cJSON* root = cJSON_CreateArray();
    if (root == nullptr) {
        return {};
    }

    for (const RoutineId& id : ids) {
        cJSON_AddItemToArray(root, cJSON_CreateString(id.Str().c_str()));
    }

    std::string result = JsonPrint(root);
    cJSON_Delete(root);
    return result;
}

std::vector<RoutineId> ParseIndex(const std::string& json) {
    std::vector<RoutineId> ids;
    if (json.empty()) {
        return ids;
    }

    cJSON* root = cJSON_Parse(json.c_str());
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return ids;
    }

    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, root) {
        if (cJSON_IsString(item) && item->valuestring != nullptr && item->valuestring[0] != '\0') {
            ids.emplace_back(item->valuestring);
        }
    }

    cJSON_Delete(root);
    return ids;
}

std::vector<RoutineId> ReadIndex(nvs_handle_t handle) {
    std::string json;
    if (!ReadString(handle, kIndexKey, &json)) {
        return {};
    }
    return ParseIndex(json);
}

bool WriteIndex(nvs_handle_t handle, const std::vector<RoutineId>& ids) {
    const std::string json = SerializeIndex(ids);
    return !json.empty() && WriteString(handle, kIndexKey, json);
}

bool ContainsId(const std::vector<RoutineId>& ids, const RoutineId& id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

std::string SerializeRoutine(const CareRoutine& routine) {
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) {
        return {};
    }

    // DP-013-r2: compact JSON for NVS.  The reader below still accepts the
    // older verbose keys, so existing devices migrate naturally on next save.
    AddString(root, "i", routine.id.Str());
    cJSON_AddNumberToObject(root, "t", static_cast<int>(routine.type));
    cJSON_AddNumberToObject(root, "st", static_cast<int>(routine.state));
    AddString(root, "n", routine.title);
    AddString(root, "d", routine.description);
    AddString(root, "x", routine.data);

    cJSON* schedule = cJSON_CreateObject();
    if (schedule != nullptr) {
        cJSON_AddNumberToObject(schedule, "h", routine.schedule.time.hour);
        cJSON_AddNumberToObject(schedule, "m", routine.schedule.time.minute);
        cJSON_AddNumberToObject(schedule, "r", static_cast<int>(routine.schedule.repeat));
        cJSON_AddNumberToObject(schedule, "mo", static_cast<int>(routine.schedule.moment));
        cJSON_AddNumberToObject(schedule, "iv", routine.schedule.interval_days);
        cJSON_AddNumberToObject(schedule, "rw", routine.schedule.reminder_window_minutes);
        cJSON_AddNumberToObject(schedule, "wd", WeekdayMask(routine.schedule.weekdays));
        cJSON_AddItemToObject(root, "s", schedule);
    }

    cJSON* placement = cJSON_CreateObject();
    if (placement != nullptr) {
        cJSON_AddNumberToObject(placement, "t", static_cast<int>(routine.placement.type));
        AddString(placement, "d", routine.placement.description);
        AddString(placement, "c", routine.placement.compartment);
        cJSON_AddBoolToObject(placement, "mon", routine.placement.monitored);
        cJSON_AddNumberToObject(placement, "b", routine.placement.ble_slot);
        cJSON_AddNumberToObject(placement, "l", routine.placement.led_number);
        cJSON_AddItemToObject(root, "p", placement);
    }

    cJSON* alert = cJSON_CreateObject();
    if (alert != nullptr) {
        cJSON_AddBoolToObject(alert, "en", routine.alert.enabled);
        cJSON_AddNumberToObject(alert, "mo", static_cast<int>(routine.alert.mode));
        cJSON_AddNumberToObject(alert, "vp", static_cast<int>(routine.alert.visual_pattern));
        cJSON_AddNumberToObject(alert, "vc", static_cast<int>(routine.alert.visual_color));
        cJSON_AddNumberToObject(alert, "sp", static_cast<int>(routine.alert.sound_pattern));
        cJSON_AddNumberToObject(alert, "rm", routine.alert.repeat_minutes);
        cJSON_AddNumberToObject(alert, "mr", routine.alert.max_repeats);
        cJSON_AddBoolToObject(alert, "v", routine.alert.voice_enabled);
        cJSON_AddItemToObject(root, "a", alert);
    }

    std::string result = JsonPrint(root);
    cJSON_Delete(root);
    return result;
}

std::optional<CareRoutine> DeserializeRoutine(const std::string& json) {
    if (json.empty()) {
        return std::nullopt;
    }

    cJSON* root = cJSON_Parse(json.c_str());
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return std::nullopt;
    }

    CareRoutine routine;
    routine.id = RoutineId(GetStringAny(root, "i", "id"));
    routine.type = static_cast<RoutineType>(GetIntAny(root, "t", "type", static_cast<int>(RoutineType::Custom)));
    routine.state = static_cast<RoutineState>(GetIntAny(root, "st", "state", static_cast<int>(RoutineState::Active)));
    routine.title = GetStringAny(root, "n", "title");
    routine.description = GetStringAny(root, "d", "description");
    routine.data = GetStringAny(root, "x", "data");

    cJSON* schedule = GetObjectAny(root, "s", "schedule");
    if (cJSON_IsObject(schedule)) {
        routine.schedule.time.hour = static_cast<uint8_t>(GetIntAny(schedule, "h", "hour", 0));
        routine.schedule.time.minute = static_cast<uint8_t>(GetIntAny(schedule, "m", "minute", 0));
        routine.schedule.repeat = static_cast<RoutineRepeatType>(GetIntAny(schedule, "r", "repeat", static_cast<int>(RoutineRepeatType::Daily)));
        routine.schedule.moment = static_cast<RoutineMoment>(GetIntAny(schedule, "mo", "moment", static_cast<int>(RoutineMoment::Anytime)));
        routine.schedule.interval_days = static_cast<uint16_t>(GetIntAny(schedule, "iv", "interval_days", 1));
        routine.schedule.reminder_window_minutes = static_cast<uint16_t>(GetIntAny(schedule, "rw", "reminder_window_minutes", 30));

        cJSON* compact_weekdays = cJSON_GetObjectItemCaseSensitive(schedule, "wd");
        if (cJSON_IsNumber(compact_weekdays)) {
            ApplyWeekdayMask(static_cast<uint8_t>(compact_weekdays->valueint), routine.schedule.weekdays);
        } else {
            cJSON* weekdays = cJSON_GetObjectItemCaseSensitive(schedule, "weekdays");
            if (cJSON_IsArray(weekdays)) {
                for (int i = 0; i < 7; ++i) {
                    cJSON* item = cJSON_GetArrayItem(weekdays, i);
                    if (cJSON_IsBool(item)) {
                        routine.schedule.weekdays[static_cast<size_t>(i)] = cJSON_IsTrue(item);
                    }
                }
            }
        }
    }

    cJSON* placement = GetObjectAny(root, "p", "placement");
    if (cJSON_IsObject(placement)) {
        routine.placement.type = static_cast<PlacementType>(GetIntAny(placement, "t", "type", static_cast<int>(PlacementType::None)));
        routine.placement.description = GetStringAny(placement, "d", "description");
        routine.placement.compartment = GetStringAny(placement, "c", "compartment");
        routine.placement.monitored = GetBoolAny(placement, "mon", "monitored", false);
        routine.placement.ble_slot = static_cast<uint8_t>(GetIntAny(placement, "b", "ble_slot", 0));
        routine.placement.led_number = static_cast<uint8_t>(GetIntAny(placement, "l", "led_number", 0));
    }

    cJSON* alert = GetObjectAny(root, "a", "alert");
    if (cJSON_IsObject(alert)) {
        routine.alert.enabled = GetBoolAny(alert, "en", "enabled", true);
        routine.alert.mode = static_cast<CareAlertMode>(GetIntAny(alert, "mo", "mode", static_cast<int>(CareAlertMode::VisualOnly)));
        routine.alert.visual_pattern = static_cast<CareVisualPattern>(GetIntAny(alert, "vp", "visual_pattern", static_cast<int>(CareVisualPattern::Breathing)));
        routine.alert.visual_color = static_cast<CareAlertColor>(GetIntAny(alert, "vc", "visual_color", static_cast<int>(CareAlertColor::Auto)));
        routine.alert.sound_pattern = static_cast<CareSoundPattern>(GetIntAny(alert, "sp", "sound_pattern", static_cast<int>(CareSoundPattern::SoftBeep)));
        routine.alert.repeat_minutes = static_cast<uint16_t>(GetIntAny(alert, "rm", "repeat_minutes", 10));
        routine.alert.max_repeats = static_cast<uint8_t>(GetIntAny(alert, "mr", "max_repeats", 3));
        routine.alert.voice_enabled = GetBoolAny(alert, "v", "voice_enabled", false);
    }

    cJSON_Delete(root);

    if (!routine.IsValid()) {
        return std::nullopt;
    }

    return routine;
}


}  // namespace


// DP-030-r1d helpers: rutinas en archivos planos dentro de /care_data
std::string RoutineFilePath(const RoutineId& id) {
    char path[96] = {};
    std::snprintf(path, sizeof(path), "%s/daily_routine_%s.json", kCareDataBasePath, id.Str().c_str());
    return path;
}

bool SaveTextFile(const char* path, const std::string& text) {
    FILE* f = std::fopen(path, "wb");
    if (f == nullptr) return false;
    const size_t written = std::fwrite(text.data(), 1, text.size(), f);
    const int close_result = std::fclose(f);
    return written == text.size() && close_result == 0;
}

bool LoadTextFile(const char* path, std::string* text, size_t max_size) {
    if (text == nullptr) return false;
    text->clear();

    FILE* f = std::fopen(path, "rb");
    if (f == nullptr) return false;

    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        return false;
    }

    long len = std::ftell(f);
    if (len < 0 || static_cast<size_t>(len) > max_size) {
        std::fclose(f);
        return false;
    }

    std::rewind(f);
    text->resize(static_cast<size_t>(len));
    const size_t got = text->empty() ? 0 : std::fread(text->data(), 1, text->size(), f);
    const int close_result = std::fclose(f);

    if ((!text->empty() && got != text->size()) || close_result != 0) {
        text->clear();
        return false;
    }

    return true;
}

bool DeleteTextFile(const char* path) {
    if (std::remove(path) == 0) return true;
    return errno == ENOENT;
}

bool DeleteRoutineNvsKey(const std::string& nvs_namespace, const char* key) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(nvs_namespace.c_str(), NVS_READWRITE, &handle);
    if (err != ESP_OK) return false;

    err = nvs_erase_key(handle, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(handle);

    nvs_close(handle);
    return err == ESP_OK;
}

std::vector<RoutineId> ReadRoutineIndexFile() {
    std::string json;
    if (!LoadTextFile(kRoutineIndexPath, &json, kMaxRoutineIndexSize)) return {};
    return ParseIndex(json);
}

bool WriteRoutineIndexFile(const std::vector<RoutineId>& ids) {
    const std::string json = SerializeIndex(ids);
    return !json.empty() && SaveTextFile(kRoutineIndexPath, json);
}

std::vector<RoutineId> ReadRoutineIndexMigrating(const std::string& nvs_namespace) {
    std::string json;
    if (LoadTextFile(kRoutineIndexPath, &json, kMaxRoutineIndexSize)) {
        return ParseIndex(json);
    }

    nvs_handle_t handle = 0;
    if (nvs_open(nvs_namespace.c_str(), NVS_READONLY, &handle) != ESP_OK) {
        return {};
    }

    const bool ok = ReadString(handle, kIndexKey, &json) && !json.empty();
    nvs_close(handle);

    if (!ok) return {};

    std::vector<RoutineId> ids = ParseIndex(json);
    if (WriteRoutineIndexFile(ids)) {
        DeleteRoutineNvsKey(nvs_namespace, kIndexKey);
        ESP_LOGI(kTag, "Migrated routine index from NVS to care_data");
    }

    return ids;
}

bool ReadRoutineSequenceMigrating(const std::string& nvs_namespace, uint32_t* sequence) {
    if (sequence == nullptr) return false;
    *sequence = 1;

    std::string raw;
    if (LoadTextFile(kRoutineSequencePath, &raw, 32)) {
        char* end = nullptr;
        unsigned long value = std::strtoul(raw.c_str(), &end, 10);
        if (end != raw.c_str() && value > 0) {
            *sequence = static_cast<uint32_t>(value);
            return true;
        }
    }

    nvs_handle_t handle = 0;
    if (nvs_open(nvs_namespace.c_str(), NVS_READONLY, &handle) == ESP_OK) {
        uint32_t nvs_sequence = 1;
        esp_err_t err = nvs_get_u32(handle, kSequenceKey, &nvs_sequence);
        nvs_close(handle);

        if (err == ESP_OK && nvs_sequence > 0) {
            *sequence = nvs_sequence;
        }
    }

    char buffer[24] = {};
    std::snprintf(buffer, sizeof(buffer), "%lu", static_cast<unsigned long>(*sequence));
    if (SaveTextFile(kRoutineSequencePath, buffer)) {
        DeleteRoutineNvsKey(nvs_namespace, kSequenceKey);
        ESP_LOGI(kTag, "Migrated routine sequence from NVS to care_data");
    }

    return true;
}

bool WriteRoutineSequenceFile(const std::string& nvs_namespace, uint32_t sequence) {
    char buffer[24] = {};
    std::snprintf(buffer, sizeof(buffer), "%lu", static_cast<unsigned long>(sequence));
    const bool ok = SaveTextFile(kRoutineSequencePath, buffer);
    if (ok) DeleteRoutineNvsKey(nvs_namespace, kSequenceKey);
    return ok;
}

bool SaveRoutineFile(const CareRoutine& routine) {
    const std::string json = SerializeRoutine(routine);
    if (json.empty()) return false;
    return SaveTextFile(RoutineFilePath(routine.id).c_str(), json);
}

std::optional<CareRoutine> LoadRoutineFile(const RoutineId& id) {
    std::string json;
    if (!LoadTextFile(RoutineFilePath(id).c_str(), &json, kMaxRoutineRecordSize)) {
        return std::nullopt;
    }
    return DeserializeRoutine(json);
}

std::optional<CareRoutine> LoadRoutineMigrating(const std::string& nvs_namespace, const RoutineId& id) {
    std::optional<CareRoutine> routine = LoadRoutineFile(id);
    if (routine.has_value()) return routine;

    const std::string key = RoutineKey(id);

    nvs_handle_t handle = 0;
    if (nvs_open(nvs_namespace.c_str(), NVS_READONLY, &handle) != ESP_OK) {
        return std::nullopt;
    }

    std::string json;
    const bool ok = ReadString(handle, key.c_str(), &json) && !json.empty();
    nvs_close(handle);

    if (!ok) return std::nullopt;

    routine = DeserializeRoutine(json);
    if (routine.has_value()) {
        if (SaveRoutineFile(routine.value())) {
            DeleteRoutineNvsKey(nvs_namespace, key.c_str());
            ESP_LOGI(kTag, "Migrated routine %s from NVS to care_data", id.Str().c_str());
        } else {
            ESP_LOGW(kTag, "Loaded routine %s from NVS, but migration to care_data failed", id.Str().c_str());
        }
    }

    return routine;
}
NvsRoutineRepository::NvsRoutineRepository(std::string nvs_namespace)
    : nvs_namespace_(std::move(nvs_namespace)) {}

bool NvsRoutineRepository::Init() {
    std::vector<RoutineId> ids = ReadRoutineIndexMigrating(nvs_namespace_);
    WriteRoutineIndexFile(ids);

    uint32_t sequence = 1;
    ReadRoutineSequenceMigrating(nvs_namespace_, &sequence);
    WriteRoutineSequenceFile(nvs_namespace_, sequence);

    ESP_LOGI(kTag, "Routine repository ready");
    return true;
}

RoutineId NvsRoutineRepository::GenerateId() {
    uint32_t sequence = 1;
    if (!ReadRoutineSequenceMigrating(nvs_namespace_, &sequence)) {
        return RoutineId();
    }

    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "r%06lu", static_cast<unsigned long>(sequence));

    if (!WriteRoutineSequenceFile(nvs_namespace_, sequence + 1)) {
        ESP_LOGE(kTag, "Failed to save routine sequence");
        return RoutineId();
    }

    return RoutineId(buffer);
}

bool NvsRoutineRepository::Save(const CareRoutine& routine) {
    if (!routine.IsValid()) {
        ESP_LOGW(kTag, "Refusing to save invalid routine");
        return false;
    }

    std::vector<RoutineId> ids = ReadRoutineIndexMigrating(nvs_namespace_);
    if (!ContainsId(ids, routine.id)) {
        ids.push_back(routine.id);
    }

    const bool ok = SaveRoutineFile(routine) && WriteRoutineIndexFile(ids);
    if (!ok) {
        ESP_LOGE(kTag, "Failed to save routine %s", routine.id.Str().c_str());
        return false;
    }

    DeleteRoutineNvsKey(nvs_namespace_, RoutineKey(routine.id).c_str());
    DeleteRoutineNvsKey(nvs_namespace_, kIndexKey);

    ESP_LOGI(kTag, "Saved routine %s payload_bytes=%lu",
             routine.id.Str().c_str(),
             static_cast<unsigned long>(SerializeRoutine(routine).size()));
    return true;
}

std::optional<CareRoutine> NvsRoutineRepository::FindById(const RoutineId& id) {
    if (id.Empty()) {
        return std::nullopt;
    }

    return LoadRoutineMigrating(nvs_namespace_, id);
}

std::vector<CareRoutine> NvsRoutineRepository::List() {
    std::vector<RoutineId> ids = ReadRoutineIndexMigrating(nvs_namespace_);

    std::vector<CareRoutine> routines;
    routines.reserve(ids.size());

    for (const RoutineId& id : ids) {
        std::optional<CareRoutine> routine = LoadRoutineMigrating(nvs_namespace_, id);
        if (routine.has_value()) {
            routines.push_back(routine.value());
        }
    }

    return routines;
}

bool NvsRoutineRepository::Delete(const RoutineId& id) {
    if (id.Empty()) {
        return false;
    }

    std::vector<RoutineId> ids = ReadRoutineIndexMigrating(nvs_namespace_);
    const auto old_size = ids.size();
    ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());

    if (ids.size() == old_size) {
        return false;
    }

    DeleteTextFile(RoutineFilePath(id).c_str());
    DeleteRoutineNvsKey(nvs_namespace_, RoutineKey(id).c_str());

    const bool ok = WriteRoutineIndexFile(ids);
    if (!ok) {
        ESP_LOGE(kTag, "Failed to delete routine %s", id.Str().c_str());
        return false;
    }

    ESP_LOGI(kTag, "Deleted routine %s", id.Str().c_str());
    return true;
}

}  // namespace xiaozhi_care::daily

