#include "care_daily/care_data_day_activity_memory_repository.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>

#include "cJSON.h"
#include "esp_log.h"

namespace xiaozhi_care::daily {
namespace {

constexpr char kTag[] = "CARE_DAY_MEMORY";
constexpr char kDayMemoryPath[] = "/care_data/day_activity_memory.json";
constexpr int kSchemaVersion = 1;

constexpr size_t kMaxDayMemoryRecords = 16;
constexpr size_t kMaxDayMemoryFileSize = 32768;

bool IsMemoryId(const std::string& id) {
    if (id.size() != 7 || id[0] != 'a') {
        return false;
    }

    for (size_t i = 1; i < id.size(); ++i) {
        if (id[i] < '0' || id[i] > '9') {
            return false;
        }
    }

    return true;
}

uint32_t MemoryIdNumber(const std::string& id) {
    if (!IsMemoryId(id)) {
        return 0;
    }

    return static_cast<uint32_t>(
        std::strtoul(id.c_str() + 1, nullptr, 10));
}

bool SaveTextFile(const char* path, const std::string& text) {
    if (path == nullptr || path[0] == '\0') {
        return false;
    }

    FILE* f = std::fopen(path, "wb");
    if (f == nullptr) {
        return false;
    }

    const size_t written =
        std::fwrite(text.data(), 1, text.size(), f);

    const int close_result = std::fclose(f);

    return written == text.size() &&
           close_result == 0;
}

bool LoadTextFile(const char* path,
                  std::string* text,
                  size_t max_size) {
    if (path == nullptr || text == nullptr) {
        return false;
    }

    text->clear();

    FILE* f = std::fopen(path, "rb");
    if (f == nullptr) {
        return false;
    }

    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        return false;
    }

    const long len = std::ftell(f);
    if (len < 0 ||
        static_cast<size_t>(len) > max_size) {
        std::fclose(f);
        return false;
    }

    std::rewind(f);

    text->resize(static_cast<size_t>(len));

    const size_t got = text->empty()
        ? 0
        : std::fread(text->data(), 1, text->size(), f);

    const int close_result = std::fclose(f);

    if ((!text->empty() && got != text->size()) ||
        close_result != 0) {
        text->clear();
        return false;
    }

