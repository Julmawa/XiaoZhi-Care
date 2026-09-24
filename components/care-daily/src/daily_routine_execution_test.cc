#include "care_daily/daily_routine_execution_test.h"

#include <optional>
#include <vector>

#include <esp_log.h>

#include "care_daily/nvs_routine_execution_repository.h"

namespace xiaozhi_care::daily {
namespace {

constexpr char kTag[] = "CARE_DAILY_EXEC_TEST";

void LogFail(const char* message) {
    ESP_LOGE(kTag, "%s", message);
}

}  // namespace

void RunDailyRoutineExecutionTest() {
    ESP_LOGI(kTag, "DP-006 Daily Routine Execution test started");

    NvsRoutineExecutionRepository repository;
    if (!repository.Init()) {
        LogFail("Repository init failed");
        return;
    }

    RoutineExecution execution;
    execution.id = repository.GenerateId();
    execution.routine_id = RoutineId("r000001");
    execution.event = RoutineExecutionEvent::Indicated;
    execution.source = RoutineExecutionSource::Test;
    execution.iso_date = "2026-09-08";
    execution.time = DailyTime(8, 15);
    execution.message = "Cosa indicada para prueba DP-006";
    execution.note = "Evento de validacion local";

    if (!execution.IsValid()) {
        LogFail("Generated execution is invalid");
        return;
    }

    if (!repository.Save(execution)) {
        LogFail("SAVE EXECUTION: FAILED");
        return;
    }
    ESP_LOGI(kTag, "SAVE EXECUTION: OK (%s)", execution.id.Str().c_str());

    std::optional<RoutineExecution> loaded = repository.FindById(execution.id);
    if (!loaded.has_value()) {
        LogFail("READ EXECUTION BY ID: FAILED");
        return;
    }
    if (loaded->routine_id != execution.routine_id || loaded->event != execution.event) {
        LogFail("READ EXECUTION BY ID: DATA MISMATCH");
        return;
    }
    ESP_LOGI(kTag, "READ EXECUTION BY ID: OK");

    std::vector<RoutineExecution> by_routine = repository.ListByRoutine(execution.routine_id);
    if (by_routine.empty()) {
        LogFail("LIST BY ROUTINE: FAILED");
        return;
    }
    ESP_LOGI(kTag, "LIST BY ROUTINE: OK (%u event(s))", static_cast<unsigned>(by_routine.size()));

    std::vector<RoutineExecution> all = repository.List();
    if (all.empty()) {
        LogFail("LIST EXECUTIONS: FAILED");
        return;
    }
    ESP_LOGI(kTag, "LIST EXECUTIONS: OK (%u total event(s))", static_cast<unsigned>(all.size()));

    ESP_LOGI(kTag, "PERSISTENCE CONFIRMED: execution event stored in NVS");
    ESP_LOGI(kTag, "TEST PASSED");
}

}  // namespace xiaozhi_care::daily
