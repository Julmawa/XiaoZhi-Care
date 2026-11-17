#include "care_daily/care_alarm_scheduler.h"

#include <algorithm>
#include <string>
#include <vector>

#include <esp_log.h>

namespace xiaozhi_care::daily {
namespace {

constexpr char kTag[] = "CARE_ALARM";

int MinutesDiff(DailyTime a, DailyTime b) {
    return static_cast<int>(a.ToMinutes()) - static_cast<int>(b.ToMinutes());
}

bool IsSameIndication(const RoutineExecution& execution,
                      const RoutineId& routine_id,
                      const std::string& iso_date) {
    return execution.routine_id == routine_id &&
           execution.iso_date == iso_date &&
           execution.event == RoutineExecutionEvent::Indicated;
}

}  // namespace

CareAlarmScheduler::CareAlarmScheduler(RoutineRepository& routines,
                                       RoutineExecutionRepository& executions)
    : routines_(routines), executions_(executions) {}

bool CareAlarmScheduler::AlreadyConfirmedToday(const std::vector<RoutineExecution>& items, const RoutineId& routine_id,
                                               const std::string& iso_date) const {
    return std::any_of(items.begin(), items.end(), [&](const RoutineExecution& execution) {
        return execution.iso_date == iso_date && execution.event == RoutineExecutionEvent::Confirmed;
    });
}

uint8_t CareAlarmScheduler::CountIndicatedToday(const std::vector<RoutineExecution>& items, const RoutineId& routine_id,
                                                const std::string& iso_date) const {
    uint16_t count = 0;

    for (const RoutineExecution& execution : items) {
        if (IsSameIndication(execution, routine_id, iso_date)) {
            ++count;
        }
    }

    for (const RoutineExecution& execution : volatile_indications_) {
        if (IsSameIndication(execution, routine_id, iso_date)) {
            ++count;
        }
    }

    return count > 255 ? 255 : static_cast<uint8_t>(count);
}

DailyTime CareAlarmScheduler::LastIndicatedTimeToday(const std::vector<RoutineExecution>& items, const RoutineId& routine_id,
                                                     const std::string& iso_date,
                                                     bool& found) const {
    found = false;
    DailyTime last{};

    const auto consider = [&](const RoutineExecution& execution) {
        if (IsSameIndication(execution, routine_id, iso_date) && execution.time.IsValid()) {
            if (!found || execution.time > last) {
                last = execution.time;
                found = true;
            }
        }
    };

    for (const RoutineExecution& execution : items) {
        consider(execution);
    }

    for (const RoutineExecution& execution : volatile_indications_) {
        consider(execution);
    }

    return last;
}

RoutineExecution CareAlarmScheduler::BuildIndicatedExecution(const CareRoutine& routine,
                                                             const RoutineEvaluationContext& context,
                                                             const std::string& iso_date,
                                                             const std::string& safe_message) {
    RoutineExecution execution;
    execution.id = executions_.GenerateId();
    execution.routine_id = routine.id;
    execution.event = RoutineExecutionEvent::Indicated;
    execution.source = RoutineExecutionSource::Engine;
    execution.iso_date = iso_date;
    execution.time = context.now;
    execution.message = safe_message;
    execution.note = "CareAlarmScheduler";
    return execution;
}

bool CareAlarmScheduler::RecordIndicated(const CareRoutine& routine,
                                         const RoutineEvaluationContext& context,
                                         const std::string& iso_date,
                                         const std::string& safe_message,
                                         RoutineExecution& execution) {
    execution = BuildIndicatedExecution(routine, context, iso_date, safe_message);

    if (!execution.IsValid()) {
        ESP_LOGW(kTag, "Refusing to save invalid alarm execution");
        return false;
    }

    return executions_.Save(execution);
}

bool CareAlarmScheduler::ReportPresented(const CareAlarmAction& action,
                                       const RoutineEvaluationContext& context,
                                       const std::string& date) {
    if (date!=offered_date_ || !(context.now==offered_time_) || !action.presentation_token) return false;
    const auto offered=std::find_if(offered_.begin(),offered_.end(),[&](const auto& entry){
        return entry.token==action.presentation_token && entry.id==action.routine_id;
    });
    if (offered==offered_.end()) return false;
    offered_.erase(offered);
    CareRoutine routine;
    routine.id = action.routine_id;
    RoutineExecution execution;
    if (RecordIndicated(routine,context,date,action.safe_message,execution)) return true;
    if (execution.IsValid()) {
        volatile_indications_.erase(
            std::remove_if(volatile_indications_.begin(),volatile_indications_.end(),
                [&](const auto& old){ return old.iso_date != date; }), volatile_indications_.end());
        if (volatile_indications_.size() < 1024) volatile_indications_.push_back(std::move(execution));
    }
    return false;
}

CareAlarmSchedulerResult CareAlarmScheduler::Tick(const RoutineEvaluationContext& context,
                                                  const std::string& iso_date) {
    return TickInternal(routines_.List(), context, iso_date, nullptr);
}

CareAlarmSchedulerResult CareAlarmScheduler::TickForRoutine(const RoutineEvaluationContext& context,
                                                            const std::string& iso_date,
                                                            const RoutineId& only_routine_id) {
    return TickInternal(routines_.List(), context, iso_date, &only_routine_id);
}

CareAlarmSchedulerResult CareAlarmScheduler::TickRoutines(const std::vector<CareRoutine>& routines,
                                                          const RoutineEvaluationContext& context,
                                                          const std::string& iso_date) {
    return TickInternal(routines, context, iso_date, nullptr);
}

CareAlarmSchedulerResult CareAlarmScheduler::TickInternal(const std::vector<CareRoutine>& routines,
                                                          const RoutineEvaluationContext& context,
                                                          const std::string& iso_date,
                                                          const RoutineId* only_routine_id) {
    CareAlarmSchedulerResult result;
    offered_.clear();
    offered_date_=iso_date;
    offered_time_=context.now;

    if (!context.IsValid() || iso_date.size() != 10) {
        ESP_LOGW(kTag, "Invalid scheduler context");
        return result;
    }

    const std::vector<RoutineEvaluation> evaluations = engine_.EvaluateAll(routines, context);

    for (const RoutineEvaluation& evaluation : evaluations) {
        if (!evaluation.IsDue()) {
            continue;
        }

        auto routine_it = std::find_if(routines.begin(), routines.end(), [&](const CareRoutine& routine) {
            return routine.id == evaluation.routine_id;
        });
        if (routine_it == routines.end()) {
            ++result.suppressed_count;
            continue;
        }

        const CareRoutine& routine = *routine_it;
        if (only_routine_id != nullptr && routine.id != *only_routine_id) {
            continue;
        }

        ++result.due_count;
        if (!routine.alert.IsValid() || !routine.alert.IsEnabled()) {
            ++result.suppressed_count;
            continue;
        }

        const auto history = executions_.ListByRoutine(routine.id);
        if (AlreadyConfirmedToday(history, routine.id, iso_date)) {
            ++result.suppressed_count;
            continue;
        }

        const uint8_t indicated_count = CountIndicatedToday(history, routine.id, iso_date);
        if (indicated_count >= routine.alert.max_repeats) {
            ++result.suppressed_count;
            continue;
        }

        bool found_last = false;
        const DailyTime last = LastIndicatedTimeToday(history, routine.id, iso_date, found_last);
        if (found_last && MinutesDiff(context.now, last) < static_cast<int>(routine.alert.repeat_minutes)) {
            ++result.suppressed_count;
            continue;
        }

        CareAlarmAction action;
        action.routine_id = routine.id;
        action.mode = routine.alert.mode;
        action.safe_message = evaluation.safe_message;
        action.visual = routine.alert.UsesVisual();
        action.sound = routine.alert.UsesSound();
        action.voice = routine.alert.UsesVoice();
        action.visual_pattern = routine.alert.visual_pattern;
        action.visual_color = routine.alert.visual_color;
        action.sound_pattern = routine.alert.sound_pattern;
        action.should_notify = true;

        action.presentation_token=++next_token_;
        offered_.push_back({action.presentation_token,action.routine_id});
        result.actions.push_back(action);
        ESP_LOGI(kTag,
                 "Alarm action queued: routine=%s visual=%d sound=%d voice=%d saved=%d visual_pattern=%d visual_color=%d sound_pattern=%d",
                 routine.id.Str().c_str(),
                 action.visual ? 1 : 0,
                 action.sound ? 1 : 0,
                 action.voice ? 1 : 0,
                 action.execution_saved ? 1 : 0,
                 static_cast<int>(action.visual_pattern),
                 static_cast<int>(action.visual_color),
                 static_cast<int>(action.sound_pattern));
    }

    return result;
}

}  // namespace xiaozhi_care::daily