    return true;
}

void AddStringIfNotEmpty(cJSON* object,
                         const char* key,
                         const std::string& value) {
    if (object != nullptr &&
        key != nullptr &&
        !value.empty()) {
        cJSON_AddStringToObject(
            object,
            key,
            value.c_str());
    }
}

cJSON* SerializeFact(const DayActivityFact& fact) {
    cJSON* object = cJSON_CreateObject();
    if (object == nullptr) {
        return nullptr;
    }

    cJSON_AddStringToObject(
        object,
        "value",
        fact.value.c_str());

    cJSON_AddStringToObject(
        object,
        "source",
        DayMemoryFactSourceName(fact.source));

    return object;
}

bool ParseFactSource(const char* text,
                     DayMemoryFactSource* source) {
    if (text == nullptr || source == nullptr) {
        return false;
    }

    if (std::strcmp(text, "user_recalled") == 0) {
        *source = DayMemoryFactSource::UserRecalled;
        return true;
    }

    if (std::strcmp(text, "user_confirmed") == 0) {
        *source = DayMemoryFactSource::UserConfirmed;
        return true;
    }

    if (std::strcmp(text, "care_suggested") == 0) {
        *source = DayMemoryFactSource::CareSuggested;
        return true;
    }

    return false;
}

bool DeserializeFact(const cJSON* object,
                     DayActivityFact* fact) {
    if (!cJSON_IsObject(object) || fact == nullptr) {
        return false;
    }

    const cJSON* value =
        cJSON_GetObjectItemCaseSensitive(object, "value");

    const cJSON* source =
        cJSON_GetObjectItemCaseSensitive(object, "source");

    if (!cJSON_IsString(value) ||
        value->valuestring == nullptr ||
        value->valuestring[0] == '\0') {
        return false;
    }

    DayMemoryFactSource parsed_source =
        DayMemoryFactSource::UserRecalled;

    if (cJSON_IsString(source) &&
        source->valuestring != nullptr) {
        if (!ParseFactSource(
                source->valuestring,
                &parsed_source)) {
            return false;
        }
    }

    fact->value = value->valuestring;
    fact->source = parsed_source;

    return true;
}

cJSON* SerializeFactArray(
    const std::vector<DayActivityFact>& facts) {
    cJSON* array = cJSON_CreateArray();
    if (array == nullptr) {
        return nullptr;
    }

    for (const DayActivityFact& fact : facts) {
        if (!fact.IsValid()) {
            continue;
        }

        cJSON* item = SerializeFact(fact);
        if (item == nullptr) {
            cJSON_Delete(array);
            return nullptr;
        }

        cJSON_AddItemToArray(array, item);
    }

    return array;
}

bool DeserializeFactArray(
    const cJSON* array,
    std::vector<DayActivityFact>* facts) {
    if (facts == nullptr) {
        return false;
    }

    facts->clear();

    if (array == nullptr) {
        return true;
    }

    if (!cJSON_IsArray(array)) {
        return false;
    }

    const cJSON* item = nullptr;
    cJSON_ArrayForEach(item, array) {
        DayActivityFact fact;
        if (!DeserializeFact(item, &fact)) {
            return false;
        }

        facts->push_back(std::move(fact));
    }

    return true;
}

bool ParseActivityState(
    const char* text,
    CompanionActivityState* state) {
    if (text == nullptr || state == nullptr) {
        return false;
    }

    if (std::strcmp(text, "discover") == 0) {
        *state = CompanionActivityState::Discover;
    } else if (std::strcmp(text, "recall") == 0) {
        *state = CompanionActivityState::Recall;
    } else if (std::strcmp(text, "check_resources") == 0) {
        *state = CompanionActivityState::CheckResources;
    } else if (std::strcmp(text, "organize") == 0) {
        *state = CompanionActivityState::Organize;
    } else if (std::strcmp(text, "doing") == 0) {
        *state = CompanionActivityState::Doing;
    } else if (std::strcmp(text, "deferred") == 0) {
        *state = CompanionActivityState::Deferred;
    } else if (std::strcmp(text, "completed") == 0) {
        *state = CompanionActivityState::Completed;
    } else if (std::strcmp(text, "cancelled") == 0) {
        *state = CompanionActivityState::Cancelled;
    } else if (std::strcmp(text, "unknown") == 0) {
        *state = CompanionActivityState::Unknown;
    } else {
        return false;
    }

    return true;
}

bool ParseHelpLevel(const char* text,
                    CompanionHelpLevel* level) {
    if (text == nullptr || level == nullptr) {
        return false;
    }

    if (std::strcmp(text, "independent") == 0) {
        *level = CompanionHelpLevel::Independent;
    } else if (std::strcmp(text, "open_question") == 0) {
        *level = CompanionHelpLevel::OpenQuestion;
    } else if (std::strcmp(text, "hint") == 0) {
        *level = CompanionHelpLevel::Hint;
    } else if (std::strcmp(text, "concrete_help") == 0) {
        *level = CompanionHelpLevel::ConcreteHelp;
    } else {
        return false;
    }

    return true;
}

cJSON* SerializeMemory(
    const DayActivityMemory& memory) {
    cJSON* object = cJSON_CreateObject();
    if (object == nullptr) {
        return nullptr;
    }

    cJSON_AddStringToObject(
        object,
        "memory_id",
        memory.memory_id.c_str());

    cJSON_AddStringToObject(
        object,
        "iso_date",
        memory.iso_date.c_str());

    cJSON_AddStringToObject(
        object,
        "activity",
        memory.activity.c_str());

    if (!memory.routine_id.Empty()) {
        cJSON_AddStringToObject(
            object,
            "routine_id",
            memory.routine_id.Str().c_str());
    }

    cJSON_AddStringToObject(
        object,
        "state",
        CompanionActivityStateName(memory.state));

    cJSON_AddStringToObject(
        object,
        "help_level",
        CompanionHelpLevelName(memory.help_level));

    if (memory.plan.IsValid()) {
        cJSON* plan = SerializeFact(memory.plan);
        if (plan == nullptr) {
            cJSON_Delete(object);
            return nullptr;
        }
        cJSON_AddItemToObject(object, "plan", plan);
    }

    if (memory.current_step.IsValid()) {
        cJSON* current_step =
            SerializeFact(memory.current_step);

        if (current_step == nullptr) {
            cJSON_Delete(object);
            return nullptr;
        }

        cJSON_AddItemToObject(
            object,
            "current_step",
            current_step);
    }

    cJSON* recalled =
        SerializeFactArray(memory.recalled_steps);

    cJSON* available =
        SerializeFactArray(memory.available_items);

    cJSON* missing =
        SerializeFactArray(memory.missing_items);

    if (recalled == nullptr ||
        available == nullptr ||
        missing == nullptr) {
        cJSON_Delete(recalled);
        cJSON_Delete(available);
        cJSON_Delete(missing);
        cJSON_Delete(object);
        return nullptr;
    }

    cJSON_AddItemToObject(
        object,
        "recalled_steps",
        recalled);

    cJSON_AddItemToObject(
        object,
        "available_items",
        available);

    cJSON_AddItemToObject(
        object,
        "missing_items",
        missing);

    cJSON_AddBoolToObject(
        object,
        "reminder_created",
        memory.reminder_created);

    AddStringIfNotEmpty(
        object,
        "reminder_text",
        memory.reminder_text);

    cJSON_AddNumberToObject(
        object,
        "last_interaction_unix",
        static_cast<double>(
            memory.last_interaction_unix));

    cJSON_AddNumberToObject(
        object,
        "defer_until_unix",
        static_cast<double>(
            memory.defer_until_unix));

    return object;
}

std::optional<DayActivityMemory> DeserializeMemory(
    const cJSON* object) {
    if (!cJSON_IsObject(object)) {
        return std::nullopt;
    }

    const cJSON* memory_id =
        cJSON_GetObjectItemCaseSensitive(
            object, "memory_id");

    const cJSON* iso_date =
        cJSON_GetObjectItemCaseSensitive(
            object, "iso_date");

    const cJSON* activity =
        cJSON_GetObjectItemCaseSensitive(
            object, "activity");

    const cJSON* state =
        cJSON_GetObjectItemCaseSensitive(
            object, "state");

    const cJSON* help_level =
        cJSON_GetObjectItemCaseSensitive(
            object, "help_level");

    if (!cJSON_IsString(memory_id) ||
        memory_id->valuestring == nullptr ||
        !cJSON_IsString(iso_date) ||
        iso_date->valuestring == nullptr ||
        !cJSON_IsString(activity) ||
        activity->valuestring == nullptr ||
        !cJSON_IsString(state) ||
        state->valuestring == nullptr ||
        !cJSON_IsString(help_level) ||
        help_level->valuestring == nullptr) {
        return std::nullopt;
    }

    DayActivityMemory memory;
    memory.memory_id = memory_id->valuestring;
    memory.iso_date = iso_date->valuestring;
    memory.activity = activity->valuestring;

    if (!IsMemoryId(memory.memory_id)) {
        return std::nullopt;
    }

    const cJSON* routine_id =
        cJSON_GetObjectItemCaseSensitive(
            object, "routine_id");

    if (cJSON_IsString(routine_id) &&
        routine_id->valuestring != nullptr &&
        routine_id->valuestring[0] != '\0') {
        memory.routine_id =
            RoutineId(routine_id->valuestring);
    }

    if (!ParseActivityState(
            state->valuestring,
            &memory.state)) {
        return std::nullopt;
    }

    if (!ParseHelpLevel(
            help_level->valuestring,
            &memory.help_level)) {
        return std::nullopt;
    }

    const cJSON* plan =
        cJSON_GetObjectItemCaseSensitive(
            object, "plan");

    if (plan != nullptr &&
        !DeserializeFact(plan, &memory.plan)) {
        return std::nullopt;
    }

    const cJSON* current_step =
        cJSON_GetObjectItemCaseSensitive(
            object, "current_step");

    if (current_step != nullptr &&
        !DeserializeFact(
            current_step,
            &memory.current_step)) {
        return std::nullopt;
    }

    if (!DeserializeFactArray(
            cJSON_GetObjectItemCaseSensitive(
                object, "recalled_steps"),
            &memory.recalled_steps)) {
        return std::nullopt;
    }

    if (!DeserializeFactArray(
            cJSON_GetObjectItemCaseSensitive(
                object, "available_items"),
            &memory.available_items)) {
        return std::nullopt;
    }

    if (!DeserializeFactArray(
            cJSON_GetObjectItemCaseSensitive(
                object, "missing_items"),
            &memory.missing_items)) {
        return std::nullopt;
    }

    const cJSON* reminder_created =
        cJSON_GetObjectItemCaseSensitive(
            object, "reminder_created");

    if (cJSON_IsBool(reminder_created)) {
        memory.reminder_created =
            cJSON_IsTrue(reminder_created);
    }

    const cJSON* reminder_text =
        cJSON_GetObjectItemCaseSensitive(
            object, "reminder_text");

    if (cJSON_IsString(reminder_text) &&
        reminder_text->valuestring != nullptr) {
        memory.reminder_text =
            reminder_text->valuestring;
    }

    const cJSON* last_interaction =
        cJSON_GetObjectItemCaseSensitive(
            object, "last_interaction_unix");

    if (cJSON_IsNumber(last_interaction)) {
        memory.last_interaction_unix =
            static_cast<int64_t>(
                last_interaction->valuedouble);
    }

    const cJSON* defer_until =
        cJSON_GetObjectItemCaseSensitive(
            object, "defer_until_unix");

    if (cJSON_IsNumber(defer_until)) {
        memory.defer_until_unix =
            static_cast<int64_t>(
                defer_until->valuedouble);
    }

    if (!memory.IsValid()) {
        return std::nullopt;
    }

    return memory;
}

bool SaveAll(
    const std::vector<DayActivityMemory>& memories) {
    if (memories.size() > kMaxDayMemoryRecords) {
        return false;
    }

    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) {
        return false;
    }

