#include "care_storage.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>
#include <utility>
#include <vector>

#include <cJSON.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_spiffs.h>
#include <esp_vfs.h>
#include <nvs.h>

namespace xiaozhi_care {
namespace {

constexpr char kTag[] = "CARE_STORAGE";
constexpr char kNamespace[] = "xiaozhi_care";
constexpr char kCareDataBasePath[] = "/care_data";
constexpr char kCareDataPartitionLabel[] = "care_data";
constexpr char kSchemaKey[] = "schema_ver";
constexpr char kProfileKey[] = "profile";
constexpr char kPersonSeqKey[] = "seq_person";
constexpr char kPreferenceSeqKey[] = "seq_pref";
constexpr char kPillboxSeqKey[] = "seq_pillbox";
constexpr char kReminderSeqKey[] = "seq_reminder";
constexpr char kAuthVersionKey[] = "auth_ver";
constexpr char kAuthIterationsKey[] = "auth_iter";
constexpr char kAuthSaltKey[] = "auth_salt";
constexpr char kAuthHashKey[] = "auth_hash";

bool IsNumericId(const std::string& id, const char* prefix) {
    const size_t prefix_len = std::strlen(prefix);
    if (id.size() != prefix_len + 6 || id.compare(0, prefix_len, prefix) != 0) {
        return false;
    }
    for (size_t i = prefix_len; i < id.size(); ++i) {
        if (id[i] < '0' || id[i] > '9') {
            return false;
        }
    }
    return true;
}

bool IsPersonId(const std::string& id) { return IsNumericId(id, "p"); }
bool IsPreferenceId(const std::string& id) { return IsNumericId(id, "pr"); }
bool IsPillboxId(const std::string& id) { return IsNumericId(id, "pb"); }
bool IsReminderId(const std::string& id) { return IsNumericId(id, "r"); }

std::string MakeId(const char* prefix, uint32_t sequence) {
    char id[16] = {};
    std::snprintf(id, sizeof(id), "%s%06lu", prefix, static_cast<unsigned long>(sequence));
    return id;
}

CareError MapNvsError(esp_err_t err) {
    if (err == ESP_OK) return CareError::OK;
    if (err == ESP_ERR_NVS_NOT_FOUND) return CareError::NOT_FOUND;
    if (err == ESP_ERR_NVS_NOT_ENOUGH_SPACE || err == ESP_ERR_NVS_PAGE_FULL) {
        return CareError::STORAGE_FULL;
    }
    return CareError::STORAGE_ERROR;
}

bool MountCareDataSpiffs() {
    static bool mounted = false;
    if (mounted) return true;

    esp_vfs_spiffs_conf_t conf = {};
    conf.base_path = kCareDataBasePath;
    conf.partition_label = kCareDataPartitionLabel;
    conf.max_files = 8;
    conf.format_if_mount_failed = true;

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(kTag, "care_data SPIFFS already mounted");
        mounted = true;
        return true;
    }
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Unable to mount care_data SPIFFS: %s", esp_err_to_name(err));
        return false;
    }

    size_t total = 0;
    size_t used = 0;
    if (esp_spiffs_info(kCareDataPartitionLabel, &total, &used) == ESP_OK) {
        ESP_LOGI(kTag, "care_data SPIFFS mounted total=%u used=%u",
                 static_cast<unsigned>(total),
                 static_cast<unsigned>(used));
    } else {
        ESP_LOGW(kTag, "care_data SPIFFS mounted, but info unavailable");
    }

    mounted = true;
    return true;
}
void AddString(cJSON* root, const char* name, const std::string& value) {
    cJSON_AddStringToObject(root, name, value.c_str());
}

std::string ReadOptionalString(const cJSON* root, const char* name) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (cJSON_IsString(item) && item->valuestring != nullptr) {
        return item->valuestring;
    }
    return {};
}

CareError FinishJson(cJSON* root, std::string& json) {
    if (root == nullptr) return CareError::SERIALIZATION_ERROR;
    char* raw = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (raw == nullptr) return CareError::SERIALIZATION_ERROR;
    json.assign(raw);
    cJSON_free(raw);
    if (json.size() > kMaxRecordSize) {
        json.clear();
        return CareError::TOO_LARGE;
    }
    return CareError::OK;
}

bool EnsureDir(const char* path) {
    if (path == nullptr || path[0] == '\0') return false;
    struct stat st = {};
    if (stat(path, &st) == 0) return S_ISDIR(st.st_mode);
    if (mkdir(path, 0775) == 0) return true;
    return errno == EEXIST;
}

bool EnsureCareDataDirs() {
    // SPIFFS does not provide real directories. Use flat files under /care_data.
    return true;
}

std::string MakeCareDataRecordPath(const char* key) {
    char path[128] = {};
    std::snprintf(path, sizeof(path), "%s/rec_%s.json", kCareDataBasePath, key);
    return path;
}

std::string MakeCareDataSequencePath(const char* key) {
    char path[128] = {};
    std::snprintf(path, sizeof(path), "%s/seq_%s.txt", kCareDataBasePath, key);
    return path;
}

