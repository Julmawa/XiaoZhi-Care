#include "care_daily/daily_routine_engine_test.h"

#include <esp_log.h>

#include "care_daily/daily_routine_engine.h"

namespace xiaozhi_care::daily {
namespace {

constexpr char kTag[] = "CARE_DAILY_ENGINE_TEST";

CareRoutine MakeEngineTestRoutine() {
    CareRoutine routine;
    routine.id = RoutineId("r-eng-001");
    routine.type = RoutineType::Medication;
    routine.state = RoutineState::Active;
    routine.title = "ENGINE_TEST_MEDICATION";
    routine.description = "Cosa de prueba DP-003";
    routine.data = "{\"private\":true}";

    routine.schedule.time = DailyTime(7, 30);
    routine.schedule.repeat = RoutineRepeatType::Daily;
    routine.schedule.moment = RoutineMoment::Fasting;
    routine.schedule.interval_days = 1;
    routine.schedule.reminder_window_minutes = 30;

    routine.placement.type = PlacementType::Pillbox;
    routine.placement.description = "Pastillero semanal";
    routine.placement.compartment = "el primer casillero de la mañana";
    routine.placement.monitored = false;

    return routine;
}

bool ExpectDue(const DailyRoutineEngine& engine,
               const CareRoutine& routine,
               const RoutineEvaluationContext& context) {
    const RoutineEvaluation evaluation = engine.Evaluate(routine, context);
    if (!evaluation.IsDue()) {
        ESP_LOGE(kTag, "Expected DUE but got status=%u", static_cast<unsigned>(evaluation.status));
        return false;
    }

    ESP_LOGI(kTag, "EVALUATE EXACT TIME: DUE");
    ESP_LOGI(kTag, "SAFE MESSAGE: %s", evaluation.safe_message.c_str());
    return true;
}

bool ExpectNotDue(const char* label,
                  const DailyRoutineEngine& engine,
                  const CareRoutine& routine,
                  const RoutineEvaluationContext& context) {
    const RoutineEvaluation evaluation = engine.Evaluate(routine, context);
    if (evaluation.IsDue()) {
        ESP_LOGE(kTag, "Expected NOT_DUE for %s", label);
        return false;
    }

    ESP_LOGI(kTag, "%s: NOT_DUE", label);
    return true;
}

}  // namespace

void RunDailyRoutineEngineTest() {
    ESP_LOGI(kTag, "DP-003 Daily Routine Engine test started");

    DailyRoutineEngine engine;
    CareRoutine routine = MakeEngineTestRoutine();

    RoutineEvaluationContext due_context;
    due_context.iso_weekday = 1;
    due_context.now = DailyTime(7, 30);

    if (!ExpectDue(engine, routine, due_context)) {
        ESP_LOGE(kTag, "TEST FAILED");
        return;
    }

    RoutineEvaluationContext late_context;
    late_context.iso_weekday = 1;
    late_context.now = DailyTime(9, 15);

    if (!ExpectNotDue("EVALUATE OUTSIDE WINDOW", engine, routine, late_context)) {
        ESP_LOGE(kTag, "TEST FAILED");
        return;
    }

    routine.state = RoutineState::Paused;
    if (!ExpectNotDue("EVALUATE PAUSED", engine, routine, due_context)) {
        ESP_LOGE(kTag, "TEST FAILED");
        return;
    }

    routine.state = RoutineState::Active;
    routine.schedule.repeat = RoutineRepeatType::SpecificWeekdays;
    routine.schedule.weekdays = {{false, true, false, false, false, false, false}};

    if (!ExpectNotDue("EVALUATE WRONG WEEKDAY", engine, routine, due_context)) {
        ESP_LOGE(kTag, "TEST FAILED");
        return;
    }

    ESP_LOGI(kTag, "TEST PASSED");
}

}  // namespace xiaozhi_care::daily
