#pragma once

#include <string>
#include <vector>

#include "care_models.h"
#include "care_storage.h"

namespace xiaozhi_care {

class CareManager {
public:
    static CareManager& GetInstance();

    CareManager(const CareManager&) = delete;
    CareManager& operator=(const CareManager&) = delete;

    CareError Init();
    bool IsReady() const;

    CareError SaveProfile(const CareProfile& profile);
    CareError GetProfile(CareProfile& profile);

    CareError AddPerson(CarePerson person, std::string& generated_id);
    CareError GetPerson(const std::string& id, CarePerson& result);
    CareError UpdatePerson(const CarePerson& person);
    CareError DeletePerson(const std::string& id);
    CareError ListPeople(std::vector<CarePerson>& result);
    CareError FindPeople(const std::string& query, std::vector<CarePerson>& result);
    CareError FindByRelationship(const std::string& relationship,
                                 std::vector<CarePerson>& result);

    CareError AddPreference(CarePreference preference, std::string& generated_id);
    CareError GetPreference(const std::string& id, CarePreference& result);
    CareError UpdatePreference(const CarePreference& preference);
    CareError DeletePreference(const std::string& id);
    CareError ListPreferences(std::vector<CarePreference>& result);

    CareError AddPillboxEntry(CarePillboxEntry entry, std::string& generated_id);
    CareError GetPillboxEntry(const std::string& id, CarePillboxEntry& result);
    CareError UpdatePillboxEntry(const CarePillboxEntry& entry);
    CareError DeletePillboxEntry(const std::string& id);
    CareError ListPillboxEntries(std::vector<CarePillboxEntry>& result);

    CareError AddReminder(CareReminder reminder, std::string& generated_id);
    CareError GetReminder(const std::string& id, CareReminder& result);
    CareError UpdateReminder(const CareReminder& reminder);
    CareError DeleteReminder(const std::string& id);
    CareError ListReminders(std::vector<CareReminder>& result);

    // Administrator password. The plaintext password exists only long enough
    // to derive/verify the PBKDF2 hash and is never persisted or logged.
    CareError IsAdminPasswordConfigured(bool& configured);
    CareError SetAdminPassword(const std::string& password);
    CareError VerifyAdminPassword(const std::string& password, bool& valid);

private:
    CareManager() = default;

    CareError ValidateProfile(const CareProfile& profile) const;
    CareError ValidatePerson(const CarePerson& person, bool require_id) const;
    CareError ValidatePreference(const CarePreference& preference, bool require_id) const;
    CareError ValidatePillboxEntry(const CarePillboxEntry& entry, bool require_id) const;
    CareError ValidateReminder(const CareReminder& reminder, bool require_id) const;

    CareError GeneratePersonId(std::string& id, uint32_t& sequence);
    CareError GeneratePreferenceId(std::string& id, uint32_t& sequence);
    CareError GeneratePillboxId(std::string& id, uint32_t& sequence);
    CareError GenerateReminderId(std::string& id, uint32_t& sequence);

    static bool IsValidPersonId(const std::string& id);
    static bool IsValidPreferenceId(const std::string& id);
    static bool IsValidPillboxId(const std::string& id);
    static bool IsValidReminderId(const std::string& id);
    static bool IsValidDate(const std::string& date);
    static bool IsValidTime(const std::string& time);
    static bool IsValidUtf8(const std::string& value);
    static size_t Utf8Length(const std::string& value);
    static std::string NormalizeForSearch(const std::string& value);
    static bool PersonMatches(const CarePerson& person, const std::string& normalized_query);

    static bool IsValidAdminPassword(const std::string& password);
    static CareError DeriveAdminPassword(const std::string& password,
                                         const std::string& salt_hex,
                                         uint32_t iterations,
                                         std::string& hash_hex);

    CareStorage storage_;
    bool initialized_ = false;
};

}  // namespace xiaozhi_care
