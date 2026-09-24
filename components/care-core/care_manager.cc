#include "care_manager.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <utility>

#include <esp_log.h>
#include <esp_random.h>
#include <psa/crypto.h>

namespace xiaozhi_care {
namespace {

constexpr char kTag[] = "CARE_CORE";
constexpr uint32_t kMaxSequence = 999999;
constexpr size_t kSha256Bytes = 32;
constexpr size_t kSha256BlockBytes = 64;

std::string MakePersonId(uint32_t sequence) {
    char id[8] = {};
    std::snprintf(id, sizeof(id), "p%06lu", static_cast<unsigned long>(sequence));
    return id;
}

std::string MakePreferenceId(uint32_t sequence) {
    char id[9] = {};
    std::snprintf(id, sizeof(id), "pr%06lu", static_cast<unsigned long>(sequence));
    return id;
}

std::string MakePillboxId(uint32_t sequence) {
    char id[9] = {};
    std::snprintf(id, sizeof(id), "pb%06lu", static_cast<unsigned long>(sequence));
    return id;
}

std::string MakeReminderId(uint32_t sequence) {
    char id[8] = {};
    std::snprintf(id, sizeof(id), "r%06lu", static_cast<unsigned long>(sequence));
    return id;
}

bool IsLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int DaysInMonth(int year, int month) {
    static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && IsLeapYear(year)) {
        return 29;
    }
    return kDays[month - 1];
}

bool IsAllowedPeriod(const std::string& period) {
    return period == "morning" || period == "noon" || period == "afternoon" ||
           period == "evening" || period == "night";
}

bool IsAllowedRecurrence(const std::string& recurrence) {
    return recurrence == "none" || recurrence == "daily" || recurrence == "weekly" ||
           recurrence == "monthly" || recurrence == "yearly";
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

    // Common Spanish/Latin UTF-8 characters. Search normalization deliberately
    // removes diacritics while stored/displayed text remains untouched.
    if (length == 2 && bytes[0] == 0xC3) {
        switch (bytes[1]) {
            case 0x81: case 0xA1: case 0x80: case 0xA0: case 0x84: case 0xA4:
                out.push_back('a'); return;  // Á á À à Ä ä
            case 0x89: case 0xA9: case 0x88: case 0xA8: case 0x8B: case 0xAB:
                out.push_back('e'); return;  // É é È è Ë ë
            case 0x8D: case 0xAD: case 0x8C: case 0xAC: case 0x8F: case 0xAF:
                out.push_back('i'); return;
            case 0x93: case 0xB3: case 0x92: case 0xB2: case 0x96: case 0xB6:
                out.push_back('o'); return;
            case 0x9A: case 0xBA: case 0x99: case 0xB9: case 0x9C: case 0xBC:
                out.push_back('u'); return;
            case 0x91: case 0xB1:
                out.push_back('n'); return;  // Ñ ñ
            default:
                break;
        }
    }

    out.append(reinterpret_cast<const char*>(bytes), length);
}


char HexDigit(uint8_t value) {
    return value < 10 ? static_cast<char>('0' + value)
                      : static_cast<char>('a' + (value - 10));
}

bool HexValue(char c, uint8_t& value) {
    if (c >= '0' && c <= '9') {
        value = static_cast<uint8_t>(c - '0');
        return true;
    }
    if (c >= 'a' && c <= 'f') {
        value = static_cast<uint8_t>(10 + c - 'a');
        return true;
    }
    if (c >= 'A' && c <= 'F') {
        value = static_cast<uint8_t>(10 + c - 'A');
        return true;
    }
    return false;
}

std::string HexEncode(const uint8_t* data, size_t length) {
    std::string out;
    out.resize(length * 2);
    for (size_t i = 0; i < length; ++i) {
        out[i * 2] = HexDigit(static_cast<uint8_t>((data[i] >> 4) & 0x0F));
        out[i * 2 + 1] = HexDigit(static_cast<uint8_t>(data[i] & 0x0F));
    }
    return out;
}

bool HexDecode(const std::string& hex, uint8_t* out, size_t out_length) {
    if (hex.size() != out_length * 2) {
        return false;
    }
    for (size_t i = 0; i < out_length; ++i) {
        uint8_t high = 0;
        uint8_t low = 0;
        if (!HexValue(hex[i * 2], high) || !HexValue(hex[i * 2 + 1], low)) {
            return false;
        }
        out[i] = static_cast<uint8_t>((high << 4) | low);
    }
    return true;
}

bool Sha256Concat(const uint8_t* first, size_t first_length,
                  const uint8_t* second, size_t second_length,
                  uint8_t out[kSha256Bytes]) {
    psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
    psa_status_t status = psa_hash_setup(&op, PSA_ALG_SHA_256);
    if (status == PSA_SUCCESS && first_length > 0) {
        status = psa_hash_update(&op, first, first_length);
    }
    if (status == PSA_SUCCESS && second_length > 0) {
        status = psa_hash_update(&op, second, second_length);
    }
    size_t output_length = 0;
    if (status == PSA_SUCCESS) {
        status = psa_hash_finish(&op, out, kSha256Bytes, &output_length);
    }
    if (status != PSA_SUCCESS) {
        psa_hash_abort(&op);
        return false;
    }
    return output_length == kSha256Bytes;
}

bool HmacSha256(const std::string& key, const uint8_t* message, size_t message_length,
                uint8_t out[kSha256Bytes]) {
    if (key.size() > kSha256BlockBytes) {
        return false;  // Password validation currently caps keys at 64 bytes.
    }

    std::array<uint8_t, kSha256BlockBytes> ipad{};
    std::array<uint8_t, kSha256BlockBytes> opad{};
    for (size_t i = 0; i < kSha256BlockBytes; ++i) {
        const uint8_t key_byte = i < key.size() ? static_cast<uint8_t>(key[i]) : 0;
        ipad[i] = static_cast<uint8_t>(key_byte ^ 0x36);
        opad[i] = static_cast<uint8_t>(key_byte ^ 0x5c);
    }

    std::array<uint8_t, kSha256Bytes> inner{};
    if (!Sha256Concat(ipad.data(), ipad.size(), message, message_length, inner.data())) {
        return false;
    }
    return Sha256Concat(opad.data(), opad.size(), inner.data(), inner.size(), out);
}

bool Pbkdf2HmacSha256(const std::string& password,
                      const uint8_t* salt, size_t salt_length,
                      uint32_t iterations,
                      uint8_t out[kSha256Bytes]) {
    if (password.empty() || salt == nullptr || salt_length == 0 || iterations == 0) {
        return false;
    }

    // XiaoZhi Care currently derives exactly one SHA-256 block (32 bytes), so
    // PBKDF2 only needs block index 1. This is standard PBKDF2-HMAC-SHA256.
    std::vector<uint8_t> first_message(salt_length + 4);
    std::memcpy(first_message.data(), salt, salt_length);
    first_message[salt_length] = 0;
    first_message[salt_length + 1] = 0;
    first_message[salt_length + 2] = 0;
    first_message[salt_length + 3] = 1;

    std::array<uint8_t, kSha256Bytes> u{};
    std::array<uint8_t, kSha256Bytes> next{};
    if (!HmacSha256(password, first_message.data(), first_message.size(), u.data())) {
        return false;
    }
    std::memcpy(out, u.data(), kSha256Bytes);

    for (uint32_t round = 1; round < iterations; ++round) {
        if (!HmacSha256(password, u.data(), u.size(), next.data())) {
            return false;
        }
        for (size_t i = 0; i < kSha256Bytes; ++i) {
            out[i] ^= next[i];
        }
        u = next;
    }
    return true;
}

bool ConstantTimeEqual(const uint8_t* left, const uint8_t* right, size_t length) {
    uint8_t diff = 0;
    for (size_t i = 0; i < length; ++i) {
        diff |= static_cast<uint8_t>(left[i] ^ right[i]);
    }
    return diff == 0;
}

}  // namespace