CareError SaveTextFile(const std::string& path, const std::string& text) {
    if (!EnsureCareDataDirs()) return CareError::STORAGE_ERROR;
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) return CareError::STORAGE_ERROR;
    const size_t written = std::fwrite(text.data(), 1, text.size(), f);
    const int close_result = std::fclose(f);
    if (written != text.size() || close_result != 0) return CareError::STORAGE_ERROR;
    return CareError::OK;
}

CareError LoadTextFile(const std::string& path, std::string& text, size_t max_size) {
    text.clear();
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return CareError::NOT_FOUND;

    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        return CareError::STORAGE_ERROR;
    }

    long len = std::ftell(f);
    if (len < 0) {
        std::fclose(f);
        return CareError::STORAGE_ERROR;
    }

    if (static_cast<size_t>(len) > max_size) {
        std::fclose(f);
        return CareError::TOO_LARGE;
    }

    std::rewind(f);
    text.resize(static_cast<size_t>(len));
    const size_t got = text.empty() ? 0 : std::fread(text.data(), 1, text.size(), f);
    const int close_result = std::fclose(f);

    if ((!text.empty() && got != text.size()) || close_result != 0) {
        text.clear();
        return CareError::STORAGE_ERROR;
    }

    return CareError::OK;
}

CareError DeleteTextFile(const std::string& path) {
    if (std::remove(path.c_str()) == 0) return CareError::OK;
    return errno == ENOENT ? CareError::NOT_FOUND : CareError::STORAGE_ERROR;
}

CareError NvsSaveStringRecord(const char* key, const std::string& json) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err == ESP_OK) err = nvs_set_str(handle, key, json.c_str());
    if (err == ESP_OK) err = nvs_commit(handle);
    if (handle != 0) nvs_close(handle);
    return MapNvsError(err);
}

CareError NvsLoadStringRecord(const char* key, std::string& json) {
    json.clear();
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (err != ESP_OK) return MapNvsError(err);

    size_t required = 0;
    err = nvs_get_str(handle, key, nullptr, &required);
    if (err != ESP_OK) {
        nvs_close(handle);
        return MapNvsError(err);
    }
    if (required == 0 || required > (kMaxRecordSize + 1)) {
        nvs_close(handle);
        return CareError::TOO_LARGE;
    }

    std::vector<char> buffer(required);
    err = nvs_get_str(handle, key, buffer.data(), &required);
    nvs_close(handle);
    if (err != ESP_OK) return MapNvsError(err);
    json.assign(buffer.data());
    return CareError::OK;
}

CareError NvsDeleteRecord(const char* key) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err == ESP_OK) err = nvs_erase_key(handle, key);
    if (err == ESP_OK) err = nvs_commit(handle);
    if (handle != 0) nvs_close(handle);
    return MapNvsError(err);
}

CareError NvsReadSequence(const char* key, uint32_t& sequence) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (err == ESP_OK) err = nvs_get_u32(handle, key, &sequence);
    if (handle != 0) nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        sequence = 0;
        return CareError::OK;
    }
    return MapNvsError(err);
}

CareError NvsWriteSequence(const char* key, uint32_t sequence) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err == ESP_OK) err = nvs_set_u32(handle, key, sequence);
    if (err == ESP_OK) err = nvs_commit(handle);
    if (handle != 0) nvs_close(handle);
    return MapNvsError(err);
}

CareError SaveStringRecord(const char* key, const std::string& json) {
    const std::string path = MakeCareDataRecordPath(key);
    CareError file_result = SaveTextFile(path, json);
    if (file_result != CareError::OK) return file_result;

    CareError cleanup = NvsDeleteRecord(key);
    if (cleanup != CareError::OK && cleanup != CareError::NOT_FOUND) {
        ESP_LOGW(kTag, "Record saved to care_data but old NVS key was not removed: %s", key);
    }

    return CareError::OK;
}

CareError LoadStringRecord(const char* key, std::string& json) {
    const std::string path = MakeCareDataRecordPath(key);

    CareError file_result = LoadTextFile(path, json, kMaxRecordSize);
    if (file_result == CareError::OK) return CareError::OK;
    if (file_result != CareError::NOT_FOUND) return file_result;

    CareError nvs_result = NvsLoadStringRecord(key, json);
    if (nvs_result != CareError::OK) return nvs_result;

    CareError migrate = SaveTextFile(path, json);
    if (migrate == CareError::OK) {
        CareError cleanup = NvsDeleteRecord(key);
        if (cleanup != CareError::OK && cleanup != CareError::NOT_FOUND) {
            ESP_LOGW(kTag, "Migrated %s to care_data, but old NVS key was not removed", key);
        } else {
            ESP_LOGI(kTag, "Migrated %s from NVS to care_data", key);
        }
    } else {
        ESP_LOGW(kTag, "Loaded %s from NVS, but migration to care_data failed: %s",
                 key, CareErrorToString(migrate));
    }

    return CareError::OK;
}

