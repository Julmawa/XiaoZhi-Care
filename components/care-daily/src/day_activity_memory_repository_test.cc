#include "care_daily/day_activity_memory_repository_test.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "care_daily/care_data_day_activity_memory_repository.h"
#include "esp_log.h"

namespace xiaozhi_care::daily {
namespace {

constexpr char kTag[] = "CARE_DAY_MEM_TEST";
constexpr char kTestPrefix[] = "DP035_TEST_";

bool IsTestMemory(const DayActivityMemory& memory) {
    return memory.activity.rfind(kTestPrefix, 0) == 0;
}

void CleanupTestMemories(
    CareDataDayActivityMemoryRepository* repository) {
    if (repository == nullptr) {
        return;
    }

    const std::vector<DayActivityMemory> memories =
        repository->List();

    for (const DayActivityMemory& memory : memories) {
        if (IsTestMemory(memory)) {
            repository->Delete(memory.memory_id);
        }
    }
}

bool ListContains(
    const std::vector<DayActivityMemory>& memories,
    const std::string& memory_id) {
    return std::any_of(
        memories.begin(),
        memories.end(),
        [&](const DayActivityMemory& memory) {
            return memory.memory_id == memory_id;
        });
}

void LogFail(const char* message) {
    ESP_LOGE(kTag, "TEST FAILED: %s", message);
}

}  // namespace

void RunDayActivityMemoryRepositoryTest() {
    ESP_LOGI(
        kTag,
        "DP035-r1 Day Activity Memory Repository test started");

    CareDataDayActivityMemoryRepository repository;

    if (!repository.Init()) {
        LogFail("repository init");
        return;
    }

    // Remove only leftovers created by previous runs of this test.
    CleanupTestMemories(&repository);

    // --------------------------------------------------
    // 1. Generate + Save
    // --------------------------------------------------

    DayActivityMemory old_memory;

    old_memory.memory_id = repository.GenerateId();

    if (old_memory.memory_id.empty()) {
        LogFail("GenerateId for old memory");
        return;
    }

    old_memory.iso_date = "2000-01-01";
    old_memory.activity = "DP035_TEST_OLD_ACTIVITY";
    old_memory.state = CompanionActivityState::Recall;
    old_memory.help_level = CompanionHelpLevel::OpenQuestion;

    old_memory.plan.value = "preparar una actividad";
    old_memory.plan.source = DayMemoryFactSource::UserRecalled;

    old_memory.current_step.value = "recordar el primer paso";
    old_memory.current_step.source =
        DayMemoryFactSource::UserConfirmed;

    DayActivityFact recalled;
    recalled.value = "primer paso recordado";
    recalled.source = DayMemoryFactSource::UserRecalled;
    old_memory.recalled_steps.push_back(recalled);

    DayActivityFact suggested;
    suggested.value = "pista sugerida por CARE";
    suggested.source = DayMemoryFactSource::CareSuggested;
    old_memory.recalled_steps.push_back(suggested);

    old_memory.last_interaction_unix = 946684800;

    if (!repository.Save(old_memory)) {
        LogFail("Save old memory");
        CleanupTestMemories(&repository);
        return;
    }

    ESP_LOGI(
        kTag,
        "SAVE: OK (%s)",
        old_memory.memory_id.c_str());

    // --------------------------------------------------
    // 2. Find + provenance validation
    // --------------------------------------------------

    std::optional<DayActivityMemory> loaded =
        repository.FindById(old_memory.memory_id);

    if (!loaded.has_value()) {
        LogFail("FindById after Save");
        CleanupTestMemories(&repository);
        return;
    }

    if (loaded->activity != old_memory.activity ||
        loaded->iso_date != old_memory.iso_date ||
        loaded->state != CompanionActivityState::Recall ||
        loaded->help_level != CompanionHelpLevel::OpenQuestion ||
        loaded->recalled_steps.size() != 2 ||
        loaded->recalled_steps[0].source !=
            DayMemoryFactSource::UserRecalled ||
        loaded->recalled_steps[1].source !=
            DayMemoryFactSource::CareSuggested) {
        LogFail("round-trip or provenance mismatch");
        CleanupTestMemories(&repository);
        return;
    }

    ESP_LOGI(kTag, "READ + PROVENANCE: OK");

    // --------------------------------------------------
    // 3. Replace existing record
    // --------------------------------------------------

    loaded->state = CompanionActivityState::Doing;
    loaded->help_level = CompanionHelpLevel::Hint;

    loaded->current_step.value = "continuar con la actividad";
    loaded->current_step.source =
        DayMemoryFactSource::UserRecalled;

    if (!repository.Save(loaded.value())) {
        LogFail("replace existing memory");
        CleanupTestMemories(&repository);
        return;
    }

    std::optional<DayActivityMemory> updated =
        repository.FindById(old_memory.memory_id);

    if (!updated.has_value() ||
        updated->state != CompanionActivityState::Doing ||
        updated->help_level != CompanionHelpLevel::Hint ||
        updated->current_step.value !=
            "continuar con la actividad") {
        LogFail("updated record mismatch");
        CleanupTestMemories(&repository);
        return;
    }

    ESP_LOGI(kTag, "UPDATE: OK");

    // --------------------------------------------------
    // 4. List
    // --------------------------------------------------

    std::vector<DayActivityMemory> all =
        repository.List();

    if (!ListContains(all, old_memory.memory_id)) {
        LogFail("List does not contain saved memory");
        CleanupTestMemories(&repository);
        return;
    }

    ESP_LOGI(
        kTag,
        "LIST: OK (%u total record(s))",
        static_cast<unsigned>(all.size()));

    // --------------------------------------------------
    // 5. Create a second record that must survive expiry
    //
    // Cleanup date is intentionally year 2000:
    // real 2026 records are "future" relative to it and therefore
    // are not expired by this test.
    // --------------------------------------------------

    DayActivityMemory current_memory;

    current_memory.memory_id = repository.GenerateId();

    if (current_memory.memory_id.empty() ||
        current_memory.memory_id == old_memory.memory_id) {
        LogFail("GenerateId for current memory");
        CleanupTestMemories(&repository);
        return;
    }

    current_memory.iso_date = "2000-01-02";
    current_memory.activity = "DP035_TEST_CURRENT_ACTIVITY";
    current_memory.state = CompanionActivityState::Discover;
    current_memory.help_level =
        CompanionHelpLevel::Independent;

    if (!repository.Save(current_memory)) {
        LogFail("Save current memory");
        CleanupTestMemories(&repository);
        return;
    }

    // --------------------------------------------------
    // 6. ClearExpired
    //
    // retention_days=0:
    // 2000-01-01 -> removed
    // 2000-01-02 -> retained
    // any 2026 real record -> retained
    // --------------------------------------------------

    const size_t removed =
        repository.ClearExpired("2000-01-02", 0);

    if (removed != 1) {
        ESP_LOGE(
            kTag,
            "TEST FAILED: expected 1 expired record, got %u",
            static_cast<unsigned>(removed));

        CleanupTestMemories(&repository);
        return;
    }

    if (repository.FindById(old_memory.memory_id).has_value()) {
        LogFail("expired record still exists");
        CleanupTestMemories(&repository);
        return;
    }

    if (!repository.FindById(
            current_memory.memory_id).has_value()) {
        LogFail("current record was incorrectly expired");
        CleanupTestMemories(&repository);
        return;
    }

    ESP_LOGI(kTag, "EXPIRATION: OK");

    // --------------------------------------------------
    // 7. Delete
    // --------------------------------------------------

    if (!repository.Delete(current_memory.memory_id)) {
        LogFail("Delete current memory");
        CleanupTestMemories(&repository);
        return;
    }

    if (repository.FindById(
            current_memory.memory_id).has_value()) {
        LogFail("deleted memory still exists");
        CleanupTestMemories(&repository);
        return;
    }

    ESP_LOGI(kTag, "DELETE: OK");

    // Final safety cleanup, only DP035_TEST_*.
    CleanupTestMemories(&repository);

    ESP_LOGI(
        kTag,
        "CREATE + READ + UPDATE + LIST + EXPIRE + DELETE: OK");

    ESP_LOGI(kTag, "TEST PASSED");
}

}  // namespace xiaozhi_care::daily