CareManager& CareManager::GetInstance() {
    static CareManager instance;
    return instance;
}

CareError CareManager::Init() {
    if (initialized_) {
        return CareError::OK;
    }

    ESP_LOGI(kTag, "Initializing XiaoZhi Care v0.3.2-alpha");
    CareError result = storage_.Init();
    if (result != CareError::OK) {
        ESP_LOGE(kTag, "Storage init failed: %s", CareErrorToString(result));
        initialized_ = false;
        return result;
    }

    initialized_ = true;
    ESP_LOGI(kTag, "XiaoZhi Care ready");
    return CareError::OK;
}

bool CareManager::IsReady() const {
    return initialized_ && storage_.IsReady();
}

bool CareManager::IsValidPersonId(const std::string& id) {
    if (id.size() != 7 || id[0] != 'p') return false;
    for (size_t i = 1; i < id.size(); ++i) {
        if (id[i] < '0' || id[i] > '9') return false;
    }
    return true;
}

bool CareManager::IsValidPreferenceId(const std::string& id) {
    if (id.size() != 8 || id[0] != 'p' || id[1] != 'r') return false;
    for (size_t i = 2; i < id.size(); ++i) {
        if (id[i] < '0' || id[i] > '9') return false;
    }
    return true;
}

bool CareManager::IsValidPillboxId(const std::string& id) {
    if (id.size() != 8 || id[0] != 'p' || id[1] != 'b') return false;
    for (size_t i = 2; i < id.size(); ++i) {
        if (id[i] < '0' || id[i] > '9') return false;
    }
    return true;
}

bool CareManager::IsValidReminderId(const std::string& id) {
    if (id.size() != 7 || id[0] != 'r') return false;
    for (size_t i = 1; i < id.size(); ++i) {
        if (id[i] < '0' || id[i] > '9') return false;
    }
    return true;
}

