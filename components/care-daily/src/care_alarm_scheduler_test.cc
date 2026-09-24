#include "care_daily/care_alarm_scheduler_test.h"

#include "care_daily/care_alarm_scheduler.h"

#include <algorithm>
#include <optional>
#include <cstdio>
#include <string>
#include <vector>

#include <esp_log.h>

namespace xiaozhi_care::daily {
namespace {

constexpr char kTag[] = "CARE_ALARM_TEST";
constexpr char kTestDate[] = "2026-09-08";

class MemoryRoutineRepository final : public RoutineRepository {
public:
    bool Init() override { return true; }

    RoutineId GenerateId() override {
        ++next_id_;
        char buffer[16] = {};
        std::snprintf(buffer, sizeof(buffer), "rmem%03u", static_cast<unsigned>(next_id_));
        return RoutineId(buffer);
    }

    bool Save(const CareRoutine& routine) override {
        if (!routine.IsValid()) {
            return false;
        }
        Delete(routine.id);
        routines_.push_back(routine);
        return true;
    }

    std::optional<CareRoutine> FindById(const RoutineId& id) override {
        const auto it = std::find_if(routines_.begin(), routines_.end(), [&](const CareRoutine& routine) {
            return routine.id == id;
        });
        if (it == routines_.end()) {
            return std::nullopt;
        }
        return *it;
    }

    std::vector<CareRoutine> List() override {
        return routines_;
    }

    bool Delete(const RoutineId& id) override {
        const auto before = routines_.size();
        routines_.erase(std::remove_if(routines_.begin(), routines_.end(), [&](const CareRoutine& routine) {
            return routine.id == id;
        }), routines_.end());
        return routines_.size() != before;
    }

private:
    uint16_t next_id_{0};
    std::vector<CareRoutine> routines_;
};

class MemoryExecutionRepository : public RoutineExecutionRepository {
public:
    bool Init() override { return true; }

    RoutineExecutionId GenerateId() override {
        ++next_id_;
        char buffer[16] = {};
        std::snprintf(buffer, sizeof(buffer), "emem%03u", static_cast<unsigned>(next_id_));
        return RoutineExecutionId(buffer);
    }

    bool Save(const RoutineExecution& execution) override {
        if (!execution.IsValid()) {
            return false;
        }
        Delete(execution.id);
        executions_.push_back(execution);
        return true;
    }

    std::optional<RoutineExecution> FindById(const RoutineExecutionId& id) override {
        const auto it = std::find_if(executions_.begin(), executions_.end(), [&](const RoutineExecution& execution) {
            return execution.id == id;
        });
        if (it == executions_.end()) {
            return std::nullopt;
        }
        return *it;
    }

    std::vector<RoutineExecution> List() override {
        return executions_;
    }

    std::vector<RoutineExecution> ListByRoutine(const RoutineId& routine_id) override {
        std::vector<RoutineExecution> result;
        for (const RoutineExecution& execution : executions_) {
            if (execution.routine_id == routine_id) {
                result.push_back(execution);
            }
        }
        return result;
    }

