#include "care_family/nvs_family_relationship_repository.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <cJSON.h>
#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>

namespace xiaozhi_care::family {
namespace {

constexpr char kTag[] = "CARE_FAMILY";
constexpr char kNamespace[] = "care_family";
constexpr char kNextIdKey[] = "next_id";
constexpr char kIndexKey[] = "index";
constexpr char kCareDataBasePath[] = "/care_data";
constexpr char kFamilyIndexPath[] = "/care_data/family_index.json";
constexpr char kFamilyNextIdPath[] = "/care_data/family_next_id.txt";
constexpr size_t kMaxRelationships = 64;
constexpr size_t kMaxFamilyRecordSize = 2048;
constexpr size_t kMaxFamilyIndexSize = 4096;

bool Open(nvs_handle_t& handle, nvs_open_mode_t mode) {
    return nvs_open(kNamespace, mode, &handle) == ESP_OK;
}

std::string FamilyRecordPath(const std::string& id) {
    char path[96] = {};
    std::snprintf(path, sizeof(path), "%s/family_%s.json", kCareDataBasePath, id.c_str());
    return path;
}

bool SaveFile(const char* path, const std::string& text) {
    FILE* f = std::fopen(path, "wb");
    if (f == nullptr) return false;
    const size_t written = std::fwrite(text.data(), 1, text.size(), f);
    const int close_result = std::fclose(f);
    return written == text.size() && close_result == 0;
}

bool LoadFile(const char* path, std::string& text, size_t max_size) {
    text.clear();
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
    text.resize(static_cast<size_t>(len));
    const size_t got = text.empty() ? 0 : std::fread(text.data(), 1, text.size(), f);
    const int close_result = std::fclose(f);

    if ((!text.empty() && got != text.size()) || close_result != 0) {
        text.clear();
        return false;
    }

    return true;
}

bool DeleteFile(const char* path) {
    if (std::remove(path) == 0) return true;
    return errno == ENOENT;
}

bool DeleteNvsKey(const char* key) {
    nvs_handle_t handle = 0;
    if (!Open(handle, NVS_READWRITE)) return false;
    esp_err_t err = nvs_erase_key(handle, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}

std::string ReadStringFromNvs(nvs_handle_t handle, const char* key) {
    size_t required = 0;
    esp_err_t err = nvs_get_str(handle, key, nullptr, &required);
    if (err != ESP_OK || required == 0) return {};
    std::string value(required, '\0');
    err = nvs_get_str(handle, key, value.data(), &required);
    if (err != ESP_OK) return {};
    if (!value.empty() && value.back() == '\0') value.pop_back();
    return value;
}

std::vector<std::string> ParseIndexJson(const std::string& raw) {
    std::vector<std::string> ids;
    if (raw.empty()) return ids;

    cJSON* root = cJSON_Parse(raw.c_str());
    if (!cJSON_IsArray(root)) {
        if (root) cJSON_Delete(root);
        return ids;
    }

    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, root) {
        if (cJSON_IsString(item) && item->valuestring != nullptr) {
            ids.emplace_back(item->valuestring);
        }
    }

    cJSON_Delete(root);
    return ids;
}

std::string BuildIndexJson(const std::vector<std::string>& ids) {
    cJSON* root = cJSON_CreateArray();
    if (root == nullptr) return {};
    for (const auto& id : ids) {
        cJSON_AddItemToArray(root, cJSON_CreateString(id.c_str()));
    }
    char* raw = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (raw == nullptr) return {};
    std::string out(raw);
    cJSON_free(raw);
    return out;
}

std::vector<std::string> ReadIndexFromNvs() {
    nvs_handle_t handle = 0;
    if (!Open(handle, NVS_READONLY)) return {};
    const std::string raw = ReadStringFromNvs(handle, kIndexKey);
    nvs_close(handle);
    return ParseIndexJson(raw);
}

std::vector<std::string> ReadIndex() {
    std::string raw;
    if (LoadFile(kFamilyIndexPath, raw, kMaxFamilyIndexSize)) {
        return ParseIndexJson(raw);
    }

    std::vector<std::string> ids = ReadIndexFromNvs();
    if (!ids.empty()) {
        const std::string migrated = BuildIndexJson(ids);
        if (!migrated.empty() && SaveFile(kFamilyIndexPath, migrated)) {
            DeleteNvsKey(kIndexKey);
            ESP_LOGI(kTag, "Migrated family index from NVS to care_data");
        }
    }

    return ids;
}

bool WriteIndex(const std::vector<std::string>& ids) {
    const std::string raw = BuildIndexJson(ids);
    return !raw.empty() && SaveFile(kFamilyIndexPath, raw);
}

bool ReadNextId(uint32_t& next_id) {
    next_id = 1;

    std::string raw;
    if (LoadFile(kFamilyNextIdPath, raw, 32)) {
        char* end = nullptr;
        unsigned long value = std::strtoul(raw.c_str(), &end, 10);
        if (end != raw.c_str() && value > 0) {
            next_id = static_cast<uint32_t>(value);
            return true;
        }
    }

    nvs_handle_t handle = 0;
    if (Open(handle, NVS_READONLY)) {
        uint32_t nvs_next = 1;
        esp_err_t err = nvs_get_u32(handle, kNextIdKey, &nvs_next);
        nvs_close(handle);
        if (err == ESP_OK && nvs_next > 0) {
            next_id = nvs_next;
            char buffer[24] = {};
            std::snprintf(buffer, sizeof(buffer), "%lu", static_cast<unsigned long>(next_id));
            if (SaveFile(kFamilyNextIdPath, buffer)) {
                DeleteNvsKey(kNextIdKey);
                ESP_LOGI(kTag, "Migrated family next_id from NVS to care_data");
            }
            return true;
        }
    }

    return true;
}

bool WriteNextId(uint32_t next_id) {
    char buffer[24] = {};
    std::snprintf(buffer, sizeof(buffer), "%lu", static_cast<unsigned long>(next_id));
    const bool ok = SaveFile(kFamilyNextIdPath, buffer);
    if (ok) DeleteNvsKey(kNextIdKey);
    return ok;
}

std::string ToJson(const FamilyRelationship& r) {
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) return {};
    cJSON_AddStringToObject(root, "id", r.id.value.c_str());
    cJSON_AddStringToObject(root, "from_person_id", r.from_person_id.c_str());
    cJSON_AddStringToObject(root, "to_person_id", r.to_person_id.c_str());
    cJSON_AddStringToObject(root, "type", r.type.c_str());
    cJSON_AddStringToObject(root, "label", r.label.c_str());
    cJSON_AddStringToObject(root, "notes", r.notes.c_str());
    cJSON_AddBoolToObject(root, "enabled", r.enabled);
    char* raw = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (raw == nullptr) return {};
    std::string out(raw);
    cJSON_free(raw);
    return out;
}

std::string JsonString(const cJSON* root, const char* name) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, name);
    return cJSON_IsString(item) && item->valuestring != nullptr ? item->valuestring : std::string();
}

