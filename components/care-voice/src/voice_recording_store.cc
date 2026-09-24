#include "care_voice/voice_recording_store.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <cJSON.h>
#include <esp_log.h>
#include <esp_spiffs.h>
#include <esp_vfs.h>

namespace xiaozhi_care::voice {
namespace {
constexpr char kTag[] = "CARE_VOICE";
constexpr char kBasePath[] = "/voice";
constexpr char kIndexPath[] = "/voice/index.json";
constexpr char kPartitionLabel[] = "voice";

uint16_t ReadLe16(const uint8_t* p) { return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8); }
uint64_t ReadLe64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}

std::string JsonString(const cJSON* obj, const char* name) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, name);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : std::string();
}

uint32_t JsonU32(const cJSON* obj, const char* name, uint32_t fallback = 0) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, name);
    return cJSON_IsNumber(item) ? static_cast<uint32_t>(item->valuedouble) : fallback;
}

bool JsonBool(const cJSON* obj, const char* name, bool fallback = true) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, name);
    return cJSON_IsBool(item) ? cJSON_IsTrue(item) : fallback;
}
}  // namespace

VoiceRecordingStore& VoiceRecordingStore::GetInstance() {
    static VoiceRecordingStore instance;
    return instance;
}

bool VoiceRecordingStore::Init() {
    if (ready_) return true;
    esp_vfs_spiffs_conf_t conf = {};
    conf.base_path = kBasePath;
    conf.partition_label = kPartitionLabel;
    conf.max_files = 16;
    conf.format_if_mount_failed = true;
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(kTag, "Voice SPIFFS already mounted");
    } else if (err != ESP_OK) {
        ESP_LOGE(kTag, "Unable to mount voice SPIFFS: %s", esp_err_to_name(err));
        return false;
    }
    size_t total = 0;
    size_t used = 0;
    if (esp_spiffs_info(kPartitionLabel, &total, &used) == ESP_OK) {
        ESP_LOGI(kTag, "Voice recordings storage ready total=%u used=%u max_recordings=%u max_file=%u duration_ms=%u",
                 static_cast<unsigned>(total), static_cast<unsigned>(used),
                 static_cast<unsigned>(limits_.max_recordings),
                 static_cast<unsigned>(limits_.max_file_bytes),
                 static_cast<unsigned>(limits_.max_duration_ms));
    }
    ready_ = true;
    return true;
}

std::vector<VoiceRecordingInfo> VoiceRecordingStore::List() {
    std::vector<VoiceRecordingInfo> values;
    if (!Init()) return values;
    LoadIndex(values);
    return values;
}

bool VoiceRecordingStore::Find(const std::string& id, VoiceRecordingInfo& out) {
    auto values = List();
    auto it = std::find_if(values.begin(), values.end(), [&](const auto& v) { return v.id == id; });
    if (it == values.end()) return false;
    out = *it;
    return true;
}

bool VoiceRecordingStore::FindByReminder(const std::string& reminder_id, VoiceRecordingInfo& out) {
    if (reminder_id.empty()) return false;
    auto values = List();
    auto it = std::find_if(values.begin(), values.end(),
                           [&](const auto& v) { return v.reminder_id == reminder_id; });
    if (it == values.end()) return false;
    out = *it;
    return true;
}

bool VoiceRecordingStore::LoadAudio(const std::string& id, std::string& audio_bytes, VoiceRecordingInfo* info) {
    audio_bytes.clear();
    VoiceRecordingInfo meta;
    if (!Find(id, meta) || !meta.enabled) return false;
    FILE* f = std::fopen(meta.file_path.c_str(), "rb");
    if (!f) return false;
    if (std::fseek(f, 0, SEEK_END) != 0) { std::fclose(f); return false; }
    long len = std::ftell(f);
    if (len <= 0 || static_cast<size_t>(len) > limits_.max_file_bytes) { std::fclose(f); return false; }
    std::rewind(f);
    audio_bytes.resize(static_cast<size_t>(len));
    size_t got = std::fread(audio_bytes.data(), 1, audio_bytes.size(), f);
    std::fclose(f);
    if (got != audio_bytes.size()) { audio_bytes.clear(); return false; }
    if (info) *info = meta;
    return true;
}