    bool Delete(const RoutineExecutionId& id) override {
        const auto before = executions_.size();
        executions_.erase(std::remove_if(executions_.begin(), executions_.end(), [&](const RoutineExecution& execution) {
            return execution.id == id;
        }), executions_.end());
        return executions_.size() != before;
    }

private:
    uint16_t next_id_{0};
    std::vector<RoutineExecution> executions_;
};

class FailingExecutionRepository final : public MemoryExecutionRepository {
public:
    bool Save(const RoutineExecution& execution) override {
        (void)execution;
        return false;
    }
};

CareRoutine BuildTestRoutine(const RoutineId& id) {
    CareRoutine routine;
    routine.id = id;
    routine.type = RoutineType::Medication;
    routine.state = RoutineState::Active;
    routine.title = "DP-012 aviso de prueba";
    routine.schedule.time = DailyTime(9, 30);
    routine.schedule.repeat = RoutineRepeatType::SpecificWeekdays;
    routine.schedule.weekdays = {{false, true, false, false, false, false, false}};
    routine.schedule.moment = RoutineMoment::BeforeBreakfast;
    routine.schedule.reminder_window_minutes = 30;
    routine.placement.type = PlacementType::Pillbox;
    routine.placement.compartment = "primer casillero";
    routine.alert.enabled = true;
    routine.alert.mode = CareAlertMode::VisualAndSound;
    routine.alert.repeat_minutes = 10;
    routine.alert.max_repeats = 3;
    return routine;
}

bool ValidateNormalScheduler() {
    MemoryRoutineRepository routines;
    MemoryExecutionRepository executions;
    routines.Init();
    executions.Init();

    const RoutineId id("r999012");
    const CareRoutine routine = BuildTestRoutine(id);

    CareAlarmScheduler scheduler(routines, executions);
    RoutineEvaluationContext context;
    context.iso_weekday = 2;
    context.now = DailyTime(9, 30);

    const std::vector<CareRoutine> test_routines{routine};

    const CareAlarmSchedulerResult first = scheduler.TickRoutines(test_routines, context, kTestDate);
    ESP_LOGI(kTag, "FIRST TICK: due=%u notified=%u suppressed=%u storage_errors=%u",
             static_cast<unsigned>(first.due_count),
             static_cast<unsigned>(first.notified_count),
             static_cast<unsigned>(first.suppressed_count),
             static_cast<unsigned>(first.storage_error_count));

    const CareAlarmSchedulerResult second = scheduler.TickRoutines(test_routines, context, kTestDate);
    ESP_LOGI(kTag, "SECOND TICK: due=%u notified=%u suppressed=%u storage_errors=%u",
             static_cast<unsigned>(second.due_count),
             static_cast<unsigned>(second.notified_count),
             static_cast<unsigned>(second.suppressed_count),
             static_cast<unsigned>(second.storage_error_count));

    return first.due_count == 1 &&
           first.notified_count == 1 &&
           first.suppressed_count == 0 &&
           first.storage_error_count == 0 &&
           second.due_count == 1 &&
           second.notified_count == 0 &&
           second.suppressed_count == 1 &&
           second.storage_error_count == 0;
}

bool ValidateStorageFailureDoesNotBlockAlarm() {
    MemoryRoutineRepository routines;
    FailingExecutionRepository executions;
    routines.Init();
    executions.Init();

    const RoutineId id("r999013");
    const CareRoutine routine = BuildTestRoutine(id);

    CareAlarmScheduler scheduler(routines, executions);
    RoutineEvaluationContext context;
    context.iso_weekday = 2;
    context.now = DailyTime(9, 30);

    const std::vector<CareRoutine> test_routines{routine};

    const CareAlarmSchedulerResult first = scheduler.TickRoutines(test_routines, context, kTestDate);
    ESP_LOGI(kTag, "STORAGE FAILURE TICK: due=%u notified=%u suppressed=%u storage_errors=%u",
             static_cast<unsigned>(first.due_count),
             static_cast<unsigned>(first.notified_count),
             static_cast<unsigned>(first.suppressed_count),
             static_cast<unsigned>(first.storage_error_count));

    const CareAlarmSchedulerResult second = scheduler.TickRoutines(test_routines, context, kTestDate);
    ESP_LOGI(kTag, "VOLATILE SUPPRESSION TICK: due=%u notified=%u suppressed=%u storage_errors=%u",
             static_cast<unsigned>(second.due_count),
             static_cast<unsigned>(second.notified_count),
             static_cast<unsigned>(second.suppressed_count),
             static_cast<unsigned>(second.storage_error_count));

    return first.due_count == 1 &&
           first.notified_count == 1 &&
           first.suppressed_count == 0 &&
           first.storage_error_count == 1 &&
           second.due_count == 1 &&
           second.notified_count == 0 &&
           second.suppressed_count == 1;
}

}  // namespace

void RunCareAlarmSchedulerTest() {
    ESP_LOGI(kTag, "DP-012-r3 Care Alarm Scheduler NVS-safe test started");

    if (!ValidateNormalScheduler()) {
        ESP_LOGE(kTag, "NORMAL SCHEDULER TEST: FAILED");
        return;
    }
    ESP_LOGI(kTag, "NORMAL SCHEDULER TEST: OK");

    if (!ValidateStorageFailureDoesNotBlockAlarm()) {
        ESP_LOGE(kTag, "STORAGE FAILURE RESILIENCE TEST: FAILED");
        return;
    }
    ESP_LOGI(kTag, "STORAGE FAILURE RESILIENCE TEST: OK");

    ESP_LOGI(kTag, "DUPLICATE SUPPRESSION: OK");
    ESP_LOGI(kTag, "TEST PASSED");
}

}  // namespace xiaozhi_care::daily