bool CareManager::IsValidUtf8(const std::string& value) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(value.data());
    size_t i = 0;
    while (i < value.size()) {
        unsigned char c = bytes[i];
        if (c <= 0x7F) {
            if (c < 0x20 && c != '\t') {
                return false;
            }
            ++i;
            continue;
        }

        size_t continuation = 0;
        uint32_t codepoint = 0;
        if ((c & 0xE0) == 0xC0) {
            continuation = 1;
            codepoint = c & 0x1F;
            if (codepoint == 0) return false;  // overlong
        } else if ((c & 0xF0) == 0xE0) {
            continuation = 2;
            codepoint = c & 0x0F;
        } else if ((c & 0xF8) == 0xF0) {
            continuation = 3;
            codepoint = c & 0x07;
        } else {
            return false;
        }

        if (i + continuation >= value.size()) {
            return false;
        }
        for (size_t j = 1; j <= continuation; ++j) {
            unsigned char cc = bytes[i + j];
            if ((cc & 0xC0) != 0x80) {
                return false;
            }
            codepoint = (codepoint << 6) | (cc & 0x3F);
        }

        if ((continuation == 1 && codepoint < 0x80) ||
            (continuation == 2 && codepoint < 0x800) ||
            (continuation == 3 && codepoint < 0x10000) ||
            codepoint > 0x10FFFF ||
            (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
            return false;
        }

        i += continuation + 1;
    }
    return true;
}

size_t CareManager::Utf8Length(const std::string& value) {
    size_t count = 0;
    for (unsigned char c : value) {
        if ((c & 0xC0) != 0x80) {
            ++count;
        }
    }
    return count;
}

bool CareManager::IsValidDate(const std::string& date) {
    if (date.empty()) {
        return true;
    }
    if (date.size() != 10 || date[4] != '-' || date[7] != '-') {
        return false;
    }
    for (size_t i = 0; i < date.size(); ++i) {
        if (i == 4 || i == 7) continue;
        if (date[i] < '0' || date[i] > '9') return false;
    }

    const int year = (date[0] - '0') * 1000 + (date[1] - '0') * 100 +
                     (date[2] - '0') * 10 + (date[3] - '0');
    const int month = (date[5] - '0') * 10 + (date[6] - '0');
    const int day = (date[8] - '0') * 10 + (date[9] - '0');
    if (year < 1 || month < 1 || month > 12) {
        return false;
    }
    return day >= 1 && day <= DaysInMonth(year, month);
}

bool CareManager::IsValidTime(const std::string& time) {
    if (time.empty()) return true;
    if (time.size() != 5 || time[2] != ':') return false;
    if (time[0] < '0' || time[0] > '9' || time[1] < '0' || time[1] > '9' ||
        time[3] < '0' || time[3] > '9' || time[4] < '0' || time[4] > '9') {
        return false;
    }
    const int hour = (time[0] - '0') * 10 + (time[1] - '0');
    const int minute = (time[3] - '0') * 10 + (time[4] - '0');
    return hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59;
}

CareError CareManager::ValidateProfile(const CareProfile& profile) const {
    if (profile.version != kProfileRecordVersion || profile.name.empty()) {
        return CareError::INVALID_ARGUMENT;
    }
    const auto valid_text = [](const std::string& value, size_t max_chars) {
        return CareManager::IsValidUtf8(value) && CareManager::Utf8Length(value) <= max_chars;
    };
    if (!valid_text(profile.name, kMaxName) ||
        !valid_text(profile.nickname, kMaxNickname) ||
        !valid_text(profile.city, kMaxCity) ||
        !valid_text(profile.timezone, kMaxTimezone) ||
        !valid_text(profile.notes, kMaxNotes)) {
        return CareError::TOO_LONG;
    }
    if (!IsValidDate(profile.birthday)) return CareError::INVALID_DATE;
    return CareError::OK;
}

CareError CareManager::ValidatePerson(const CarePerson& person, bool require_id) const {
    if (require_id && !IsValidPersonId(person.id)) {
        return CareError::INVALID_ID;
    }
    if (!require_id && !person.id.empty()) {
        return CareError::INVALID_ID;
    }
    if (person.version != kPersonRecordVersion || person.name.empty()) {
        return CareError::INVALID_ARGUMENT;
    }
    if (person.aliases.size() > kMaxAliases) {
        return CareError::TOO_LONG;
    }

    const auto valid_text = [](const std::string& value, size_t max_chars) {
        return CareManager::IsValidUtf8(value) && CareManager::Utf8Length(value) <= max_chars;
    };

    if (!valid_text(person.name, kMaxName) ||
        !valid_text(person.nickname, kMaxNickname) ||
        !valid_text(person.relationship, kMaxRelationship) ||
        !valid_text(person.phone, kMaxPhone) ||
        !valid_text(person.address, kMaxAddress) ||
        !valid_text(person.notes, kMaxNotes)) {
        return CareError::TOO_LONG;
    }

    for (const auto& alias : person.aliases) {
        if (!valid_text(alias, kMaxAlias)) {
            return CareError::TOO_LONG;
        }
    }

    if (!IsValidDate(person.birthday)) {
        return CareError::INVALID_DATE;
    }
    return CareError::OK;
}