CareError DeleteRecord(const char* key) {
    const std::string path = MakeCareDataRecordPath(key);
    CareError file_result = DeleteTextFile(path);
    CareError nvs_result = NvsDeleteRecord(key);

    if (file_result == CareError::OK || nvs_result == CareError::OK) return CareError::OK;
    if (file_result == CareError::NOT_FOUND && nvs_result == CareError::NOT_FOUND) return CareError::NOT_FOUND;
    return file_result != CareError::NOT_FOUND ? file_result : nvs_result;
}

CareError ReadSequence(const char* key, uint32_t& sequence) {
    sequence = 0;
    const std::string path = MakeCareDataSequencePath(key);

    std::string raw;
    CareError file_result = LoadTextFile(path, raw, 32);
    if (file_result == CareError::OK) {
        char* end = nullptr;
        unsigned long value = std::strtoul(raw.c_str(), &end, 10);
        if (end == raw.c_str()) return CareError::DESERIALIZATION_ERROR;
        sequence = static_cast<uint32_t>(value);
        return CareError::OK;
    }
    if (file_result != CareError::NOT_FOUND) return file_result;

    CareError nvs_result = NvsReadSequence(key, sequence);
    if (nvs_result != CareError::OK) return nvs_result;

    if (sequence > 0) {
        char buffer[24] = {};
        std::snprintf(buffer, sizeof(buffer), "%lu", static_cast<unsigned long>(sequence));
        CareError migrate = SaveTextFile(path, buffer);
        if (migrate == CareError::OK) {
            CareError cleanup = NvsDeleteRecord(key);
            if (cleanup != CareError::OK && cleanup != CareError::NOT_FOUND) {
                ESP_LOGW(kTag, "Migrated sequence %s, but old NVS key was not removed", key);
            } else {
                ESP_LOGI(kTag, "Migrated sequence %s from NVS to care_data", key);
            }
        }
    }

    return CareError::OK;
}

CareError WriteSequence(const char* key, uint32_t sequence) {
    char buffer[24] = {};
    std::snprintf(buffer, sizeof(buffer), "%lu", static_cast<unsigned long>(sequence));

    CareError file_result = SaveTextFile(MakeCareDataSequencePath(key), buffer);
    if (file_result != CareError::OK) return file_result;

    CareError cleanup = NvsDeleteRecord(key);
    if (cleanup != CareError::OK && cleanup != CareError::NOT_FOUND) {
        ESP_LOGW(kTag, "Sequence saved to care_data but old NVS key was not removed: %s", key);
    }

    return CareError::OK;
}
esp_err_t EnsureU32Key(nvs_handle_t handle, const char* key, uint32_t initial, bool& changed) {
    uint32_t value = 0;
    esp_err_t err = nvs_get_u32(handle, key, &value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = nvs_set_u32(handle, key, initial);
        if (err == ESP_OK) changed = true;
    }
    return err;
}

}  // namespace

const char* CareErrorToString(CareError error) {
    switch (error) {
        case CareError::OK: return "OK";
        case CareError::NOT_INITIALIZED: return "NOT_INITIALIZED";
        case CareError::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case CareError::INVALID_ID: return "INVALID_ID";
        case CareError::INVALID_DATE: return "INVALID_DATE";
        case CareError::INVALID_TIME: return "INVALID_TIME";
        case CareError::TOO_LONG: return "TOO_LONG";
        case CareError::TOO_LARGE: return "TOO_LARGE";
        case CareError::LIMIT_REACHED: return "LIMIT_REACHED";
        case CareError::NOT_FOUND: return "NOT_FOUND";
        case CareError::ALREADY_EXISTS: return "ALREADY_EXISTS";
        case CareError::IN_USE: return "IN_USE";
        case CareError::SERIALIZATION_ERROR: return "SERIALIZATION_ERROR";
        case CareError::DESERIALIZATION_ERROR: return "DESERIALIZATION_ERROR";
        case CareError::STORAGE_ERROR: return "STORAGE_ERROR";
        case CareError::STORAGE_FULL: return "STORAGE_FULL";
        case CareError::UNSUPPORTED_VERSION: return "UNSUPPORTED_VERSION";
        case CareError::INTERNAL_ERROR: return "INTERNAL_ERROR";
        default: return "UNKNOWN";
    }
}

CareError CareStorage::Init() {
    std::lock_guard<std::mutex> lock(mutex_);

    // DP-030-r1a: mount the dedicated care_data partition.
    // This step is intentionally read/write ready but does not migrate or delete NVS data yet.
    if (!MountCareDataSpiffs()) {
        ESP_LOGE(kTag, "care_data storage unavailable");
        initialized_ = false;
        return CareError::STORAGE_ERROR;
    }

    // XiaoZhi initializes NVS before Care starts. Care must never erase the
    // shared NVS partition because it may contain Wi-Fi/device configuration.
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Unable to open NVS namespace: %s", esp_err_to_name(err));
        initialized_ = false;
        return MapNvsError(err);
    }
    nvs_close(handle);

    CareError schema_result = CheckSchemaLocked();
    initialized_ = (schema_result == CareError::OK);
    if (initialized_) {
        ESP_LOGI(kTag, "Storage ready (schema=%lu)", static_cast<unsigned long>(kSchemaVersion));
    }
    return schema_result;
}