VoiceRecordingValidation VoiceRecordingStore::ValidateOggOpus(const uint8_t* data, size_t size) const {
    VoiceRecordingValidation v;
    v.size_bytes = size;
    if (data == nullptr || size < 64) { v.error = "Archivo vacío o demasiado chico"; return v; }
    if (size > limits_.max_file_bytes) { v.error = "El archivo supera 32 KB"; return v; }

    bool opus_head = false;
    uint16_t pre_skip = 0;
    uint64_t last_granule = 0;
    size_t pos = 0;
    while (pos + 27 <= size) {
        if (std::memcmp(data + pos, "OggS", 4) != 0) { ++pos; continue; }
        const uint8_t page_segments = data[pos + 26];
        if (pos + 27 + page_segments > size) { v.error = "Cabecera OGG incompleta"; return v; }
        size_t payload_size = 0;
        for (uint8_t i = 0; i < page_segments; ++i) payload_size += data[pos + 27 + i];
        const size_t payload_pos = pos + 27 + page_segments;
        if (payload_pos + payload_size > size) { v.error = "Página OGG incompleta"; return v; }
        if (payload_size >= 19 && std::memcmp(data + payload_pos, "OpusHead", 8) == 0) {
            opus_head = true;
            pre_skip = ReadLe16(data + payload_pos + 10);
        }
        uint64_t granule = ReadLe64(data + pos + 6);
        if (granule != UINT64_MAX && granule > last_granule) last_granule = granule;
        pos = payload_pos + payload_size;
    }

    if (!opus_head) { v.error = "El archivo debe ser OGG / Opus"; return v; }
    if (last_granule <= pre_skip) { v.error = "No pude calcular la duración"; return v; }
    const uint64_t samples = last_granule - pre_skip;
    v.duration_ms = static_cast<uint32_t>((samples * 1000ULL + 24000ULL) / 48000ULL);
    if (v.duration_ms > limits_.max_duration_ms) {
        v.error = "La grabación supera los 10 segundos";
        return v;
    }
    v.ok = true;
    return v;
}

bool VoiceRecordingStore::Save(const std::string& label,
                               const std::string& text,
                               const std::string& reminder_id,
                               const uint8_t* data,
                               size_t size,
                               VoiceRecordingInfo& saved,
                               std::string& error) {
    saved = {};
    error.clear();
    if (!Init()) { error = "No se pudo inicializar el almacenamiento de grabaciones"; return false; }
    // DP040B_FASE1_GROUP_TARGETS
    // reminder_id es el nombre legacy del destino de audio.
    if (label.empty() || label.size() > 48 || text.size() > 120 ||
        reminder_id.empty() || reminder_id.size() > 96) {
        error = "INVALID_VOICE_RECORDING";
        return false;
    }
    auto validation = ValidateOggOpus(data, size);
    if (!validation.ok) { error = validation.error; return false; }
    auto values = List();
    if (values.size() >= limits_.max_recordings) { error = "VOICE_LIMIT_REACHED"; return false; }
    const auto already_assigned = std::find_if(
        values.begin(), values.end(),
        [&](const auto& v) { return v.reminder_id == reminder_id; });
    if (already_assigned != values.end()) {
        error = "REMINDER_ALREADY_HAS_AUDIO";
        return false;
    }
    const std::string id = NextId(values);
    const std::string path = std::string(kBasePath) + "/" + EscapeFileId(id) + ".ogg";
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { error = "No se pudo crear el archivo de audio"; return false; }
    const size_t written = std::fwrite(data, 1, size, f);
    std::fclose(f);
    if (written != size) { std::remove(path.c_str()); error = "No se pudo guardar todo el archivo"; return false; }
    saved.id = id;
    saved.label = label;
    saved.text = text;
    saved.file_path = path;
    saved.reminder_id = reminder_id;
    saved.duration_ms = validation.duration_ms;
    saved.size_bytes = size;
    saved.enabled = true;
    values.push_back(saved);
    if (!SaveIndex(values)) { std::remove(path.c_str()); error = "No se pudo guardar el índice"; return false; }
    ESP_LOGI(kTag, "Voice recording saved id=%s label=%s duration_ms=%u size=%u",
             saved.id.c_str(), saved.label.c_str(), static_cast<unsigned>(saved.duration_ms),
             static_cast<unsigned>(saved.size_bytes));
    return true;
}

bool VoiceRecordingStore::AssignToReminder(const std::string& id,
                                               const std::string& reminder_id,
                                               std::string& error) {
    error.clear();
    if (!Init()) { error = "VOICE_STORAGE_NOT_READY"; return false; }
    auto values = List();
    auto current = std::find_if(
        values.begin(), values.end(),
        [&](const auto& v) { return v.id == id; });
    if (current == values.end()) { error = "NOT_FOUND"; return false; }

    if (!reminder_id.empty()) {
        auto conflict = std::find_if(
            values.begin(), values.end(),
            [&](const auto& v) { return v.id != id && v.reminder_id == reminder_id; });
        if (conflict != values.end()) {
            error = "REMINDER_ALREADY_HAS_AUDIO";
            return false;
        }
    }

    current->reminder_id = reminder_id;
    if (!SaveIndex(values)) { error = "VOICE_INDEX_WRITE_FAILED"; return false; }
    ESP_LOGI(kTag, "Voice assignment updated id=%s reminder=%s",
             id.c_str(), reminder_id.c_str());
    return true;
}

