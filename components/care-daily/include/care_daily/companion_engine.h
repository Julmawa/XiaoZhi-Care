#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "care_daily/day_activity_memory.h"
#include "care_daily/routine_execution.h"

namespace xiaozhi_care::daily {

/**
 * @brief What kind of accompaniment would be useful now.
 *
 * This is an internal decision only. It contains no conversational text.
 */
enum class CompanionAction : uint8_t {
    None,
    Prepare,
    CheckDue,
    CheckOverdue,
    ContinueActivity,
};

/**
 * @brief Internal reason for a companion suggestion.
 *
 * Reasons are machine-readable and must not be presented directly as
 * conversational phrases.
 */
enum class CompanionReason : uint8_t {
    None,
    RoutineUpcoming,
    RoutineDueUnconfirmed,
    RoutineOverdueUnconfirmed,
    ActiveDayActivity,
};

/**
 * @brief Normalized routine signal consumed by CompanionEngine.
 *
 * The caller is responsible for translating DailyRoutineEngine / scheduling
 * information into one of these states.
 *
 * minutes_until convention:
 *   > 0 : routine is in the future
 *   = 0 : scheduled now
 *   < 0 : scheduled time has passed
 *
 * Exactly one of due_now / overdue / upcoming must be true.
 */
struct CompanionRoutineCandidate {
    RoutineId routine_id;

    bool due_now{false};
    bool overdue{false};
    bool upcoming{false};

    int16_t minutes_until{0};

    [[nodiscard]] bool IsValid() const {
        const int state_count =
            (due_now ? 1 : 0) +
            (overdue ? 1 : 0) +
            (upcoming ? 1 : 0);

        if (routine_id.Empty() || state_count != 1) {
            return false;
        }

        if (overdue && minutes_until >= 0) {
            return false;
        }

        if (upcoming && minutes_until <= 0) {
            return false;
        }

        return true;
    }
};

/**
 * @brief Current conversational context supplied by the caller.
 *
 * DP035 first version is conservative: CARE may suggest accompaniment only
 * while a conversation is already active.
 */
struct CompanionContext {
    std::string iso_date;
    bool conversation_active{false};
    int64_t now_unix{0};

    [[nodiscard]] bool IsValidDate() const {
        return iso_date.size() == 10 &&
               iso_date[4] == '-' &&
               iso_date[7] == '-';
    }

    [[nodiscard]] bool IsValid() const {
        return IsValidDate();
    }
};

/**
 * @brief Deterministic result from CompanionEngine.
 */
struct CompanionSuggestion {
    CompanionAction action{CompanionAction::None};
    CompanionReason reason{CompanionReason::None};

    RoutineId routine_id;
    std::string memory_id;

    /**
     * True means that, if the person explicitly confirms completion,
     * an execution record may be appropriate.
     *
     * It never means that CARE may mark something completed automatically.
     */
    bool can_mark_completed{false};

    [[nodiscard]] bool HasSuggestion() const {
        return action != CompanionAction::None;
    }
};

/**
 * @brief Pure decision engine for everyday accompaniment.
 *
 * It does not:
 * - read clocks
 * - open repositories
 * - write DayActivityMemory
 * - write RoutineExecution
 * - call MCP
 * - generate conversational text
 *
 * It only decides what category of accompaniment is useful now.
 */
class CompanionEngine {
public:
    CompanionEngine() = default;

    [[nodiscard]] CompanionSuggestion Evaluate(
        const std::vector<CompanionRoutineCandidate>& routine_candidates,
        const std::vector<RoutineExecution>& executions,
        const std::vector<DayActivityMemory>& day_memories,
        const CompanionContext& context) const;

private:
    [[nodiscard]] bool WasConfirmedToday(
        const RoutineId& routine_id,
        const std::vector<RoutineExecution>& executions,
        const std::string& iso_date) const;

    [[nodiscard]] const DayActivityMemory* FindActiveDayMemory(
        const std::vector<DayActivityMemory>& day_memories,
        const CompanionContext& context) const;
};

const char* CompanionActionName(CompanionAction action);
const char* CompanionReasonName(CompanionReason reason);

}  // namespace xiaozhi_care::daily