bool CareStorage::IsReady() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return initialized_;
}

CareError CareStorage::CheckSchemaLocked() {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) return MapNvsError(err);

    bool changed = false;
    uint32_t schema = 0;
    err = nvs_get_u32(handle, kSchemaKey, &schema);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        schema = kSchemaVersion;
        err = nvs_set_u32(handle, kSchemaKey, schema);
        if (err == ESP_OK) changed = true;
    }
    if (err == ESP_OK && schema != kSchemaVersion) {
        nvs_close(handle);
        ESP_LOGE(kTag, "Unsupported schema %lu (expected %lu)",
                 static_cast<unsigned long>(schema),
                 static_cast<unsigned long>(kSchemaVersion));
        return CareError::UNSUPPORTED_VERSION;
    }

    // DP-030-r1b: keep only the minimal schema marker in NVS.
    // Record sequences now live in /care_data/seq and are migrated lazily.
    if (err == ESP_OK && changed) err = nvs_commit(handle);
    nvs_close(handle);
    return MapNvsError(err);
}

CareError CareStorage::SerializeProfile(const CareProfile& profile, std::string& json) const {
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) return CareError::SERIALIZATION_ERROR;
    cJSON_AddNumberToObject(root, "v", static_cast<double>(profile.version));
    AddString(root, "name", profile.name);
    AddString(root, "nickname", profile.nickname);
    AddString(root, "birthday", profile.birthday);
    AddString(root, "city", profile.city);
    AddString(root, "timezone", profile.timezone);
    AddString(root, "notes", profile.notes);
    return FinishJson(root, json);
}

CareError CareStorage::DeserializeProfile(const std::string& json, CareProfile& profile) const {
    cJSON* root = cJSON_ParseWithLength(json.c_str(), json.size());
    if (root == nullptr) return CareError::DESERIALIZATION_ERROR;
    const cJSON* version = cJSON_GetObjectItemCaseSensitive(root, "v");
    const cJSON* name = cJSON_GetObjectItemCaseSensitive(root, "name");
    if (!cJSON_IsNumber(version) || !cJSON_IsString(name) || name->valuestring == nullptr) {
        cJSON_Delete(root);
        return CareError::DESERIALIZATION_ERROR;
    }
    if (version->valueint != static_cast<int>(kProfileRecordVersion)) {
        cJSON_Delete(root);
        return CareError::UNSUPPORTED_VERSION;
    }
    CareProfile parsed;
    parsed.version = static_cast<uint32_t>(version->valueint);
    parsed.name = name->valuestring;
    parsed.nickname = ReadOptionalString(root, "nickname");
    parsed.birthday = ReadOptionalString(root, "birthday");
    parsed.city = ReadOptionalString(root, "city");
    parsed.timezone = ReadOptionalString(root, "timezone");
    parsed.notes = ReadOptionalString(root, "notes");
    cJSON_Delete(root);
    profile = std::move(parsed);
    return CareError::OK;
}

CareError CareStorage::SerializePerson(const CarePerson& person, std::string& json) const {
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) return CareError::SERIALIZATION_ERROR;
    cJSON_AddNumberToObject(root, "v", static_cast<double>(person.version));
    AddString(root, "id", person.id);
    AddString(root, "name", person.name);
    AddString(root, "nickname", person.nickname);
    AddString(root, "relationship", person.relationship);
    AddString(root, "phone", person.phone);
    AddString(root, "address", person.address);
    AddString(root, "birthday", person.birthday);
    cJSON_AddBoolToObject(root, "has_pet", person.has_pet);
    AddString(root, "pet_type", person.pet_type);
    AddString(root, "pet_name", person.pet_name);
    cJSON* aliases = cJSON_AddArrayToObject(root, "aliases");
    if (aliases == nullptr) { cJSON_Delete(root); return CareError::SERIALIZATION_ERROR; }
    for (const auto& alias : person.aliases) {
        cJSON* value = cJSON_CreateString(alias.c_str());
        if (value == nullptr) { cJSON_Delete(root); return CareError::SERIALIZATION_ERROR; }
        cJSON_AddItemToArray(aliases, value);
    }
    AddString(root, "notes", person.notes);
    cJSON_AddBoolToObject(root, "enabled", person.enabled);
    return FinishJson(root, json);
}