bool VoiceRecordingStore::UnassignReminder(const std::string& reminder_id) {
    if (reminder_id.empty() || !Init()) return false;
    auto values = List();
    bool changed = false;
    for (auto& value : values) {
        if (value.reminder_id == reminder_id) {
            value.reminder_id.clear();
            changed = true;
        }
    }
    if (!changed) return true;
    if (!SaveIndex(values)) return false;
    ESP_LOGI(kTag, "Voice recording unassigned from reminder=%s", reminder_id.c_str());
    return true;
}

bool VoiceRecordingStore::Delete(const std::string& id, std::string* error) {
    if (error) error->clear();
    if (!Init()) { if (error) *error = "VOICE_STORAGE_NOT_READY"; return false; }
    auto values = List();
    auto it = std::find_if(values.begin(), values.end(), [&](const auto& v) { return v.id == id; });
    if (it == values.end()) { if (error) *error = "NOT_FOUND"; return false; }
    if (!it->reminder_id.empty()) {
        if (error) *error = "AUDIO_ASSIGNED_TO_REMINDER";
        return false;
    }
    const std::string file_path = it->file_path;
    values.erase(it);
    if (!SaveIndex(values)) { if (error) *error = "VOICE_INDEX_WRITE_FAILED"; return false; }
    std::remove(file_path.c_str());
    ESP_LOGI(kTag, "Voice recording deleted id=%s", id.c_str());
    return true;
}

bool VoiceRecordingStore::LoadIndex(std::vector<VoiceRecordingInfo>& values) {
    values.clear();
    FILE* f = std::fopen(kIndexPath, "rb");
    if (!f) return true;
    std::fseek(f, 0, SEEK_END);
    long len = std::ftell(f);
    std::rewind(f);
    if (len <= 0 || len > 8192) { std::fclose(f); return false; }
    std::string raw(static_cast<size_t>(len), '\0');
    std::fread(raw.data(), 1, raw.size(), f);
    std::fclose(f);
    cJSON* root = cJSON_ParseWithLength(raw.data(), raw.size());
    if (!cJSON_IsArray(root)) { if (root) cJSON_Delete(root); return false; }
    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, root) {
        if (!cJSON_IsObject(item)) continue;
        VoiceRecordingInfo v;
        v.id = JsonString(item, "id");
        v.label = JsonString(item, "label");
        v.text = JsonString(item, "text");
        v.file_path = JsonString(item, "file_path");
        v.reminder_id = JsonString(item, "reminder_id");
        v.duration_ms = JsonU32(item, "duration_ms");
        v.size_bytes = JsonU32(item, "size_bytes");
        v.enabled = JsonBool(item, "enabled", true);
        if (!v.id.empty() && !v.file_path.empty()) values.push_back(std::move(v));
    }
    cJSON_Delete(root);
    return true;
}

bool VoiceRecordingStore::SaveIndex(const std::vector<VoiceRecordingInfo>& values) {
    cJSON* root = cJSON_CreateArray();
    if (!root) return false;
    for (const auto& v : values) {
        cJSON* item = cJSON_CreateObject();
        if (!item) { cJSON_Delete(root); return false; }
        cJSON_AddStringToObject(item, "id", v.id.c_str());
        cJSON_AddStringToObject(item, "label", v.label.c_str());
        cJSON_AddStringToObject(item, "text", v.text.c_str());
        cJSON_AddStringToObject(item, "file_path", v.file_path.c_str());
        cJSON_AddStringToObject(item, "reminder_id", v.reminder_id.c_str());
        cJSON_AddNumberToObject(item, "duration_ms", v.duration_ms);
        cJSON_AddNumberToObject(item, "size_bytes", static_cast<double>(v.size_bytes));
        cJSON_AddBoolToObject(item, "enabled", v.enabled);
        cJSON_AddItemToArray(root, item);
    }
    char* raw = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!raw) return false;
    FILE* f = std::fopen(kIndexPath, "wb");
    if (!f) { cJSON_free(raw); return false; }
    std::fwrite(raw, 1, std::strlen(raw), f);
    std::fclose(f);
    cJSON_free(raw);
    return true;
}

std::string VoiceRecordingStore::NextId(const std::vector<VoiceRecordingInfo>& existing) const {
    for (int i = 1; i <= 999; ++i) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "vr%03d", i);
        bool used = std::any_of(existing.begin(), existing.end(), [&](const auto& v) { return v.id == buf; });
        if (!used) return buf;
    }
    return "vr999";
}

std::string VoiceRecordingStore::EscapeFileId(const std::string& id) {
    std::string out;
    for (char c : id) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') out += c;
    }
    return out.empty() ? "voice" : out;
}

}  // namespace xiaozhi_care::voice