    cJSON_AddNumberToObject(
        root,
        "schema",
        kSchemaVersion);

    cJSON* array =
        cJSON_AddArrayToObject(root, "memories");

    if (array == nullptr) {
        cJSON_Delete(root);
        return false;
    }

    for (const DayActivityMemory& memory : memories) {
        cJSON* item = SerializeMemory(memory);
        if (item == nullptr) {
            cJSON_Delete(root);
            return false;
        }

        cJSON_AddItemToArray(array, item);
    }

    char* raw = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (raw == nullptr) {
        return false;
    }

    std::string json(raw);
    cJSON_free(raw);

    if (json.size() > kMaxDayMemoryFileSize) {
        return false;
    }

    return SaveTextFile(
        kDayMemoryPath,
        json);
}

bool LoadAll(
    std::vector<DayActivityMemory>* memories) {
    if (memories == nullptr) {
        return false;
    }

    memories->clear();

    std::string json;

    if (!LoadTextFile(
            kDayMemoryPath,
            &json,
            kMaxDayMemoryFileSize)) {
        return false;
    }

    cJSON* root = cJSON_Parse(json.c_str());
    if (root == nullptr) {
        return false;
    }

    const cJSON* schema =
        cJSON_GetObjectItemCaseSensitive(
            root, "schema");

    const cJSON* array =
        cJSON_GetObjectItemCaseSensitive(
            root, "memories");

    if (!cJSON_IsNumber(schema) ||
        schema->valueint != kSchemaVersion ||
        !cJSON_IsArray(array)) {
        cJSON_Delete(root);
        return false;
    }

    const int count = cJSON_GetArraySize(array);

    if (count < 0 ||
        static_cast<size_t>(count) >
            kMaxDayMemoryRecords) {
        cJSON_Delete(root);
        return false;
    }

    const cJSON* item = nullptr;
    cJSON_ArrayForEach(item, array) {
        std::optional<DayActivityMemory> memory =
            DeserializeMemory(item);

        if (!memory.has_value()) {
            cJSON_Delete(root);
            memories->clear();
            return false;
        }

        memories->push_back(
            std::move(memory.value()));
    }

    cJSON_Delete(root);
    return true;
}

