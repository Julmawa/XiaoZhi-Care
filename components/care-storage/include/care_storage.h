#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "care_models.h"

namespace xiaozhi_care {

class CareStorage {
public:
    CareStorage() = default;
    ~CareStorage() = default;

    CareStorage(const CareStorage&) = delete;
    CareStorage& operator=(const CareStorage&) = delete;

    CareError Init();
    bool IsReady() const;

    CareError SaveProfile(const CareProfile& profile);
    CareError LoadProfile(CareProfile& profile);

    CareError SavePerson(const CarePerson& person);
    CareError LoadPerson(const std::string& id, CarePerson& result);
    CareError DeletePerson(const std::string& id);
    CareError ListPeople(std::vector<CarePerson>& result);
    CareError GetPersonSequence(uint32_t& sequence);
    CareError SetPersonSequence(uint32_t sequence);

    CareError SavePreference(const CarePreference& preference);
    CareError LoadPreference(const std::string& id, CarePreference& result);
    CareError DeletePreference(const std::string& id);
    CareError ListPreferences(std::vector<CarePreference>& result);
    CareError GetPreferenceSequence(uint32_t& sequence);
    CareError SetPreferenceSequence(uint32_t sequence);

    CareError SavePillboxEntry(const CarePillboxEntry& entry);
    CareError LoadPillboxEntry(const std::string& id, CarePillboxEntry& result);
    CareError DeletePillboxEntry(const std::string& id);
    CareError ListPillboxEntries(std::vector<CarePillboxEntry>& result);
    CareError GetPillboxSequence(uint32_t& sequence);
    CareError SetPillboxSequence(uint32_t sequence);

    CareError SaveReminder(const CareReminder& reminder);
    CareError LoadReminder(const std::string& id, CareReminder& result);
    CareError DeleteReminder(const std::string& id);
    CareError ListReminders(std::vector<CareReminder>& result);
    CareError GetReminderSequence(uint32_t& sequence);
    CareError SetReminderSequence(uint32_t sequence);

    // Local administrator credential record. Only the salt + derived hash are
    // persisted; the plaintext password is never written to NVS.
    CareError SaveAdminAuth(const CareAuthRecord& auth);
    CareError LoadAdminAuth(CareAuthRecord& auth);

private:
    CareError CheckSchemaLocked();

    CareError SerializeProfile(const CareProfile& profile, std::string& json) const;
    CareError DeserializeProfile(const std::string& json, CareProfile& profile) const;
    CareError SerializePerson(const CarePerson& person, std::string& json) const;
    CareError DeserializePerson(const std::string& json, CarePerson& person) const;
    CareError SerializePreference(const CarePreference& preference, std::string& json) const;
    CareError DeserializePreference(const std::string& json, CarePreference& preference) const;
    CareError SerializePillboxEntry(const CarePillboxEntry& entry, std::string& json) const;
    CareError DeserializePillboxEntry(const std::string& json, CarePillboxEntry& entry) const;
    CareError SerializeReminder(const CareReminder& reminder, std::string& json) const;
    CareError DeserializeReminder(const std::string& json, CareReminder& reminder) const;

    mutable std::mutex mutex_;
    bool initialized_ = false;
};

}  // namespace xiaozhi_care
