#pragma once

#include <string>

namespace xiaozhi_care {

// Read-only MCP-facing query service. It exposes only the minimum fields needed
// for conversational answers and never returns administrator credentials or
// free-form notes.
class CareMcpService {
public:
    static CareMcpService& GetInstance();

    // DP034_R4_CACHE_LIFECYCLE
    // Preload evita el costo de /care_data en la primera consulta.
    // Las invalidaciones mantienen coherencia cuando el panel modifica datos.
    void PreloadCache();
    void InvalidatePeopleCache();
    void InvalidatePreferencesCache();

    std::string GetProfile(const std::string& field = "basic");
    std::string FindPerson(const std::string& query,
                           const std::string& field = "basic");
    std::string GetPreference(const std::string& category,
                              const std::string& owner = "profile");
    std::string GetPillbox(int weekday = 0,
                           const std::string& period = "",
                           bool include_color = false);
    std::string GetPillboxPlan(const std::string& day = "today");
    std::string GetReminders(const std::string& date = "");
    std::string GetDueRoutines(int weekday = 0,
                               const std::string& time = "",
                               const std::string& mode = "now");
    std::string RecordRoutineExecution(const std::string& routine_id,
                                       const std::string& event = "confirmed",
                                       const std::string& message = "");
    std::string GetRoutineExecutionHistory(const std::string& routine_id = "");
    std::string GetDailyStatus(const std::string& date = "",
                               int weekday = 0,
                               const std::string& time = "",
                               const std::string& focus = "all");
    std::string GetPendingToday(const std::string& date = "",
                                int weekday = 0,
                                const std::string& time = "");
    std::string GetCompletedToday(const std::string& date = "",
                                  int weekday = 0,
                                  const std::string& time = "");
    std::string GetWeather(const std::string& city = "Buenos Aires",
                           const std::string& latitude = "",
                           const std::string& longitude = "");

    std::string FindFamilyMember(const std::string& query);
    std::string WhoIsFamilyMember(const std::string& query);
    std::string GetFamilyRelationships(const std::string& person = "");
    std::string GetPersonConnections(const std::string& person);
    std::string GetChildrenOf(const std::string& person = "profile");
    std::string GetGrandchildrenOf(const std::string& person = "profile");
    std::string GetPartnerOf(const std::string& person);

private:
    CareMcpService() = default;
};

}  // namespace xiaozhi_care