bool FileExists(const char* path) {
    if (path == nullptr) {
        return false;
    }

    FILE* f = std::fopen(path, "rb");
    if (f == nullptr) {
        return false;
    }

    std::fclose(f);
    return true;
}

bool ParseIsoDate(const std::string& text,
                  int* year,
                  unsigned* month,
                  unsigned* day) {
    if (year == nullptr ||
        month == nullptr ||
        day == nullptr ||
        text.size() != 10 ||
        text[4] != '-' ||
        text[7] != '-') {
        return false;
    }

    for (size_t i = 0; i < text.size(); ++i) {
        if (i == 4 || i == 7) {
            continue;
        }

        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
    }

    *year =
        std::atoi(text.substr(0, 4).c_str());

    *month =
        static_cast<unsigned>(
            std::atoi(
                text.substr(5, 2).c_str()));

    *day =
        static_cast<unsigned>(
            std::atoi(
                text.substr(8, 2).c_str()));

    if (*month < 1 || *month > 12 ||
        *day < 1 || *day > 31) {
        return false;
    }

    return true;
}

/*
 * Converts a civil calendar date to a monotonically increasing day number.
 * Howard Hinnant's civil-date transformation; epoch choice is irrelevant
 * because DP035 only compares differences between dates.
 */
int64_t CivilDayNumber(
    int year,
    unsigned month,
    unsigned day) {
    year -= month <= 2;

    const int era =
        (year >= 0 ? year : year - 399) / 400;

    const unsigned yoe =
        static_cast<unsigned>(
            year - era * 400);

    const unsigned doy =
        (153 * (month + (month > 2 ? -3 : 9)) + 2)
            / 5 +
        day - 1;

    const unsigned doe =
        yoe * 365 +
        yoe / 4 -
        yoe / 100 +
        doy;

    return static_cast<int64_t>(era) * 146097 +
           static_cast<int64_t>(doe);
}

