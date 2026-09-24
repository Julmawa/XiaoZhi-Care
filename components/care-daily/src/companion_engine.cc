#include "care_daily/companion_engine.h"

#include <cstdlib>
#include <limits>

namespace xiaozhi_care::daily {
namespace {

bool IsContinuableState(CompanionActivityState state) {
    switch (state) {
        case CompanionActivityState::Discover:
        case CompanionActivityState::Recall:
        case CompanionActivityState::CheckResources:
        case CompanionActivityState::Organize:
        case CompanionActivityState::Doing:
        case CompanionActivityState::Unknown:
            return true;

        case CompanionActivityState::Deferred:
            // Deferred is handled separately using defer_until_unix.
            return true;

        case CompanionActivityState::Completed:
        case CompanionActivityState::Cancelled:
            return false;

        default:
            return false;
    }
}

CompanionSuggestion MakeRoutineSuggestion(
    CompanionAction action,
    CompanionReason reason,
    const CompanionRoutineCandidate& candidate,
    bool can_mark_completed) {
    CompanionSuggestion suggestion;
    suggestion.action = action;
    suggestion.reason = reason;
    suggestion.routine_id = candidate.routine_id;
    suggestion.can_mark_completed = can_mark_completed;
    return suggestion;
}

}  // namespace

bool CompanionEngine::WasConfirmedToday(
    const RoutineId& routine_id,
    const std::vector<RoutineExecution>& executions,
    const std::string& iso_date) const {
    if (routine_id.Empty() || iso_date.empty()) {
        return false;
    }

    for (const RoutineExecution& execution : executions) {
        if (execution.routine_id == routine_id &&
            execution.iso_date == iso_date &&
            execution.event == RoutineExecutionEvent::Confirmed) {
            return true;
        }
    }

    return false;
}

const DayActivityMemory* CompanionEngine::FindActiveDayMemory(
    const std::vector<DayActivityMemory>& day_memories,
    const CompanionContext& context) const {
    const DayActivityMemory* best = nullptr;

    for (const DayActivityMemory& memory : day_memories) {
        if (!memory.IsValid() ||
            memory.iso_date != context.iso_date ||
            !IsContinuableState(memory.state)) {
            continue;
        }

        if (memory.state == CompanionActivityState::Deferred &&
            memory.defer_until_unix > 0 &&
            context.now_unix > 0 &&
            context.now_unix < memory.defer_until_unix) {
            continue;
        }

        if (best == nullptr ||
            memory.last_interaction_unix >
                best->last_interaction_unix) {
            best = &memory;
        }
    }

    return best;
}

CompanionSuggestion CompanionEngine::Evaluate(
    const std::vector<CompanionRoutineCandidate>& routine_candidates,
    const std::vector<RoutineExecution>& executions,
    const std::vector<DayActivityMemory>& day_memories,
    const CompanionContext& context) const {
    CompanionSuggestion none;

    if (!context.IsValid() ||
        !context.conversation_active) {
        return none;
    }

    // --------------------------------------------------
    // Priority 1: due now, not confirmed today.
    //
    // If several are due, prefer the one closest to its scheduled time.
    // --------------------------------------------------

    const CompanionRoutineCandidate* best_due = nullptr;
    int best_due_distance = std::numeric_limits<int>::max();

    for (const CompanionRoutineCandidate& candidate :
         routine_candidates) {
        if (!candidate.IsValid() ||
            !candidate.due_now ||
            WasConfirmedToday(
                candidate.routine_id,
                executions,
                context.iso_date)) {
            continue;
        }

        const int distance =
            std::abs(static_cast<int>(
                candidate.minutes_until));

        if (best_due == nullptr ||
            distance < best_due_distance) {
            best_due = &candidate;
            best_due_distance = distance;
        }
    }

    if (best_due != nullptr) {
        return MakeRoutineSuggestion(
            CompanionAction::CheckDue,
            CompanionReason::RoutineDueUnconfirmed,
            *best_due,
            true);
    }

    // --------------------------------------------------
    // Priority 2: recently overdue, not confirmed today.
    //
    // minutes_until is negative here. The value closest to zero is the
    // most recently overdue routine.
    // --------------------------------------------------

    const CompanionRoutineCandidate* best_overdue = nullptr;
    int16_t best_overdue_minutes =
        std::numeric_limits<int16_t>::min();

    for (const CompanionRoutineCandidate& candidate :
         routine_candidates) {
        if (!candidate.IsValid() ||
            !candidate.overdue ||
            WasConfirmedToday(
                candidate.routine_id,
                executions,
                context.iso_date)) {
            continue;
        }

        if (best_overdue == nullptr ||
            candidate.minutes_until >
                best_overdue_minutes) {
            best_overdue = &candidate;
            best_overdue_minutes =
                candidate.minutes_until;
        }
    }

    if (best_overdue != nullptr) {
        return MakeRoutineSuggestion(
            CompanionAction::CheckOverdue,
            CompanionReason::RoutineOverdueUnconfirmed,
            *best_overdue,
            true);
    }

    // --------------------------------------------------
    // Priority 3: continue a current day activity.
    // --------------------------------------------------

    const DayActivityMemory* active_memory =
        FindActiveDayMemory(
            day_memories,
            context);

    if (active_memory != nullptr) {
        CompanionSuggestion suggestion;

        suggestion.action =
            CompanionAction::ContinueActivity;

        suggestion.reason =
            CompanionReason::ActiveDayActivity;

        suggestion.memory_id =
            active_memory->memory_id;

        suggestion.routine_id =
            active_memory->routine_id;

        // General activities are never auto-completable merely because
        // they are being accompanied.
        suggestion.can_mark_completed = false;

        return suggestion;
    }

    // --------------------------------------------------
    // Priority 4: nearest upcoming routine.
    // --------------------------------------------------

    const CompanionRoutineCandidate* best_upcoming = nullptr;
    int16_t best_upcoming_minutes =
        std::numeric_limits<int16_t>::max();

    for (const CompanionRoutineCandidate& candidate :
         routine_candidates) {
        if (!candidate.IsValid() ||
            !candidate.upcoming ||
            WasConfirmedToday(
                candidate.routine_id,
                executions,
                context.iso_date)) {
            continue;
        }

        if (best_upcoming == nullptr ||
            candidate.minutes_until <
                best_upcoming_minutes) {
            best_upcoming = &candidate;
            best_upcoming_minutes =
                candidate.minutes_until;
        }
    }

    if (best_upcoming != nullptr) {
        return MakeRoutineSuggestion(
            CompanionAction::Prepare,
            CompanionReason::RoutineUpcoming,
            *best_upcoming,
            false);
    }

    return none;
}

const char* CompanionActionName(
    CompanionAction action) {
    switch (action) {
        case CompanionAction::None:
            return "none";
        case CompanionAction::Prepare:
            return "prepare";
        case CompanionAction::CheckDue:
            return "check_due";
        case CompanionAction::CheckOverdue:
            return "check_overdue";
        case CompanionAction::ContinueActivity:
            return "continue_activity";
        default:
            return "none";
    }
}

const char* CompanionReasonName(
    CompanionReason reason) {
    switch (reason) {
        case CompanionReason::None:
            return "none";
        case CompanionReason::RoutineUpcoming:
            return "routine_upcoming";
        case CompanionReason::RoutineDueUnconfirmed:
            return "routine_due_unconfirmed";
        case CompanionReason::RoutineOverdueUnconfirmed:
            return "routine_overdue_unconfirmed";
        case CompanionReason::ActiveDayActivity:
            return "active_day_activity";
        default:
            return "none";
    }
}

}  // namespace xiaozhi_care::daily