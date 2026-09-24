#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace xiaozhi_care {

constexpr uint32_t kSchemaVersion = 1;
constexpr uint32_t kProfileRecordVersion = 1;
constexpr uint32_t kPersonRecordVersion = 1;
constexpr uint32_t kPreferenceRecordVersion = 2;
constexpr uint32_t kPillboxRecordVersion = 1;
constexpr uint32_t kReminderRecordVersion = 1;
constexpr uint32_t kAuthRecordVersion = 1;

constexpr size_t kMaxRecordSize = 2048;
constexpr size_t kMaxPeople = 32;
constexpr size_t kMaxPreferences = 32;
constexpr size_t kMaxPillboxEntries = 64;
constexpr size_t kMaxReminders = 64;

constexpr size_t kMaxName = 64;
constexpr size_t kMaxNickname = 32;
constexpr size_t kMaxRelationship = 32;
constexpr size_t kMaxPhone = 32;
constexpr size_t kMaxAddress = 128;
constexpr size_t kMaxBirthday = 10;  // YYYY-MM-DD
constexpr size_t kMaxPetType = 16;
constexpr size_t kMaxPetName = 64;
constexpr size_t kMaxCity = 64;
constexpr size_t kMaxTimezone = 64;
constexpr size_t kMaxNotes = 256;
constexpr size_t kMaxAlias = 48;
constexpr size_t kMaxAliases = 8;
constexpr size_t kMaxPreferenceOwner = 16;
constexpr size_t kMaxCategory = 32;
constexpr size_t kMaxPreferenceValue = 96;
constexpr size_t kMaxPeriod = 16;
constexpr size_t kMaxTime = 5;  // HH:MM
constexpr size_t kMaxCompartment = 64;
constexpr size_t kMaxColor = 32;
constexpr size_t kMaxTitle = 96;
constexpr size_t kMaxRecurrence = 16;

// Authentication constants. Passwords are never persisted in plaintext.
constexpr size_t kMinAdminPasswordBytes = 8;
constexpr size_t kMaxAdminPasswordBytes = 64;
constexpr size_t kAuthSaltBytes = 16;
constexpr size_t kAuthHashBytes = 32;
constexpr uint32_t kAuthPbkdf2Iterations = 10000;

struct CareProfile {
    uint32_t version = kProfileRecordVersion;

    std::string name;
    std::string nickname;
    std::string birthday;
    std::string city;
    std::string timezone;
    std::string notes;
};

struct CarePerson {
    uint32_t version = kPersonRecordVersion;

    std::string id;
    std::string name;
    std::string nickname;
    std::string relationship;
    std::string phone;
    std::string address;
    std::string birthday;
    bool has_pet = false;
    std::string pet_type;  // dog/cat/other
    std::string pet_name;
    std::vector<std::string> aliases;
    std::string notes;

    bool enabled = true;
};

struct CarePreference {
    uint32_t version = kPreferenceRecordVersion;

    std::string id;
    // "profile" for the primary user, or a CarePerson id such as p000003.
    std::string owner_id = "profile";
    std::string category;
    std::string value;
    std::string notes;

    bool enabled = true;
};

struct CarePillboxEntry {
    uint32_t version = kPillboxRecordVersion;

    std::string id;
    uint8_t weekday = 0;  // ISO-8601: 1=Monday ... 7=Sunday
    std::string period;   // morning/noon/afternoon/evening/night
    std::string time;     // optional HH:MM
    std::string compartment;
    std::string pill_color;
    std::string notes;

    bool enabled = true;
};

struct CareReminder {
    uint32_t version = kReminderRecordVersion;

    std::string id;
    std::string title;
    std::string date;       // YYYY-MM-DD
    std::string time;       // optional HH:MM
    std::string recurrence; // none/daily/weekly/monthly/yearly
    std::string related_person_id;
    std::string notes;

    bool enabled = true;
};

struct CareAuthRecord {
    uint32_t version = kAuthRecordVersion;
    uint32_t iterations = kAuthPbkdf2Iterations;
    std::string salt_hex;  // 16 random bytes encoded as 32 lowercase hex chars.
    std::string hash_hex;  // PBKDF2-HMAC-SHA256 output, 32 bytes / 64 hex chars.
};

enum class CareError {
    OK = 0,
    NOT_INITIALIZED,
    INVALID_ARGUMENT,
    INVALID_ID,
    INVALID_DATE,
    INVALID_TIME,
    TOO_LONG,
    TOO_LARGE,
    LIMIT_REACHED,
    NOT_FOUND,
    ALREADY_EXISTS,
    IN_USE,
    SERIALIZATION_ERROR,
    DESERIALIZATION_ERROR,
    STORAGE_ERROR,
    STORAGE_FULL,
    UNSUPPORTED_VERSION,
    INTERNAL_ERROR,
};

const char* CareErrorToString(CareError error);

}  // namespace xiaozhi_care
