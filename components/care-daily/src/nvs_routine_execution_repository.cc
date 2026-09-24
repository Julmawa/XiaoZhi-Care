#include "care_daily/nvs_routine_execution_repository.h"

#include <algorithm>
#include <cstdio>
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

constexpr char kTag[] = "CARE_DAILY_EXEC";
constexpr char kIndexKey[] = "de_idx";
constexpr char kSequenceKey[] = "de_seq";
constexpr char kExecutionKeyPrefix[] = "de_";
constexpr char kCareDataBasePath[] = "/care_data";
constexpr char kExecutionIndexPath[] = "/care_data/daily_execution_index.json";
constexpr char kExecutionSequencePath[] = "/care_data/daily_execution_seq.txt";
constexpr size_t kMaxExecutionRecordSize = 2048;
constexpr size_t kMaxExecutionIndexSize = 4096;

std::string ExecutionKey(const RoutineExecutionId& id) {
    return std::string(kExecutionKeyPrefix) + id.Str();
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

std::string SerializeIndex(const std::vector<RoutineExecutionId>& ids) {
    cJSON* root = cJSON_CreateArray();
    if (root == nullptr) {
        return {};
    }

    for (const RoutineExecutionId& id : ids) {
        cJSON_AddItemToArray(root, cJSON_CreateString(id.Str().c_str()));
    }

    std::string result = JsonPrint(root);
    cJSON_Delete(root);
    return result;
}

std::vector<RoutineExecutionId> ParseIndex(const std::string& json) {
    std::vector<RoutineExecutionId> ids;
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


bool ContainsId(const std::vector<RoutineExecutionId>& ids, const RoutineExecutionId& id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

std::string SerializeExecution(const RoutineExecution& execution) {
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) {
        return {};
    }

    AddString(root, "id", execution.id.Str());
    AddString(root, "routine_id", execution.routine_id.Str());
    cJSON_AddNumberToObject(root, "event", static_cast<int>(execution.event));
    cJSON_AddNumberToObject(root, "source", static_cast<int>(execution.source));
    AddString(root, "iso_date", execution.iso_date);
    cJSON_AddNumberToObject(root, "hour", execution.time.hour);
    cJSON_AddNumberToObject(root, "minute", execution.time.minute);
    AddString(root, "message", execution.message);
    AddString(root, "note", execution.note);

    std::string result = JsonPrint(root);
    cJSON_Delete(root);
    return result;
}

std::optional<RoutineExecution> DeserializeExecution(const std::string& json) {
    if (json.empty()) {
        return std::nullopt;
    }

    cJSON* root = cJSON_Parse(json.c_str());
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return std::nullopt;
    }

    RoutineExecution execution;
    execution.id = RoutineExecutionId(GetString(root, "id"));
    execution.routine_id = RoutineId(GetString(root, "routine_id"));
    execution.event = static_cast<RoutineExecutionEvent>(GetInt(root, "event", static_cast<int>(RoutineExecutionEvent::Indicated)));
    execution.source = static_cast<RoutineExecutionSource>(GetInt(root, "source", static_cast<int>(RoutineExecutionSource::Engine)));
    execution.iso_date = GetString(root, "iso_date");
    execution.time.hour = static_cast<uint8_t>(GetInt(root, "hour", 0));
    execution.time.minute = static_cast<uint8_t>(GetInt(root, "minute", 0));
    execution.message = GetString(root, "message");
    execution.note = GetString(root, "note");

    cJSON_Delete(root);

    if (!execution.IsValid()) {
        return std::nullopt;
    }

    return execution;
}

}  // namespace


// DP-030-r1e helpers: ejecuciones en archivos planos dentro de /care_data
std::string ExecutionFilePath(const RoutineExecutionId& id) {
    char path[96] = {};
    std::snprintf(path, sizeof(path), "%s/daily_execution_%s.json", kCareDataBasePath, id.Str().c_str());
    return path;
}

bool SaveExecutionTextFile(const char* path, const std::string& text) {
    FILE* f = std::fopen(path, "wb");
    if (f == nullptr) return false;
    const size_t written = std::fwrite(text.data(), 1, text.size(), f);
    const int close_result = std::fclose(f);
    return written == text.size() && close_result == 0;
}