CareError CareStorage::DeserializePerson(const std::string& json, CarePerson& person) const {
    cJSON* root = cJSON_ParseWithLength(json.c_str(), json.size());
    if (root == nullptr) return CareError::DESERIALIZATION_ERROR;
    const cJSON* version = cJSON_GetObjectItemCaseSensitive(root, "v");
    const cJSON* id = cJSON_GetObjectItemCaseSensitive(root, "id");
    const cJSON* name = cJSON_GetObjectItemCaseSensitive(root, "name");
    if (!cJSON_IsNumber(version) || !cJSON_IsString(id) || !cJSON_IsString(name) ||
        id->valuestring == nullptr || name->valuestring == nullptr) {
        cJSON_Delete(root); return CareError::DESERIALIZATION_ERROR;
    }
    if (version->valueint != static_cast<int>(kPersonRecordVersion)) {
        cJSON_Delete(root); return CareError::UNSUPPORTED_VERSION;
    }
    CarePerson parsed;
    parsed.version = static_cast<uint32_t>(version->valueint);
    parsed.id = id->valuestring;
    parsed.name = name->valuestring;
    parsed.nickname = ReadOptionalString(root, "nickname");
    parsed.relationship = ReadOptionalString(root, "relationship");
    parsed.phone = ReadOptionalString(root, "phone");
    parsed.address = ReadOptionalString(root, "address");
    parsed.birthday = ReadOptionalString(root, "birthday");
    parsed.has_pet = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "has_pet"));
    parsed.pet_type = ReadOptionalString(root, "pet_type");
    parsed.pet_name = ReadOptionalString(root, "pet_name");
    parsed.notes = ReadOptionalString(root, "notes");
    const cJSON* enabled = cJSON_GetObjectItemCaseSensitive(root, "enabled");
    parsed.enabled = !cJSON_IsBool(enabled) || cJSON_IsTrue(enabled);
    const cJSON* aliases = cJSON_GetObjectItemCaseSensitive(root, "aliases");
    if (aliases != nullptr) {
        if (!cJSON_IsArray(aliases)) { cJSON_Delete(root); return CareError::DESERIALIZATION_ERROR; }
        const cJSON* item = nullptr;
        cJSON_ArrayForEach(item, aliases) {
            if (!cJSON_IsString(item) || item->valuestring == nullptr) {
                cJSON_Delete(root); return CareError::DESERIALIZATION_ERROR;
            }
            parsed.aliases.emplace_back(item->valuestring);
        }
    }
    cJSON_Delete(root);
    if (!IsPersonId(parsed.id)) return CareError::INVALID_ID;
    person = std::move(parsed);
    return CareError::OK;
}

CareError CareStorage::SerializePreference(const CarePreference& preference, std::string& json) const {
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) return CareError::SERIALIZATION_ERROR;
    cJSON_AddNumberToObject(root, "v", static_cast<double>(preference.version));
    AddString(root, "id", preference.id);
    AddString(root, "owner_id", preference.owner_id);
    AddString(root, "category", preference.category);
    AddString(root, "value", preference.value);
    AddString(root, "notes", preference.notes);
    cJSON_AddBoolToObject(root, "enabled", preference.enabled);
    return FinishJson(root, json);
}

CareError CareStorage::DeserializePreference(const std::string& json, CarePreference& preference) const {
    cJSON* root = cJSON_ParseWithLength(json.c_str(), json.size());
    if (root == nullptr) return CareError::DESERIALIZATION_ERROR;
    const cJSON* version = cJSON_GetObjectItemCaseSensitive(root, "v");
    const cJSON* id = cJSON_GetObjectItemCaseSensitive(root, "id");
    const cJSON* category = cJSON_GetObjectItemCaseSensitive(root, "category");
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(root, "value");
    if (!cJSON_IsNumber(version) || !cJSON_IsString(id) || !cJSON_IsString(category) ||
        !cJSON_IsString(value) || id->valuestring == nullptr || category->valuestring == nullptr ||
        value->valuestring == nullptr) {
        cJSON_Delete(root); return CareError::DESERIALIZATION_ERROR;
    }
    // v1 preferences had no owner_id and always belonged to the primary profile.
    // v2 adds owner_id while keeping the global NVS schema at version 1.
    if (version->valueint != 1 && version->valueint != static_cast<int>(kPreferenceRecordVersion)) {
        cJSON_Delete(root); return CareError::UNSUPPORTED_VERSION;
    }
    CarePreference parsed;
    parsed.version = kPreferenceRecordVersion;
    parsed.id = id->valuestring;
    parsed.owner_id = ReadOptionalString(root, "owner_id");
    if (parsed.owner_id.empty()) parsed.owner_id = "profile";
    parsed.category = category->valuestring;
    parsed.value = value->valuestring;
    parsed.notes = ReadOptionalString(root, "notes");
    const cJSON* enabled = cJSON_GetObjectItemCaseSensitive(root, "enabled");
    parsed.enabled = !cJSON_IsBool(enabled) || cJSON_IsTrue(enabled);
    cJSON_Delete(root);
    if (!IsPreferenceId(parsed.id)) return CareError::INVALID_ID;
    preference = std::move(parsed);
    return CareError::OK;
}

CareError CareStorage::SerializePillboxEntry(const CarePillboxEntry& entry, std::string& json) const {
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) return CareError::SERIALIZATION_ERROR;
    cJSON_AddNumberToObject(root, "v", static_cast<double>(entry.version));
    AddString(root, "id", entry.id);
    cJSON_AddNumberToObject(root, "weekday", static_cast<double>(entry.weekday));
    AddString(root, "period", entry.period);
    AddString(root, "time", entry.time);
    AddString(root, "compartment", entry.compartment);
    AddString(root, "pill_color", entry.pill_color);
    AddString(root, "notes", entry.notes);
    cJSON_AddBoolToObject(root, "enabled", entry.enabled);
    return FinishJson(root, json);
}

