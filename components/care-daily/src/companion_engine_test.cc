#include "care_daily/companion_engine_test.h"

#include <vector>

#include "care_daily/companion_engine.h"
#include "esp_log.h"

namespace xiaozhi_care::daily {
namespace {

constexpr char kTag[] = "CARE_COMPANION_TEST";
constexpr char kDate[] = "2026-09-17";

CompanionRoutineCandidate MakeCandidate(
    const char* id,
    bool due,
    bool overdue,
    bool upcoming,
    int16_t minutes_until) {
    CompanionRoutineCandidate candidate;
    candidate.routine_id = RoutineId(id);
    candidate.due_now = due;
    candidate.overdue = overdue;
    candidate.upcoming = upcoming;
    candidate.minutes_until = minutes_until;
    return candidate;
}

RoutineExecution MakeConfirmed(
    const char* execution_id,
    const char* routine_id) {
    RoutineExecution execution;
    execution.id =
        RoutineExecutionId(execution_id);
    execution.routine_id =
        RoutineId(routine_id);
    execution.event =
        RoutineExecutionEvent::Confirmed;
    execution.source =
        RoutineExecutionSource::Test;
    execution.iso_date = kDate;
    execution.time = DailyTime(12, 0);
    return execution;
}

DayActivityMemory MakeActiveMemory() {
    DayActivityMemory memory;
    memory.memory_id = "a999901";
    memory.iso_date = kDate;
    memory.activity = "DP035_TEST_ACTIVITY";
    memory.state = CompanionActivityState::Recall;
    memory.help_level =
        CompanionHelpLevel::OpenQuestion;
    memory.last_interaction_unix = 1000;
    return memory;
}

bool ExpectAction(
    const char* label,
    const CompanionSuggestion& suggestion,
    CompanionAction expected_action,
    const char* expected_routine_id,
    const char* expected_memory_id) {
    if (suggestion.action != expected_action) {
        ESP_LOGE(
            kTag,
            "%s FAILED: action=%s",
            label,
            CompanionActionName(suggestion.action));
        return false;
    }

    const std::string expected_routine =
        expected_routine_id != nullptr
            ? expected_routine_id
            : "";

    const std::string expected_memory =
        expected_memory_id != nullptr
            ? expected_memory_id
            : "";

    if (suggestion.routine_id.Str() !=
            expected_routine ||
        suggestion.memory_id !=
            expected_memory) {
        ESP_LOGE(
            kTag,
            "%s FAILED: target mismatch",
            label);
        return false;
    }

    ESP_LOGI(
        kTag,
        "%s: OK (%s)",
        label,
        CompanionActionName(suggestion.action));

    return true;
}

}  // namespace

void RunCompanionEngineTest() {
    ESP_LOGI(
        kTag,
        "DP035-r2 Companion Engine test started");

    CompanionEngine engine;

    CompanionContext context;
    context.iso_date = kDate;
    context.conversation_active = true;
    context.now_unix = 2000;

    const CompanionRoutineCandidate due =
        MakeCandidate(
            "r900001",
            true, false, false,
            0);

    const CompanionRoutineCandidate overdue =
        MakeCandidate(
            "r900002",
            false, true, false,
            -10);

    const CompanionRoutineCandidate upcoming_far =
        MakeCandidate(
            "r900003",
            false, false, true,
            90);

    const CompanionRoutineCandidate upcoming_near =
        MakeCandidate(
            "r900004",
            false, false, true,
            30);

    const std::vector<CompanionRoutineCandidate> candidates{
        upcoming_far,
        overdue,
        upcoming_near,
        due,
    };

    const DayActivityMemory active =
        MakeActiveMemory();

    // 1. No spontaneous accompaniment outside an active conversation.
    CompanionContext inactive = context;
    inactive.conversation_active = false;

    if (!ExpectAction(
            "INACTIVE CONVERSATION",
            engine.Evaluate(
                candidates,
                {},
                {active},
                inactive),
            CompanionAction::None,
            "",
            "")) {
        return;
    }

    // 2. Due wins over overdue, activity and upcoming.
    CompanionSuggestion suggestion =
        engine.Evaluate(
            candidates,
            {},
            {active},
            context);

    if (!ExpectAction(
            "DUE PRIORITY",
            suggestion,
            CompanionAction::CheckDue,
            "r900001",
            "")) {
        return;
    }

    if (!suggestion.can_mark_completed) {
        ESP_LOGE(
            kTag,
            "DUE PRIORITY FAILED: can_mark_completed=false");
        return;
    }

    // 3. Once due routine is explicitly confirmed, overdue wins.
    const RoutineExecution due_confirmed =
        MakeConfirmed(
            "e900001",
            "r900001");

    suggestion = engine.Evaluate(
        candidates,
        {due_confirmed},
        {active},
        context);

    if (!ExpectAction(
            "OVERDUE PRIORITY",
            suggestion,
            CompanionAction::CheckOverdue,
            "r900002",
            "")) {
        return;
    }

    // 4. Once both are confirmed, continue current activity.
    const RoutineExecution overdue_confirmed =
        MakeConfirmed(
            "e900002",
            "r900002");

    suggestion = engine.Evaluate(
        candidates,
        {due_confirmed, overdue_confirmed},
        {active},
        context);

    if (!ExpectAction(
            "CONTINUE ACTIVITY",
            suggestion,
            CompanionAction::ContinueActivity,
            "",
            "a999901")) {
        return;
    }

    if (suggestion.can_mark_completed) {
        ESP_LOGE(
            kTag,
            "CONTINUE ACTIVITY FAILED: auto-completion allowed");
        return;
    }

    // 5. Deferred activity must stay quiet until its defer time.
    DayActivityMemory deferred = active;
    deferred.state =
        CompanionActivityState::Deferred;
    deferred.defer_until_unix = 3000;

    suggestion = engine.Evaluate(
        candidates,
        {due_confirmed, overdue_confirmed},
        {deferred},
        context);

    if (!ExpectAction(
            "DEFERRED -> PREPARE",
            suggestion,
            CompanionAction::Prepare,
            "r900004",
            "")) {
        return;
    }

    // 6. Nearest upcoming routine wins.
    if (suggestion.can_mark_completed) {
        ESP_LOGE(
            kTag,
            "PREPARE FAILED: can_mark_completed=true");
        return;
    }

    // 7. No eligible signals means silence.
    suggestion = engine.Evaluate(
        {},
        {},
        {},
        context);

    if (!ExpectAction(
            "NO CANDIDATE",
            suggestion,
            CompanionAction::None,
            "",
            "")) {
        return;
    }

    ESP_LOGI(
        kTag,
        "PRIORITY + CONFIRMATION + DEFER + SILENCE: OK");

    ESP_LOGI(
        kTag,
        "TEST PASSED");
}

}  // namespace xiaozhi_care::daily