CareError CareManager::ValidatePreference(const CarePreference& preference, bool require_id) const {
    if (require_id && !IsValidPreferenceId(preference.id)) return CareError::INVALID_ID;
    if (!require_id && !preference.id.empty()) return CareError::INVALID_ID;
    if (preference.version != kPreferenceRecordVersion || preference.owner_id.empty() ||
        preference.category.empty() || preference.value.empty()) {
        return CareError::INVALID_ARGUMENT;
    }
    if (preference.owner_id != "profile" && !IsValidPersonId(preference.owner_id)) {
        return CareError::INVALID_ARGUMENT;
    }
    const auto valid_text = [](const std::string& value, size_t max_chars) {
        return CareManager::IsValidUtf8(value) && CareManager::Utf8Length(value) <= max_chars;
    };
    if (!valid_text(preference.owner_id, kMaxPreferenceOwner) ||
        !valid_text(preference.category, kMaxCategory) ||
        !valid_text(preference.value, kMaxPreferenceValue) ||
        !valid_text(preference.notes, kMaxNotes)) {
        return CareError::TOO_LONG;
    }
    return CareError::OK;
}

CareError CareManager::ValidatePillboxEntry(const CarePillboxEntry& entry, bool require_id) const {
    if (require_id && !IsValidPillboxId(entry.id)) return CareError::INVALID_ID;
    if (!require_id && !entry.id.empty()) return CareError::INVALID_ID;
    if (entry.version != kPillboxRecordVersion || entry.weekday < 1 || entry.weekday > 7 ||
        !IsAllowedPeriod(entry.period) || entry.compartment.empty()) {
        return CareError::INVALID_ARGUMENT;
    }
    const auto valid_text = [](const std::string& value, size_t max_chars) {
        return CareManager::IsValidUtf8(value) && CareManager::Utf8Length(value) <= max_chars;
    };
    if (!valid_text(entry.period, kMaxPeriod) ||
        !valid_text(entry.compartment, kMaxCompartment) ||
        !valid_text(entry.pill_color, kMaxColor) ||
        !valid_text(entry.notes, kMaxNotes)) {
        return CareError::TOO_LONG;
    }
    if (!IsValidTime(entry.time)) return CareError::INVALID_TIME;
    return CareError::OK;
}

CareError CareManager::ValidateReminder(const CareReminder& reminder, bool require_id) const {
    if (require_id && !IsValidReminderId(reminder.id)) return CareError::INVALID_ID;
    if (!require_id && !reminder.id.empty()) return CareError::INVALID_ID;
    if (reminder.version != kReminderRecordVersion || reminder.title.empty() ||
        reminder.date.empty() || !IsAllowedRecurrence(reminder.recurrence)) {
        return CareError::INVALID_ARGUMENT;
    }
    const auto valid_text = [](const std::string& value, size_t max_chars) {
        return CareManager::IsValidUtf8(value) && CareManager::Utf8Length(value) <= max_chars;
    };
    if (!valid_text(reminder.title, kMaxTitle) ||
        !valid_text(reminder.recurrence, kMaxRecurrence) ||
        !valid_text(reminder.notes, kMaxNotes)) {
        return CareError::TOO_LONG;
    }
    if (!IsValidDate(reminder.date)) return CareError::INVALID_DATE;
    if (!IsValidTime(reminder.time)) return CareError::INVALID_TIME;
    if (!reminder.related_person_id.empty() && !IsValidPersonId(reminder.related_person_id)) {
        return CareError::INVALID_ID;
    }
    return CareError::OK;
}

CareError CareManager::GeneratePersonId(std::string& id, uint32_t& sequence) {
    CareError seq_result = storage_.GetPersonSequence(sequence);
    if (seq_result != CareError::OK) {
        return seq_result;
    }

    while (sequence < kMaxSequence) {
        ++sequence;
        std::string candidate = MakePersonId(sequence);
        CarePerson existing;
        CareError lookup = storage_.LoadPerson(candidate, existing);
        if (lookup == CareError::NOT_FOUND) {
            id = std::move(candidate);
            return CareError::OK;
        }
        if (lookup != CareError::OK) {
            return lookup;
        }
    }
    return CareError::LIMIT_REACHED;
}

CareError CareManager::GeneratePreferenceId(std::string& id, uint32_t& sequence) {
    CareError seq_result = storage_.GetPreferenceSequence(sequence);
    if (seq_result != CareError::OK) return seq_result;
    while (sequence < kMaxSequence) {
        ++sequence;
        std::string candidate = MakePreferenceId(sequence);
        CarePreference existing;
        CareError lookup = storage_.LoadPreference(candidate, existing);
        if (lookup == CareError::NOT_FOUND) { id = std::move(candidate); return CareError::OK; }
        if (lookup != CareError::OK) return lookup;
    }
    return CareError::LIMIT_REACHED;
}