bool IsoDateToDayNumber(
    const std::string& text,
    int64_t* day_number) {
    if (day_number == nullptr) {
        return false;
    }

    int year = 0;
    unsigned month = 0;
    unsigned day = 0;

    if (!ParseIsoDate(
            text,
            &year,
            &month,
            &day)) {
        return false;
    }

    *day_number =
        CivilDayNumber(year, month, day);

    return true;
}

}  // namespace

bool CareDataDayActivityMemoryRepository::Init() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (FileExists(kDayMemoryPath)) {
        std::vector<DayActivityMemory> memories;

        if (!LoadAll(&memories)) {
            // Day memory is intentionally transient and non-authoritative.
            // A stale, partial or incompatible file must not disable CARE.
            ESP_LOGW(
                kTag,
                "Invalid day-memory file; resetting transient day memory");

            const std::vector<DayActivityMemory> empty;

            if (!SaveAll(empty)) {
                ESP_LOGE(
                    kTag,
                    "Failed to recover invalid day-memory file");

                return false;
            }

            ESP_LOGI(
                kTag,
                "Transient day-memory file recovered");
        }
    } else {
        const std::vector<DayActivityMemory> empty;

        if (!SaveAll(empty)) {
            ESP_LOGE(
                kTag,
                "Failed to create day-memory file");

            return false;
        }
    }

    initialized_ = true;

    ESP_LOGI(
        kTag,
        "Day-memory repository ready");

    return true;
}

std::string
CareDataDayActivityMemoryRepository::GenerateId() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        return "";
    }

    std::vector<DayActivityMemory> memories;
    if (!LoadAll(&memories)) {
        return "";
    }

    uint32_t max_id = 0;

    for (const DayActivityMemory& memory : memories) {
        max_id = std::max(
            max_id,
            MemoryIdNumber(memory.memory_id));
    }

    if (max_id >= 999999u) {
        return "";
    }

    char buffer[16] = {};

    std::snprintf(
        buffer,
        sizeof(buffer),
        "a%06lu",
        static_cast<unsigned long>(max_id + 1));

    return std::string(buffer);
}