CareError CareStorage::DeserializePillboxEntry(const std::string& json, CarePillboxEntry& entry) const {
    cJSON* root = cJSON_ParseWithLength(json.c_str(), json.size());
    if (root == nullptr) return CareError::DESERIALIZATION_ERROR;
    const cJSON* version = cJSON_GetObjectItemCaseSensitive(root, "v");
    const cJSON* id = cJSON_GetObjectItemCaseSensitive(root, "id");
    const cJSON* weekday = cJSON_GetObjectItemCaseSensitive(root, "weekday");
    const cJSON* period = cJSON_GetObjectItemCaseSensitive(root, "period");
    const cJSON* compartment = cJSON_GetObjectItemCaseSensitive(root, "compartment");
    if (!cJSON_IsNumber(version) || !cJSON_IsString(id) || !cJSON_IsNumber(weekday) ||
        !cJSON_IsString(period) || !cJSON_IsString(compartment) || id->valuestring == nullptr ||
        period->valuestring == nullptr || compartment->valuestring == nullptr) {
        cJSON_Delete(root); return CareError::DESERIALIZATION_ERROR;
    }
    if (version->valueint != static_cast<int>(kPillboxRecordVersion)) {
        cJSON_Delete(root); return CareError::UNSUPPORTED_VERSION;
    }
    CarePillboxEntry parsed;
    parsed.version = static_cast<uint32_t>(version->valueint);
    parsed.id = id->valuestring;
    parsed.weekday = static_cast<uint8_t>(weekday->valueint);
    parsed.period = period->valuestring;
    parsed.time = ReadOptionalString(root, "time");
    parsed.compartment = compartment->valuestring;
    parsed.pill_color = ReadOptionalString(root, "pill_color");
    parsed.notes = ReadOptionalString(root, "notes");
    const cJSON* enabled = cJSON_GetObjectItemCaseSensitive(root, "enabled");
    parsed.enabled = !cJSON_IsBool(enabled) || cJSON_IsTrue(enabled);
    cJSON_Delete(root);
    if (!IsPillboxId(parsed.id)) return CareError::INVALID_ID;
    entry = std::move(parsed);
    return CareError::OK;
}

CareError CareStorage::SerializeReminder(const CareReminder& reminder, std::string& json) const {
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) return CareError::SERIALIZATION_ERROR;
    cJSON_AddNumberToObject(root, "v", static_cast<double>(reminder.version));
    AddString(root, "id", reminder.id);
    AddString(root, "title", reminder.title);
    AddString(root, "date", reminder.date);
    AddString(root, "time", reminder.time);
    AddString(root, "recurrence", reminder.recurrence);
    AddString(root, "related_person_id", reminder.related_person_id);
    AddString(root, "notes", reminder.notes);
    cJSON_AddBoolToObject(root, "enabled", reminder.enabled);
    return FinishJson(root, json);
}

CareError CareStorage::DeserializeReminder(const std::string& json, CareReminder& reminder) const {
    cJSON* root = cJSON_ParseWithLength(json.c_str(), json.size());
    if (root == nullptr) return CareError::DESERIALIZATION_ERROR;
    const cJSON* version = cJSON_GetObjectItemCaseSensitive(root, "v");
    const cJSON* id = cJSON_GetObjectItemCaseSensitive(root, "id");
    const cJSON* title = cJSON_GetObjectItemCaseSensitive(root, "title");
    const cJSON* date = cJSON_GetObjectItemCaseSensitive(root, "date");
    const cJSON* recurrence = cJSON_GetObjectItemCaseSensitive(root, "recurrence");
    if (!cJSON_IsNumber(version) || !cJSON_IsString(id) || !cJSON_IsString(title) ||
        !cJSON_IsString(date) || !cJSON_IsString(recurrence) || id->valuestring == nullptr ||
        title->valuestring == nullptr || date->valuestring == nullptr || recurrence->valuestring == nullptr) {
        cJSON_Delete(root); return CareError::DESERIALIZATION_ERROR;
    }
    if (version->valueint != static_cast<int>(kReminderRecordVersion)) {
        cJSON_Delete(root); return CareError::UNSUPPORTED_VERSION;
    }
    CareReminder parsed;
    parsed.version = static_cast<uint32_t>(version->valueint);
    parsed.id = id->valuestring;
    parsed.title = title->valuestring;
    parsed.date = date->valuestring;
    parsed.time = ReadOptionalString(root, "time");
    parsed.recurrence = recurrence->valuestring;
    parsed.related_person_id = ReadOptionalString(root, "related_person_id");
    parsed.notes = ReadOptionalString(root, "notes");
    const cJSON* enabled = cJSON_GetObjectItemCaseSensitive(root, "enabled");
    parsed.enabled = !cJSON_IsBool(enabled) || cJSON_IsTrue(enabled);
    cJSON_Delete(root);
    if (!IsReminderId(parsed.id)) return CareError::INVALID_ID;
    reminder = std::move(parsed);
    return CareError::OK;
}