CareError CareManager::GeneratePillboxId(std::string& id, uint32_t& sequence) {
    CareError seq_result = storage_.GetPillboxSequence(sequence);
    if (seq_result != CareError::OK) return seq_result;
    while (sequence < kMaxSequence) {
        ++sequence;
        std::string candidate = MakePillboxId(sequence);
        CarePillboxEntry existing;
        CareError lookup = storage_.LoadPillboxEntry(candidate, existing);
        if (lookup == CareError::NOT_FOUND) { id = std::move(candidate); return CareError::OK; }
        if (lookup != CareError::OK) return lookup;
    }
    return CareError::LIMIT_REACHED;
}

CareError CareManager::GenerateReminderId(std::string& id, uint32_t& sequence) {
    CareError seq_result = storage_.GetReminderSequence(sequence);
    if (seq_result != CareError::OK) return seq_result;
    while (sequence < kMaxSequence) {
        ++sequence;
        std::string candidate = MakeReminderId(sequence);
        CareReminder existing;
        CareError lookup = storage_.LoadReminder(candidate, existing);
        if (lookup == CareError::NOT_FOUND) { id = std::move(candidate); return CareError::OK; }
        if (lookup != CareError::OK) return lookup;
    }
    return CareError::LIMIT_REACHED;
}

CareError CareManager::SaveProfile(const CareProfile& profile) {
    if (!IsReady()) return CareError::NOT_INITIALIZED;
    CareError validation = ValidateProfile(profile);
    if (validation != CareError::OK) return validation;
    CareError result = storage_.SaveProfile(profile);
    if (result == CareError::OK) ESP_LOGI(kTag, "Profile saved");
    return result;
}

CareError CareManager::GetProfile(CareProfile& profile) {
    if (!IsReady()) return CareError::NOT_INITIALIZED;
    return storage_.LoadProfile(profile);
}

CareError CareManager::AddPerson(CarePerson person, std::string& generated_id) {
    generated_id.clear();
    if (!IsReady()) {
        return CareError::NOT_INITIALIZED;
    }

    CareError validation = ValidatePerson(person, false);
    if (validation != CareError::OK) {
        return validation;
    }

    std::vector<CarePerson> people;
    CareError list_result = storage_.ListPeople(people);
    if (list_result != CareError::OK) {
        return list_result;
    }
    if (people.size() >= kMaxPeople) {
        return CareError::LIMIT_REACHED;
    }

    uint32_t sequence = 0;
    CareError id_result = GeneratePersonId(person.id, sequence);
    if (id_result != CareError::OK) {
        return id_result;
    }

    CareError save_result = storage_.SavePerson(person);
    if (save_result != CareError::OK) {
        return save_result;
    }

    // Commit the sequence after the record. If this fails, the next AddPerson()
    // safely detects the already-used ID before generating a new one.
    CareError seq_save = storage_.SetPersonSequence(sequence);
    if (seq_save != CareError::OK) {
        ESP_LOGW(kTag, "Person saved but sequence update failed: %s", CareErrorToString(seq_save));
        return seq_save;
    }

    generated_id = person.id;
    ESP_LOGI(kTag, "Person added: %s", generated_id.c_str());
    return CareError::OK;
}

CareError CareManager::GetPerson(const std::string& id, CarePerson& result) {
    if (!IsReady()) {
        return CareError::NOT_INITIALIZED;
    }
    if (!IsValidPersonId(id)) {
        return CareError::INVALID_ID;
    }
    return storage_.LoadPerson(id, result);
}

CareError CareManager::UpdatePerson(const CarePerson& person) {
    if (!IsReady()) {
        return CareError::NOT_INITIALIZED;
    }
    CareError validation = ValidatePerson(person, true);
    if (validation != CareError::OK) {
        return validation;
    }

    CarePerson current;
    CareError exists = storage_.LoadPerson(person.id, current);
    if (exists != CareError::OK) {
        return exists;
    }

    CareError result = storage_.SavePerson(person);
    if (result == CareError::OK) {
        ESP_LOGI(kTag, "Person updated: %s", person.id.c_str());
    }
    return result;
}

CareError CareManager::DeletePerson(const std::string& id) {
    if (!IsReady()) return CareError::NOT_INITIALIZED;
    if (!IsValidPersonId(id)) return CareError::INVALID_ID;

    // Preserve referential integrity. A person referenced by preferences or
    // reminders must be detached before the person can be deleted.
    std::vector<CarePreference> preferences;
    CareError pref_result = storage_.ListPreferences(preferences);
    if (pref_result != CareError::OK) return pref_result;
    for (const auto& preference : preferences) {
        if (preference.owner_id == id) return CareError::IN_USE;
    }

    std::vector<CareReminder> reminders;
    CareError list_result = storage_.ListReminders(reminders);
    if (list_result != CareError::OK) return list_result;
    for (const auto& reminder : reminders) {
        if (reminder.related_person_id == id) return CareError::IN_USE;
    }

    CareError result = storage_.DeletePerson(id);
    if (result == CareError::OK) ESP_LOGI(kTag, "Person deleted: %s", id.c_str());
    return result;
}

CareError CareManager::ListPeople(std::vector<CarePerson>& result) {
    if (!IsReady()) {
        return CareError::NOT_INITIALIZED;
    }
    return storage_.ListPeople(result);
}