bool CareDataDayActivityMemoryRepository::Save(
    const DayActivityMemory& memory) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_ ||
        !memory.IsValid() ||
        !IsMemoryId(memory.memory_id)) {
        return false;
    }

    std::vector<DayActivityMemory> memories;

    if (!LoadAll(&memories)) {
        return false;
    }

    bool replaced = false;

    for (DayActivityMemory& existing : memories) {
        if (existing.memory_id == memory.memory_id) {
            existing = memory;
            replaced = true;
            break;
        }
    }

    if (!replaced) {
        if (memories.size() >=
            kMaxDayMemoryRecords) {
            ESP_LOGW(
                kTag,
                "Day-memory limit reached");

            return false;
        }

        memories.push_back(memory);
    }

    if (!SaveAll(memories)) {
        ESP_LOGE(
            kTag,
            "Failed to save day memory %s",
            memory.memory_id.c_str());

        return false;
    }

    ESP_LOGI(
        kTag,
        "Saved day memory %s",
        memory.memory_id.c_str());

    return true;
}

std::optional<DayActivityMemory>
CareDataDayActivityMemoryRepository::FindById(
    const std::string& memory_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_ ||
        !IsMemoryId(memory_id)) {
        return std::nullopt;
    }

    std::vector<DayActivityMemory> memories;

    if (!LoadAll(&memories)) {
        return std::nullopt;
    }

    for (const DayActivityMemory& memory : memories) {
        if (memory.memory_id == memory_id) {
            return memory;
        }
    }

    return std::nullopt;
}

std::vector<DayActivityMemory>
CareDataDayActivityMemoryRepository::List() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        return {};
    }

    std::vector<DayActivityMemory> memories;

    if (!LoadAll(&memories)) {
        return {};
    }

    return memories;
}

bool CareDataDayActivityMemoryRepository::Delete(
    const std::string& memory_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_ ||
        !IsMemoryId(memory_id)) {
        return false;
    }

    std::vector<DayActivityMemory> memories;

    if (!LoadAll(&memories)) {
        return false;
    }

    const size_t old_size = memories.size();

    memories.erase(
        std::remove_if(
            memories.begin(),
            memories.end(),
            [&](const DayActivityMemory& memory) {
                return memory.memory_id == memory_id;
            }),
        memories.end());

    if (memories.size() == old_size) {
        return false;
    }

    if (!SaveAll(memories)) {
        ESP_LOGE(
            kTag,
            "Failed to delete day memory %s",
            memory_id.c_str());

        return false;
    }

    ESP_LOGI(
        kTag,
        "Deleted day memory %s",
        memory_id.c_str());

    return true;
}

size_t
CareDataDayActivityMemoryRepository::ClearExpired(
    const std::string& current_iso_date,
    uint16_t retention_days) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        return 0;
    }

    int64_t current_day = 0;

    if (!IsoDateToDayNumber(
            current_iso_date,
            &current_day)) {
        ESP_LOGW(
            kTag,
            "Refusing expiration with invalid date");

        return 0;
    }

    std::vector<DayActivityMemory> memories;

    if (!LoadAll(&memories)) {
        return 0;
    }

    const size_t old_size = memories.size();

    memories.erase(
        std::remove_if(
            memories.begin(),
            memories.end(),
            [&](const DayActivityMemory& memory) {
                int64_t memory_day = 0;

                if (!IsoDateToDayNumber(
                        memory.iso_date,
                        &memory_day)) {
                    return false;
                }

                const int64_t age =
                    current_day - memory_day;

                return age >
                    static_cast<int64_t>(
                        retention_days);
            }),
        memories.end());

    const size_t removed =
        old_size - memories.size();

    if (removed == 0) {
        return 0;
    }

    if (!SaveAll(memories)) {
        ESP_LOGE(
            kTag,
            "Failed to persist expiration cleanup");

        return 0;
    }

    ESP_LOGI(
        kTag,
        "Expired %u day-memory records",
        static_cast<unsigned>(removed));

    return removed;
}

}  // namespace xiaozhi_care::daily