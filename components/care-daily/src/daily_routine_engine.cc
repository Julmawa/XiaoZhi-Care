#include "care_daily/daily_routine_engine.h"

#include <cstdlib>

namespace xiaozhi_care::daily {
namespace {

int TimeDiffMinutes(const DailyTime& now, const DailyTime& scheduled) {
    return static_cast<int>(now.ToMinutes()) - static_cast<int>(scheduled.ToMinutes());
}

bool IsWithinWindow(int minutes_from_schedule, uint16_t window_minutes) {
    return std::abs(minutes_from_schedule) <= static_cast<int>(window_minutes);
}

const char* MomentText(RoutineMoment moment) {
    switch (moment) {
        case RoutineMoment::Fasting:
            return " en ayunas";
        case RoutineMoment::BeforeBreakfast:
            return " antes del desayuno";
        case RoutineMoment::WithBreakfast:
            return " con el desayuno";
        case RoutineMoment::AfterBreakfast:
            return " después del desayuno";
        case RoutineMoment::BeforeLunch:
            return " antes del almuerzo";
        case RoutineMoment::WithLunch:
            return " con el almuerzo";
        case RoutineMoment::AfterLunch:
            return " después del almuerzo";
        case RoutineMoment::BeforeDinner:
            return " antes de la cena";
        case RoutineMoment::WithDinner:
            return " con la cena";
        case RoutineMoment::AfterDinner:
            return " después de la cena";
        case RoutineMoment::Bedtime:
            return " antes de dormir";
        case RoutineMoment::Anytime:
        default:
            return "";
    }
}

}  // namespace

RoutineEvaluation DailyRoutineEngine::Evaluate(const CareRoutine& routine,
                                               const RoutineEvaluationContext& context) const {
    RoutineEvaluation evaluation;
    evaluation.routine_id = routine.id;

    if (!context.IsValid()) {
        evaluation.status = RoutineDueStatus::InvalidContext;
        return evaluation;
    }

    if (!routine.IsValid()) {
        evaluation.status = RoutineDueStatus::InvalidRoutine;
        return evaluation;
    }

    if (routine.state == RoutineState::Paused) {
        evaluation.status = RoutineDueStatus::Paused;
        return evaluation;
    }

    if (routine.state == RoutineState::Archived) {
        evaluation.status = RoutineDueStatus::Archived;
        return evaluation;
    }

    if (routine.schedule.repeat == RoutineRepeatType::AsNeeded) {
        evaluation.status = RoutineDueStatus::AsNeeded;
        return evaluation;
    }

    if (routine.schedule.repeat == RoutineRepeatType::EveryNDays) {
        evaluation.status = RoutineDueStatus::UnsupportedRepeat;
        return evaluation;
    }

    if (!IsRoutineForWeekday(routine, context.iso_weekday)) {
        evaluation.status = RoutineDueStatus::NotDue;
        return evaluation;
    }

    const int delta = TimeDiffMinutes(context.now, routine.schedule.time);
    evaluation.minutes_from_schedule = static_cast<int16_t>(delta);

    if (IsWithinWindow(delta, routine.schedule.reminder_window_minutes)) {
        evaluation.status = RoutineDueStatus::Due;
        evaluation.safe_message = BuildSafeMessage(routine);
        return evaluation;
    }

    evaluation.status = RoutineDueStatus::NotDue;
    return evaluation;
}

std::vector<RoutineEvaluation> DailyRoutineEngine::EvaluateAll(const std::vector<CareRoutine>& routines,
                                                               const RoutineEvaluationContext& context) const {
    std::vector<RoutineEvaluation> evaluations;
    evaluations.reserve(routines.size());

    for (const CareRoutine& routine : routines) {
        evaluations.push_back(Evaluate(routine, context));
    }

    return evaluations;
}

std::vector<CareRoutine> DailyRoutineEngine::FindDueRoutines(const std::vector<CareRoutine>& routines,
                                                             const RoutineEvaluationContext& context) const {
    std::vector<CareRoutine> due;

    for (const CareRoutine& routine : routines) {
        const RoutineEvaluation evaluation = Evaluate(routine, context);
        if (evaluation.IsDue()) {
            due.push_back(routine);
        }
    }

    return due;
}

bool DailyRoutineEngine::IsRoutineForWeekday(const CareRoutine& routine,
                                             uint8_t iso_weekday) const {
    if (iso_weekday < 1 || iso_weekday > 7) {
        return false;
    }

    switch (routine.schedule.repeat) {
        case RoutineRepeatType::Daily:
            return true;

        case RoutineRepeatType::SpecificWeekdays: {
            const size_t index = static_cast<size_t>(iso_weekday - 1);
            return routine.schedule.weekdays[index];
        }

        case RoutineRepeatType::AsNeeded:
        case RoutineRepeatType::EveryNDays:
        default:
            return false;
    }
}

std::string DailyRoutineEngine::BuildSafeMessage(const CareRoutine& routine) const {
    std::string message;

    if (routine.type == RoutineType::Medication) {
        message = "Ahora te toca esto que esta anotado";
        message += MomentText(routine.schedule.moment);

        if (!routine.placement.compartment.empty()) {
            message += ". Está en ";
            message += routine.placement.compartment;
        } else if (!routine.placement.description.empty()) {
            message += ". Está en ";
            message += routine.placement.description;
        }

        message += ".";
        return message;
    }

    message = "Ahora corresponde: ";
    message += routine.title;
    message += ".";
    return message;
}

}  // namespace xiaozhi_care::daily