CareError CareManager::AddPreference(CarePreference preference, std::string& generated_id) {
    generated_id.clear();
    if (!IsReady()) return CareError::NOT_INITIALIZED;
    CareError validation = ValidatePreference(preference, false);
    if (validation != CareError::OK) return validation;
    if (preference.owner_id != "profile") {
        CarePerson owner;
        CareError owner_result = storage_.LoadPerson(preference.owner_id, owner);
        if (owner_result != CareError::OK) return owner_result;
    }
    std::vector<CarePreference> values;
    CareError list_result = storage_.ListPreferences(values);
    if (list_result != CareError::OK) return list_result;
    if (values.size() >= kMaxPreferences) return CareError::LIMIT_REACHED;
    uint32_t sequence = 0;
    CareError id_result = GeneratePreferenceId(preference.id, sequence);
    if (id_result != CareError::OK) return id_result;
    CareError save_result = storage_.SavePreference(preference);
    if (save_result != CareError::OK) return save_result;
    CareError seq_save = storage_.SetPreferenceSequence(sequence);
    if (seq_save != CareError::OK) return seq_save;
    generated_id = preference.id;
    ESP_LOGI(kTag, "Preference added: %s", generated_id.c_str());
    return CareError::OK;
}

CareError CareManager::GetPreference(const std::string& id, CarePreference& result) {
    if (!IsReady()) return CareError::NOT_INITIALIZED;
    if (!IsValidPreferenceId(id)) return CareError::INVALID_ID;
    return storage_.LoadPreference(id, result);
}

CareError CareManager::UpdatePreference(const CarePreference& preference) {
    if (!IsReady()) return CareError::NOT_INITIALIZED;
    CareError validation = ValidatePreference(preference, true);
    if (validation != CareError::OK) return validation;
    if (preference.owner_id != "profile") {
        CarePerson owner;
        CareError owner_result = storage_.LoadPerson(preference.owner_id, owner);
        if (owner_result != CareError::OK) return owner_result;
    }
    CarePreference current;
    CareError exists = storage_.LoadPreference(preference.id, current);
    if (exists != CareError::OK) return exists;
    CareError result = storage_.SavePreference(preference);
    if (result == CareError::OK) ESP_LOGI(kTag, "Preference updated: %s", preference.id.c_str());
    return result;
}

CareError CareManager::DeletePreference(const std::string& id) {
    if (!IsReady()) return CareError::NOT_INITIALIZED;
    if (!IsValidPreferenceId(id)) return CareError::INVALID_ID;
    CareError result = storage_.DeletePreference(id);
    if (result == CareError::OK) ESP_LOGI(kTag, "Preference deleted: %s", id.c_str());
    return result;
}

CareError CareManager::ListPreferences(std::vector<CarePreference>& result) {
    if (!IsReady()) return CareError::NOT_INITIALIZED;
    return storage_.ListPreferences(result);
}

CareError CareManager::AddPillboxEntry(CarePillboxEntry entry, std::string& generated_id) {
    generated_id.clear();
    if (!IsReady()) return CareError::NOT_INITIALIZED;
    CareError validation = ValidatePillboxEntry(entry, false);
    if (validation != CareError::OK) return validation;
    std::vector<CarePillboxEntry> values;
    CareError list_result = storage_.ListPillboxEntries(values);
    if (list_result != CareError::OK) return list_result;
    if (values.size() >= kMaxPillboxEntries) return CareError::LIMIT_REACHED;
    uint32_t sequence = 0;
    CareError id_result = GeneratePillboxId(entry.id, sequence);
    if (id_result != CareError::OK) return id_result;
    CareError save_result = storage_.SavePillboxEntry(entry);
    if (save_result != CareError::OK) return save_result;
    CareError seq_save = storage_.SetPillboxSequence(sequence);
    if (seq_save != CareError::OK) return seq_save;
    generated_id = entry.id;
    ESP_LOGI(kTag, "Pillbox entry added: %s", generated_id.c_str());
    return CareError::OK;
}

CareError CareManager::GetPillboxEntry(const std::string& id, CarePillboxEntry& result) {
    if (!IsReady()) return CareError::NOT_INITIALIZED;
    if (!IsValidPillboxId(id)) return CareError::INVALID_ID;
    return storage_.LoadPillboxEntry(id, result);
}

CareError CareManager::UpdatePillboxEntry(const CarePillboxEntry& entry) {
    if (!IsReady()) return CareError::NOT_INITIALIZED;
    CareError validation = ValidatePillboxEntry(entry, true);
    if (validation != CareError::OK) return validation;
    CarePillboxEntry current;
    CareError exists = storage_.LoadPillboxEntry(entry.id, current);
    if (exists != CareError::OK) return exists;
    CareError result = storage_.SavePillboxEntry(entry);
    if (result == CareError::OK) ESP_LOGI(kTag, "Pillbox entry updated: %s", entry.id.c_str());
    return result;
}