bool LoadExecutionTextFile(const char* path, std::string* text, size_t max_size) {
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

bool DeleteExecutionTextFile(const char* path) {
    if (std::remove(path) == 0) return true;
    return errno == ENOENT;
}

bool DeleteExecutionNvsKey(const std::string& nvs_namespace, const char* key) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(nvs_namespace.c_str(), NVS_READWRITE, &handle);
    if (err != ESP_OK) return false;

    err = nvs_erase_key(handle, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(handle);

    nvs_close(handle);
    return err == ESP_OK;
}

std::vector<RoutineExecutionId> ReadExecutionIndexFile() {
    std::string json;
    if (!LoadExecutionTextFile(kExecutionIndexPath, &json, kMaxExecutionIndexSize)) return {};
    return ParseIndex(json);
}

bool WriteExecutionIndexFile(const std::vector<RoutineExecutionId>& ids) {
    const std::string json = SerializeIndex(ids);
    return !json.empty() && SaveExecutionTextFile(kExecutionIndexPath, json);
}

std::vector<RoutineExecutionId> ReadExecutionIndexMigrating(const std::string& nvs_namespace) {
    std::string json;
    if (LoadExecutionTextFile(kExecutionIndexPath, &json, kMaxExecutionIndexSize)) {
        return ParseIndex(json);
    }

    nvs_handle_t handle = 0;
    if (nvs_open(nvs_namespace.c_str(), NVS_READONLY, &handle) != ESP_OK) {
        return {};
    }

    const bool ok = ReadString(handle, kIndexKey, &json) && !json.empty();
    nvs_close(handle);

    if (!ok) return {};

    std::vector<RoutineExecutionId> ids = ParseIndex(json);
    if (WriteExecutionIndexFile(ids)) {
        DeleteExecutionNvsKey(nvs_namespace, kIndexKey);
        ESP_LOGI(kTag, "Migrated execution index from NVS to care_data");
    }

    return ids;
}

bool ReadExecutionSequenceMigrating(const std::string& nvs_namespace, uint32_t* sequence) {
    if (sequence == nullptr) return false;
    *sequence = 0;

    std::string raw;
    if (LoadExecutionTextFile(kExecutionSequencePath, &raw, 32)) {
        char* end = nullptr;
        unsigned long value = std::strtoul(raw.c_str(), &end, 10);
        if (end != raw.c_str()) {
            *sequence = static_cast<uint32_t>(value);
            return true;
        }
    }

    nvs_handle_t handle = 0;
    if (nvs_open(nvs_namespace.c_str(), NVS_READONLY, &handle) == ESP_OK) {
        uint32_t nvs_sequence = 0;
        esp_err_t err = nvs_get_u32(handle, kSequenceKey, &nvs_sequence);
        nvs_close(handle);

        if (err == ESP_OK) {
            *sequence = nvs_sequence;
        }
    }

    char buffer[24] = {};
    std::snprintf(buffer, sizeof(buffer), "%lu", static_cast<unsigned long>(*sequence));
    if (SaveExecutionTextFile(kExecutionSequencePath, buffer)) {
        DeleteExecutionNvsKey(nvs_namespace, kSequenceKey);
        ESP_LOGI(kTag, "Migrated execution sequence from NVS to care_data");
    }

    return true;
}

bool WriteExecutionSequenceFile(const std::string& nvs_namespace, uint32_t sequence) {
    char buffer[24] = {};
    std::snprintf(buffer, sizeof(buffer), "%lu", static_cast<unsigned long>(sequence));
    const bool ok = SaveExecutionTextFile(kExecutionSequencePath, buffer);
    if (ok) DeleteExecutionNvsKey(nvs_namespace, kSequenceKey);
    return ok;
}

bool SaveExecutionFile(const RoutineExecution& execution) {
    const std::string json = SerializeExecution(execution);
    if (json.empty()) return false;
    return SaveExecutionTextFile(ExecutionFilePath(execution.id).c_str(), json);
}

std::optional<RoutineExecution> LoadExecutionFile(const RoutineExecutionId& id) {
    std::string json;
    if (!LoadExecutionTextFile(ExecutionFilePath(id).c_str(), &json, kMaxExecutionRecordSize)) {
        return std::nullopt;
    }
    return DeserializeExecution(json);
}

std::optional<RoutineExecution> LoadExecutionMigrating(const std::string& nvs_namespace, const RoutineExecutionId& id) {
    std::optional<RoutineExecution> execution = LoadExecutionFile(id);
    if (execution.has_value()) return execution;

    const std::string key = ExecutionKey(id);

    nvs_handle_t handle = 0;
    if (nvs_open(nvs_namespace.c_str(), NVS_READONLY, &handle) != ESP_OK) {
        return std::nullopt;
    }

    std::string json;
    const bool ok = ReadString(handle, key.c_str(), &json) && !json.empty();
    nvs_close(handle);

    if (!ok) return std::nullopt;

    execution = DeserializeExecution(json);
    if (execution.has_value()) {
        if (SaveExecutionFile(execution.value())) {
            DeleteExecutionNvsKey(nvs_namespace, key.c_str());
            ESP_LOGI(kTag, "Migrated execution %s from NVS to care_data", id.Str().c_str());
        } else {
            ESP_LOGW(kTag, "Loaded execution %s from NVS, but migration to care_data failed", id.Str().c_str());
        }
    }

    return execution;
}
NvsRoutineExecutionRepository::NvsRoutineExecutionRepository(std::string nvs_namespace)
    : nvs_namespace_(std::move(nvs_namespace)) {}

bool NvsRoutineExecutionRepository::Init() {
    std::vector<RoutineExecutionId> ids = ReadExecutionIndexMigrating(nvs_namespace_);
    WriteExecutionIndexFile(ids);

    uint32_t sequence = 0;
    ReadExecutionSequenceMigrating(nvs_namespace_, &sequence);
    WriteExecutionSequenceFile(nvs_namespace_, sequence);

    ESP_LOGI(kTag, "Execution repository ready");
    return true;
}

RoutineExecutionId NvsRoutineExecutionRepository::GenerateId() {
    uint32_t sequence = 0;
    if (!ReadExecutionSequenceMigrating(nvs_namespace_, &sequence)) {
        return RoutineExecutionId();
    }

    ++sequence;

    if (!WriteExecutionSequenceFile(nvs_namespace_, sequence)) {
        ESP_LOGE(kTag, "Failed to save execution sequence");
        return RoutineExecutionId();
    }

    char buffer[12] = {};
    std::snprintf(buffer, sizeof(buffer), "e%06lu", static_cast<unsigned long>(sequence));
    return RoutineExecutionId(buffer);
}

bool NvsRoutineExecutionRepository::Save(const RoutineExecution& execution) {
    if (!execution.IsValid()) {
        ESP_LOGW(kTag, "Refusing to save invalid execution");
        return false;
    }

    std::vector<RoutineExecutionId> ids = ReadExecutionIndexMigrating(nvs_namespace_);
    if (!ContainsId(ids, execution.id)) {
        ids.push_back(execution.id);
    }

    const bool ok = SaveExecutionFile(execution) && WriteExecutionIndexFile(ids);
    if (!ok) {
        ESP_LOGE(kTag, "Failed to save execution %s", execution.id.Str().c_str());
        return false;
    }

    DeleteExecutionNvsKey(nvs_namespace_, ExecutionKey(execution.id).c_str());
    DeleteExecutionNvsKey(nvs_namespace_, kIndexKey);

    ESP_LOGI(kTag, "Saved execution %s", execution.id.Str().c_str());
    return true;
}

std::optional<RoutineExecution> NvsRoutineExecutionRepository::FindById(const RoutineExecutionId& id) {
    if (id.Empty()) {
        return std::nullopt;
    }

    return LoadExecutionMigrating(nvs_namespace_, id);
}

std::vector<RoutineExecution> NvsRoutineExecutionRepository::List() {
    std::vector<RoutineExecutionId> ids = ReadExecutionIndexMigrating(nvs_namespace_);

    std::vector<RoutineExecution> executions;
    executions.reserve(ids.size());

    for (const RoutineExecutionId& id : ids) {
        std::optional<RoutineExecution> execution = LoadExecutionMigrating(nvs_namespace_, id);
        if (execution.has_value()) {
            executions.push_back(execution.value());
        }
    }

    return executions;
}

std::vector<RoutineExecution> NvsRoutineExecutionRepository::ListByRoutine(const RoutineId& routine_id) {
    if (routine_id.Empty()) {
        return {};
    }

    std::vector<RoutineExecution> all = List();
    std::vector<RoutineExecution> result;
    for (const RoutineExecution& execution : all) {
        if (execution.routine_id == routine_id) {
            result.push_back(execution);
        }
    }
    return result;
}

bool NvsRoutineExecutionRepository::Delete(const RoutineExecutionId& id) {
    if (id.Empty()) {
        return false;
    }

    std::vector<RoutineExecutionId> ids = ReadExecutionIndexMigrating(nvs_namespace_);
    const auto old_size = ids.size();
    ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());

    if (ids.size() == old_size) {
        return false;
    }

    DeleteExecutionTextFile(ExecutionFilePath(id).c_str());
    DeleteExecutionNvsKey(nvs_namespace_, ExecutionKey(id).c_str());

    const bool ok = WriteExecutionIndexFile(ids);
    if (!ok) {
        ESP_LOGE(kTag, "Failed to delete execution %s", id.Str().c_str());
        return false;
    }

    ESP_LOGI(kTag, "Deleted execution %s", id.Str().c_str());
    return true;
}

}  // namespace xiaozhi_care::daily