CareError CareStorage::SaveProfile(const CareProfile& profile) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) return CareError::NOT_INITIALIZED;
    std::string json;
    CareError result = SerializeProfile(profile, json);
    if (result != CareError::OK) return result;
    return SaveStringRecord(kProfileKey, json);
}

CareError CareStorage::LoadProfile(CareProfile& profile) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) return CareError::NOT_INITIALIZED;
    std::string json;
    CareError result = LoadStringRecord(kProfileKey, json);
    if (result != CareError::OK) return result;
    return DeserializeProfile(json, profile);
}

#define DEFINE_RECORD_STORAGE(NAME, TYPE, ID_CHECK, SERIALIZE, DESERIALIZE) \
CareError CareStorage::Save##NAME(const TYPE& value) { \
    std::lock_guard<std::mutex> lock(mutex_); \
    if (!initialized_) return CareError::NOT_INITIALIZED; \
    if (!ID_CHECK(value.id)) return CareError::INVALID_ID; \
    std::string json; \
    CareError result = SERIALIZE(value, json); \
    if (result != CareError::OK) return result; \
    return SaveStringRecord(value.id.c_str(), json); \
} \
CareError CareStorage::Load##NAME(const std::string& id, TYPE& value) { \
    std::lock_guard<std::mutex> lock(mutex_); \
    if (!initialized_) return CareError::NOT_INITIALIZED; \
    if (!ID_CHECK(id)) return CareError::INVALID_ID; \
    std::string json; \
    CareError result = LoadStringRecord(id.c_str(), json); \
    if (result != CareError::OK) return result; \
    return DESERIALIZE(json, value); \
} \
CareError CareStorage::Delete##NAME(const std::string& id) { \
    std::lock_guard<std::mutex> lock(mutex_); \
    if (!initialized_) return CareError::NOT_INITIALIZED; \
    if (!ID_CHECK(id)) return CareError::INVALID_ID; \
    return DeleteRecord(id.c_str()); \
}

DEFINE_RECORD_STORAGE(Person, CarePerson, IsPersonId, SerializePerson, DeserializePerson)
DEFINE_RECORD_STORAGE(Preference, CarePreference, IsPreferenceId, SerializePreference, DeserializePreference)
DEFINE_RECORD_STORAGE(PillboxEntry, CarePillboxEntry, IsPillboxId, SerializePillboxEntry, DeserializePillboxEntry)
DEFINE_RECORD_STORAGE(Reminder, CareReminder, IsReminderId, SerializeReminder, DeserializeReminder)

#undef DEFINE_RECORD_STORAGE

#define DEFINE_SEQUENCE_METHODS(NAME, KEY) \
CareError CareStorage::Get##NAME##Sequence(uint32_t& sequence) { \
    std::lock_guard<std::mutex> lock(mutex_); \
    if (!initialized_) return CareError::NOT_INITIALIZED; \
    return ReadSequence(KEY, sequence); \
} \
CareError CareStorage::Set##NAME##Sequence(uint32_t sequence) { \
    std::lock_guard<std::mutex> lock(mutex_); \
    if (!initialized_) return CareError::NOT_INITIALIZED; \
    return WriteSequence(KEY, sequence); \
}

DEFINE_SEQUENCE_METHODS(Person, kPersonSeqKey)
DEFINE_SEQUENCE_METHODS(Preference, kPreferenceSeqKey)
DEFINE_SEQUENCE_METHODS(Pillbox, kPillboxSeqKey)
DEFINE_SEQUENCE_METHODS(Reminder, kReminderSeqKey)

#undef DEFINE_SEQUENCE_METHODS

CareError CareStorage::SaveAdminAuth(const CareAuthRecord& auth) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) return CareError::NOT_INITIALIZED;
    if (auth.version != kAuthRecordVersion || auth.iterations == 0 ||
        auth.salt_hex.size() != kAuthSaltBytes * 2 || auth.hash_hex.size() != kAuthHashBytes * 2) {
        return CareError::INVALID_ARGUMENT;
    }
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err == ESP_OK) err = nvs_set_u32(handle, kAuthVersionKey, auth.version);
    if (err == ESP_OK) err = nvs_set_u32(handle, kAuthIterationsKey, auth.iterations);
    if (err == ESP_OK) err = nvs_set_str(handle, kAuthSaltKey, auth.salt_hex.c_str());
    if (err == ESP_OK) err = nvs_set_str(handle, kAuthHashKey, auth.hash_hex.c_str());
    if (err == ESP_OK) err = nvs_commit(handle);
    if (handle != 0) nvs_close(handle);
    return MapNvsError(err);
}