CareError CareManager::DeletePillboxEntry(const std::string& id) {
    if (!IsReady()) return CareError::NOT_INITIALIZED;
    if (!IsValidPillboxId(id)) return CareError::INVALID_ID;
    CareError result = storage_.DeletePillboxEntry(id);
    if (result == CareError::OK) ESP_LOGI(kTag, "Pillbox entry deleted: %s", id.c_str());
    return result;
}

CareError CareManager::ListPillboxEntries(std::vector<CarePillboxEntry>& result) {
    if (!IsReady()) return CareError::NOT_INITIALIZED;
    return storage_.ListPillboxEntries(result);
}

CareError CareManager::AddReminder(CareReminder reminder, std::string& generated_id) {
    generated_id.clear();
    if (!IsReady()) return CareError::NOT_INITIALIZED;
    CareError validation = ValidateReminder(reminder, false);
    if (validation != CareError::OK) return validation;
    if (!reminder.related_person_id.empty()) {
        CarePerson person;
        CareError person_result = storage_.LoadPerson(reminder.related_person_id, person);
        if (person_result != CareError::OK) return person_result;
    }
    std::vector<CareReminder> values;
    CareError list_result = storage_.ListReminders(values);
    if (list_result != CareError::OK) return list_result;
    if (values.size() >= kMaxReminders) return CareError::LIMIT_REACHED;
    uint32_t sequence = 0;
    CareError id_result = GenerateReminderId(reminder.id, sequence);
    if (id_result != CareError::OK) return id_result;
    CareError save_result = storage_.SaveReminder(reminder);
    if (save_result != CareError::OK) return save_result;
    CareError seq_save = storage_.SetReminderSequence(sequence);
    if (seq_save != CareError::OK) return seq_save;
    generated_id = reminder.id;
    ESP_LOGI(kTag, "Reminder added: %s", generated_id.c_str());
    return CareError::OK;
}

CareError CareManager::GetReminder(const std::string& id, CareReminder& result) {
    if (!IsReady()) return CareError::NOT_INITIALIZED;
    if (!IsValidReminderId(id)) return CareError::INVALID_ID;
    return storage_.LoadReminder(id, result);
}

CareError CareManager::UpdateReminder(const CareReminder& reminder) {
    if (!IsReady()) return CareError::NOT_INITIALIZED;
    CareError validation = ValidateReminder(reminder, true);
    if (validation != CareError::OK) return validation;
    CareReminder current;
    CareError exists = storage_.LoadReminder(reminder.id, current);
    if (exists != CareError::OK) return exists;
    if (!reminder.related_person_id.empty()) {
        CarePerson person;
        CareError person_result = storage_.LoadPerson(reminder.related_person_id, person);
        if (person_result != CareError::OK) return person_result;
    }
    CareError result = storage_.SaveReminder(reminder);
    if (result == CareError::OK) ESP_LOGI(kTag, "Reminder updated: %s", reminder.id.c_str());
    return result;
}

CareError CareManager::DeleteReminder(const std::string& id) {
    if (!IsReady()) return CareError::NOT_INITIALIZED;
    if (!IsValidReminderId(id)) return CareError::INVALID_ID;
    CareError result = storage_.DeleteReminder(id);
    if (result == CareError::OK) ESP_LOGI(kTag, "Reminder deleted: %s", id.c_str());
    return result;
}

CareError CareManager::ListReminders(std::vector<CareReminder>& result) {
    if (!IsReady()) return CareError::NOT_INITIALIZED;
    return storage_.ListReminders(result);
}