std::optional<FamilyRelationship> FromJson(const std::string& raw) {
    cJSON* root = cJSON_Parse(raw.c_str());
    if (!cJSON_IsObject(root)) {
        if (root) cJSON_Delete(root);
        return std::nullopt;
    }

    FamilyRelationship r;
    r.id = RelationshipId(JsonString(root, "id"));
    r.from_person_id = JsonString(root, "from_person_id");
    r.to_person_id = JsonString(root, "to_person_id");
    r.type = JsonString(root, "type");
    r.label = JsonString(root, "label");
    r.notes = JsonString(root, "notes");
    const cJSON* enabled = cJSON_GetObjectItemCaseSensitive(root, "enabled");
    r.enabled = !cJSON_IsBool(enabled) || cJSON_IsTrue(enabled);
    cJSON_Delete(root);

    if (r.id.Empty() || r.from_person_id.empty() || r.to_person_id.empty()) return std::nullopt;
    return r;
}

bool IsValidKey(const std::string& key) {
    return key.size() > 1 && key.size() < 16;
}

bool SaveRelationshipFile(const FamilyRelationship& relationship) {
    const std::string raw = ToJson(relationship);
    if (raw.empty()) return false;
    return SaveFile(FamilyRecordPath(relationship.id.value).c_str(), raw);
}