CareError CareStorage::LoadAdminAuth(CareAuthRecord& auth) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) return CareError::NOT_INITIALIZED;

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (err != ESP_OK) return MapNvsError(err);

    uint32_t version = 0;
    uint32_t iterations = 0;
    err = nvs_get_u32(handle, kAuthVersionKey, &version);
    if (err == ESP_ERR_NVS_NOT_FOUND) { nvs_close(handle); return CareError::NOT_FOUND; }
    if (err != ESP_OK) { nvs_close(handle); return MapNvsError(err); }
    if (version != kAuthRecordVersion) { nvs_close(handle); return CareError::UNSUPPORTED_VERSION; }

    err = nvs_get_u32(handle, kAuthIterationsKey, &iterations);
    if (err != ESP_OK || iterations == 0) {
        nvs_close(handle);
        return err == ESP_OK ? CareError::DESERIALIZATION_ERROR : MapNvsError(err);
    }

    auto read_string = [handle](const char* key, std::string& value) -> esp_err_t {
        size_t required = 0;
        esp_err_t read_err = nvs_get_str(handle, key, nullptr, &required);
        if (read_err != ESP_OK) return read_err;
        if (required == 0 || required > 129) return ESP_ERR_INVALID_SIZE;
        std::vector<char> buffer(required);
        read_err = nvs_get_str(handle, key, buffer.data(), &required);
        if (read_err == ESP_OK) value.assign(buffer.data());
        return read_err;
    };

    std::string salt_hex;
    std::string hash_hex;
    err = read_string(kAuthSaltKey, salt_hex);
    if (err == ESP_OK) err = read_string(kAuthHashKey, hash_hex);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) return CareError::DESERIALIZATION_ERROR;
    if (err != ESP_OK) return MapNvsError(err);
    if (salt_hex.size() != kAuthSaltBytes * 2 || hash_hex.size() != kAuthHashBytes * 2) {
        return CareError::DESERIALIZATION_ERROR;
    }

    CareAuthRecord loaded;
    loaded.version = version;
    loaded.iterations = iterations;
    loaded.salt_hex = std::move(salt_hex);
    loaded.hash_hex = std::move(hash_hex);
    auth = std::move(loaded);
    return CareError::OK;
}

CareError CareStorage::ListPeople(std::vector<CarePerson>& result) {
    result.clear();
    uint32_t sequence = 0;
    CareError seq_result = GetPersonSequence(sequence);
    if (seq_result != CareError::OK) return seq_result;
    for (uint32_t i = 1; i <= sequence; ++i) {
        CarePerson value;
        CareError load = LoadPerson(MakeId("p", i), value);
        if (load == CareError::NOT_FOUND) continue;
        if (load != CareError::OK) {
            ESP_LOGW(kTag, "Skipping unreadable person p%06lu: %s",
                     static_cast<unsigned long>(i), CareErrorToString(load));
            continue;
        }
        result.emplace_back(std::move(value));
    }
    return CareError::OK;
}

CareError CareStorage::ListPreferences(std::vector<CarePreference>& result) {
    result.clear();
    uint32_t sequence = 0;
    CareError seq_result = GetPreferenceSequence(sequence);
    if (seq_result != CareError::OK) return seq_result;
    for (uint32_t i = 1; i <= sequence; ++i) {
        CarePreference value;
        CareError load = LoadPreference(MakeId("pr", i), value);
        if (load == CareError::NOT_FOUND) continue;
        if (load != CareError::OK) {
            ESP_LOGW(kTag, "Skipping unreadable preference pr%06lu: %s",
                     static_cast<unsigned long>(i), CareErrorToString(load));
            continue;
        }
        result.emplace_back(std::move(value));
    }
    return CareError::OK;
}

CareError CareStorage::ListPillboxEntries(std::vector<CarePillboxEntry>& result) {
    result.clear();
    uint32_t sequence = 0;
    CareError seq_result = GetPillboxSequence(sequence);
    if (seq_result != CareError::OK) return seq_result;
    for (uint32_t i = 1; i <= sequence; ++i) {
        CarePillboxEntry value;
        CareError load = LoadPillboxEntry(MakeId("pb", i), value);
        if (load == CareError::NOT_FOUND) continue;
        if (load != CareError::OK) {
            ESP_LOGW(kTag, "Skipping unreadable pillbox entry pb%06lu: %s",
                     static_cast<unsigned long>(i), CareErrorToString(load));
            continue;
        }
        result.emplace_back(std::move(value));
    }
    return CareError::OK;
}

CareError CareStorage::ListReminders(std::vector<CareReminder>& result) {
    result.clear();
    uint32_t sequence = 0;
    CareError seq_result = GetReminderSequence(sequence);
    if (seq_result != CareError::OK) return seq_result;
    for (uint32_t i = 1; i <= sequence; ++i) {
        CareReminder value;
        CareError load = LoadReminder(MakeId("r", i), value);
        if (load == CareError::NOT_FOUND) continue;
        if (load != CareError::OK) {
            ESP_LOGW(kTag, "Skipping unreadable reminder r%06lu: %s",
                     static_cast<unsigned long>(i), CareErrorToString(load));
            continue;
        }
        result.emplace_back(std::move(value));
    }
    return CareError::OK;
}

}  // namespace xiaozhi_care