std::string CareManager::NormalizeForSearch(const std::string& value) {
    std::string out;
    out.reserve(value.size());

    const auto* bytes = reinterpret_cast<const unsigned char*>(value.data());
    bool previous_space = true;
    size_t i = 0;
    while (i < value.size()) {
        unsigned char c = bytes[i];
        size_t length = 1;
        if ((c & 0xE0) == 0xC0) length = 2;
        else if ((c & 0xF0) == 0xE0) length = 3;
        else if ((c & 0xF8) == 0xF0) length = 4;

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

bool CareManager::PersonMatches(const CarePerson& person, const std::string& normalized_query) {
    if (NormalizeForSearch(person.id) == normalized_query ||
        NormalizeForSearch(person.name) == normalized_query ||
        NormalizeForSearch(person.nickname) == normalized_query ||
        NormalizeForSearch(person.relationship) == normalized_query) {
        return true;
    }

    for (const auto& alias : person.aliases) {
        if (NormalizeForSearch(alias) == normalized_query) {
            return true;
        }
    }
    return false;
}

CareError CareManager::FindPeople(const std::string& query, std::vector<CarePerson>& result) {
    result.clear();
    if (!IsReady()) {
        return CareError::NOT_INITIALIZED;
    }
    if (!IsValidUtf8(query)) {
        return CareError::INVALID_ARGUMENT;
    }

    const std::string normalized = NormalizeForSearch(query);
    if (normalized.empty()) {
        return CareError::INVALID_ARGUMENT;
    }

    std::vector<CarePerson> people;
    CareError list_result = storage_.ListPeople(people);
    if (list_result != CareError::OK) {
        return list_result;
    }

    for (const auto& person : people) {
        if (person.enabled && PersonMatches(person, normalized)) {
            result.push_back(person);
        }
    }
    return result.empty() ? CareError::NOT_FOUND : CareError::OK;
}

CareError CareManager::FindByRelationship(const std::string& relationship,
                                          std::vector<CarePerson>& result) {
    result.clear();
    if (!IsReady()) {
        return CareError::NOT_INITIALIZED;
    }
    if (!IsValidUtf8(relationship)) {
        return CareError::INVALID_ARGUMENT;
    }

    const std::string normalized = NormalizeForSearch(relationship);
    if (normalized.empty()) {
        return CareError::INVALID_ARGUMENT;
    }

    std::vector<CarePerson> people;
    CareError list_result = storage_.ListPeople(people);
    if (list_result != CareError::OK) {
        return list_result;
    }

    for (const auto& person : people) {
        if (person.enabled && NormalizeForSearch(person.relationship) == normalized) {
            result.push_back(person);
        }
    }
    return result.empty() ? CareError::NOT_FOUND : CareError::OK;
}


bool CareManager::IsValidAdminPassword(const std::string& password) {
    if (password.size() < kMinAdminPasswordBytes || password.size() > kMaxAdminPasswordBytes) {
        return false;
    }
    if (!IsValidUtf8(password)) {
        return false;
    }
    // Reject ASCII control characters. Spaces and punctuation are allowed.
    for (unsigned char c : password) {
        if (c < 0x20 || c == 0x7F) {
            return false;
        }
    }
    return true;
}

CareError CareManager::DeriveAdminPassword(const std::string& password,
                                           const std::string& salt_hex,
                                           uint32_t iterations,
                                           std::string& hash_hex) {
    hash_hex.clear();
    if (!IsValidAdminPassword(password) || iterations == 0) {
        return CareError::INVALID_ARGUMENT;
    }

    std::array<uint8_t, kAuthSaltBytes> salt{};
    if (!HexDecode(salt_hex, salt.data(), salt.size())) {
        return CareError::DESERIALIZATION_ERROR;
    }

    psa_status_t psa_status = psa_crypto_init();
    if (psa_status != PSA_SUCCESS) {
        return CareError::INTERNAL_ERROR;
    }

    std::array<uint8_t, kAuthHashBytes> derived{};
    if (!Pbkdf2HmacSha256(password, salt.data(), salt.size(), iterations, derived.data())) {
        return CareError::INTERNAL_ERROR;
    }
    hash_hex = HexEncode(derived.data(), derived.size());
    return CareError::OK;
}

CareError CareManager::IsAdminPasswordConfigured(bool& configured) {
    configured = false;
    if (!IsReady()) {
        return CareError::NOT_INITIALIZED;
    }

    CareAuthRecord auth;
    CareError result = storage_.LoadAdminAuth(auth);
    if (result == CareError::NOT_FOUND) {
        return CareError::OK;
    }
    if (result != CareError::OK) {
        return result;
    }
    configured = true;
    return CareError::OK;
}

CareError CareManager::SetAdminPassword(const std::string& password) {
    if (!IsReady()) {
        return CareError::NOT_INITIALIZED;
    }
    if (!IsValidAdminPassword(password)) {
        return CareError::INVALID_ARGUMENT;
    }

    std::array<uint8_t, kAuthSaltBytes> salt{};
    esp_fill_random(salt.data(), salt.size());

    CareAuthRecord auth;
    auth.version = kAuthRecordVersion;
    auth.iterations = kAuthPbkdf2Iterations;
    auth.salt_hex = HexEncode(salt.data(), salt.size());

    CareError derive = DeriveAdminPassword(password, auth.salt_hex, auth.iterations, auth.hash_hex);
    if (derive != CareError::OK) {
        return derive;
    }

    CareError saved = storage_.SaveAdminAuth(auth);
    if (saved == CareError::OK) {
        ESP_LOGI(kTag, "Administrator password configured/updated");
    }
    return saved;
}

CareError CareManager::VerifyAdminPassword(const std::string& password, bool& valid) {
    valid = false;
    if (!IsReady()) {
        return CareError::NOT_INITIALIZED;
    }
    if (!IsValidAdminPassword(password)) {
        return CareError::OK;
    }

    CareAuthRecord auth;
    CareError load = storage_.LoadAdminAuth(auth);
    if (load != CareError::OK) {
        return load;
    }

    std::string derived_hex;
    CareError derive = DeriveAdminPassword(password, auth.salt_hex, auth.iterations, derived_hex);
    if (derive != CareError::OK) {
        return derive;
    }

    std::array<uint8_t, kAuthHashBytes> expected{};
    std::array<uint8_t, kAuthHashBytes> actual{};
    if (!HexDecode(auth.hash_hex, expected.data(), expected.size()) ||
        !HexDecode(derived_hex, actual.data(), actual.size())) {
        return CareError::DESERIALIZATION_ERROR;
    }

    valid = ConstantTimeEqual(expected.data(), actual.data(), expected.size());
    return CareError::OK;
}

}  // namespace xiaozhi_care
