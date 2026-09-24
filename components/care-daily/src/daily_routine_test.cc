#include "care_daily/daily_routine_test.h"

#include <optional>
#include <vector>

#include <esp_log.h>

#include "care_daily/nvs_routine_repository.h"

namespace xiaozhi_care::daily {
namespace {

constexpr char kTag[] = "CARE_DAILY_TEST";
constexpr char kTestTitle[] = "CARE_DAILY_TEST_ROUTINE";

std::optional<CareRoutine> FindExistingTestRoutine(NvsRoutineRepository* repository) {
    if (repository == nullptr) {
        return std::nullopt;
    }

    std::vector<CareRoutine> routines = repository->List();
    for (const CareRoutine& routine : routines) {
        if (routine.title == kTestTitle) {
            return routine;
        }
    }

    return std::nullopt;
}

CareRoutine MakeTestRoutine(const RoutineId& id) {
    CareRoutine routine;
    routine.id = id;
    routine.type = RoutineType::Medication;
    routine.state = RoutineState::Active;
    routine.title = kTestTitle;
    routine.description = "Cosa de prueba DP-002";
    routine.data = "{\"visual\":\"pastilla ovalada amarilla\",\"speak_drug_name\":false}";

    routine.schedule.time = DailyTime(7, 30);
    routine.schedule.repeat = RoutineRepeatType::Daily;
    routine.schedule.moment = RoutineMoment::Fasting;
    routine.schedule.interval_days = 1;
    routine.schedule.reminder_window_minutes = 30;

    routine.placement.type = PlacementType::Pillbox;
    routine.placement.description = "Pastillero semanal";
    routine.placement.compartment = "Primer casillero de la mañana";
    routine.placement.monitored = false;
    routine.placement.ble_slot = 0;
    routine.placement.led_number = 0;

    return routine;
}

bool ValidateTestRoutine(const CareRoutine& routine) {
    if (!routine.IsValid()) {
        ESP_LOGE(kTag, "Validation failed: routine is invalid");
        return false;
    }

    if (routine.title != kTestTitle) {
        ESP_LOGE(kTag, "Validation failed: title mismatch");
        return false;
    }

    if (routine.schedule.time != DailyTime(7, 30)) {
        ESP_LOGE(kTag, "Validation failed: time mismatch");
        return false;
    }

    if (routine.placement.type != PlacementType::Pillbox) {
        ESP_LOGE(kTag, "Validation failed: placement type mismatch");
        return false;
    }

    if (routine.placement.compartment.empty()) {
        ESP_LOGE(kTag, "Validation failed: missing compartment");
        return false;
    }

    return true;
}

}  // namespace

void RunDailyRoutinePersistenceTest() {
    ESP_LOGI(kTag, "DP-002 Daily Routine Repository test started");

    NvsRoutineRepository repository;
    if (!repository.Init()) {
        ESP_LOGE(kTag, "TEST FAILED: repository init failed");
        return;
    }

    std::optional<CareRoutine> existing = FindExistingTestRoutine(&repository);
    if (existing.has_value()) {
        ESP_LOGI(kTag, "Existing test routine found before write (%s)", existing->id.Str().c_str());
        if (ValidateTestRoutine(existing.value())) {
            ESP_LOGI(kTag, "PERSISTENCE CONFIRMED: routine survived reboot / reflashing with NVS preserved");
            ESP_LOGI(kTag, "TEST PASSED");
        } else {
            ESP_LOGE(kTag, "TEST FAILED: existing routine is corrupted");
        }
        return;
    }

    ESP_LOGI(kTag, "No existing test routine found; creating one");

    RoutineId id = repository.GenerateId();
    if (id.Empty()) {
        ESP_LOGE(kTag, "TEST FAILED: id generation failed");
        return;
    }

    CareRoutine routine = MakeTestRoutine(id);
    if (!repository.Save(routine)) {
        ESP_LOGE(kTag, "TEST FAILED: save failed");
        return;
    }

    std::optional<CareRoutine> loaded = repository.FindById(id);
    if (!loaded.has_value()) {
        ESP_LOGE(kTag, "TEST FAILED: load after save failed");
        return;
    }

    if (!ValidateTestRoutine(loaded.value())) {
        ESP_LOGE(kTag, "TEST FAILED: loaded routine validation failed");
        return;
    }

    std::vector<CareRoutine> routines = repository.List();
    ESP_LOGI(kTag, "LIST ROUTINES: OK (%u total routine(s))", static_cast<unsigned>(routines.size()));
    ESP_LOGI(kTag, "SAVE + LOAD: OK (%s)", id.Str().c_str());
    ESP_LOGI(kTag, "TEST PASSED");
    ESP_LOGI(kTag, "Reboot once without erase-flash to confirm persistence");
}

}  // namespace xiaozhi_care::daily