std::optional<FamilyRelationship> LoadRelationshipFile(const std::string& id) {
    std::string raw;
    if (!LoadFile(FamilyRecordPath(id).c_str(), raw, kMaxFamilyRecordSize)) {
        return std::nullopt;
    }
    return FromJson(raw);
}

std::optional<FamilyRelationship> LoadRelationshipFromNvs(const std::string& id) {
    nvs_handle_t handle = 0;
    if (!Open(handle, NVS_READONLY)) return std::nullopt;
    std::string raw = ReadStringFromNvs(handle, id.c_str());
    nvs_close(handle);

    auto rel = FromJson(raw);
    if (rel.has_value()) {
        if (SaveRelationshipFile(rel.value())) {
            DeleteNvsKey(id.c_str());
            ESP_LOGI(kTag, "Migrated family relationship %s from NVS to care_data", id.c_str());
        } else {
            ESP_LOGW(kTag, "Loaded family relationship %s from NVS, but migration failed", id.c_str());
        }
    }

    return rel;
}

}  // namespace

bool NvsFamilyRelationshipRepository::Init() {
    uint32_t next_id = 1;
    ReadNextId(next_id);

    std::vector<std::string> ids = ReadIndex();
    WriteIndex(ids);
    WriteNextId(next_id);

    ready_ = true;
    ESP_LOGI(kTag, "Family relationship repository ready");
    return true;
}

RelationshipId NvsFamilyRelationshipRepository::GenerateId() {
    if (!ready_ && !Init()) return RelationshipId();

    uint32_t next_id = 1;
    if (!ReadNextId(next_id)) return RelationshipId();

    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "f%06lu", static_cast<unsigned long>(next_id));

    if (!WriteNextId(next_id + 1)) return RelationshipId();
    return RelationshipId(buffer);
}

bool NvsFamilyRelationshipRepository::Save(const FamilyRelationship& relationship) {
    if (!ready_ && !Init()) return false;
    if (relationship.id.Empty() || relationship.from_person_id.empty() || relationship.to_person_id.empty()) return false;
    if (!IsValidKey(relationship.id.value)) return false;

    std::vector<std::string> ids = ReadIndex();
    if (std::find(ids.begin(), ids.end(), relationship.id.value) == ids.end()) {
        if (ids.size() >= kMaxRelationships) return false;
        ids.push_back(relationship.id.value);
    }

    const bool ok = SaveRelationshipFile(relationship) && WriteIndex(ids);
    if (ok) {
        DeleteNvsKey(relationship.id.value.c_str());
        DeleteNvsKey(kIndexKey);
        ESP_LOGI(kTag, "Saved family relationship %s", relationship.id.value.c_str());
    }

    return ok;
}

std::optional<FamilyRelationship> NvsFamilyRelationshipRepository::FindById(const RelationshipId& id) {
    if (!ready_ && !Init()) return std::nullopt;
    if (id.Empty() || !IsValidKey(id.value)) return std::nullopt;

    auto rel = LoadRelationshipFile(id.value);
    if (rel.has_value()) return rel;

    return LoadRelationshipFromNvs(id.value);
}

std::vector<FamilyRelationship> NvsFamilyRelationshipRepository::List() {
    std::vector<FamilyRelationship> out;
    if (!ready_ && !Init()) return out;

    std::vector<std::string> ids = ReadIndex();
    for (const auto& id : ids) {
        auto rel = LoadRelationshipFile(id);
        if (!rel.has_value()) rel = LoadRelationshipFromNvs(id);
        if (rel.has_value()) out.push_back(rel.value());
    }

    return out;
}

bool NvsFamilyRelationshipRepository::Delete(const RelationshipId& id) {
    if (!ready_ && !Init()) return false;
    if (id.Empty() || !IsValidKey(id.value)) return false;

    std::vector<std::string> ids = ReadIndex();
    const auto old_size = ids.size();
    ids.erase(std::remove(ids.begin(), ids.end(), id.value), ids.end());
    if (ids.size() == old_size) return false;

    DeleteFile(FamilyRecordPath(id.value).c_str());
    DeleteNvsKey(id.value.c_str());

    return WriteIndex(ids);
}

NvsFamilyRelationshipRepository& GetFamilyRelationshipRepository() {
    static NvsFamilyRelationshipRepository repo;
    return repo;
}

}  // namespace xiaozhi_care::family
