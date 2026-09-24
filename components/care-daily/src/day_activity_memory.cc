#include "care_daily/day_activity_memory.h"

namespace xiaozhi_care::daily {

const char* CompanionActivityStateName(CompanionActivityState state) {
    switch (state) {
        case CompanionActivityState::Discover:
            return "discover";
        case CompanionActivityState::Recall:
            return "recall";
        case CompanionActivityState::CheckResources:
            return "check_resources";
        case CompanionActivityState::Organize:
            return "organize";
        case CompanionActivityState::Doing:
            return "doing";
        case CompanionActivityState::Deferred:
            return "deferred";
        case CompanionActivityState::Completed:
            return "completed";
        case CompanionActivityState::Cancelled:
            return "cancelled";
        case CompanionActivityState::Unknown:
            return "unknown";
        default:
            return "unknown";
    }
}

const char* CompanionHelpLevelName(CompanionHelpLevel level) {
    switch (level) {
        case CompanionHelpLevel::Independent:
            return "independent";
        case CompanionHelpLevel::OpenQuestion:
            return "open_question";
        case CompanionHelpLevel::Hint:
            return "hint";
        case CompanionHelpLevel::ConcreteHelp:
            return "concrete_help";
        default:
            return "independent";
    }
}

const char* DayMemoryFactSourceName(DayMemoryFactSource source) {
    switch (source) {
        case DayMemoryFactSource::UserRecalled:
            return "user_recalled";
        case DayMemoryFactSource::UserConfirmed:
            return "user_confirmed";
        case DayMemoryFactSource::CareSuggested:
            return "care_suggested";
        default:
            return "user_recalled";
    }
}

}  // namespace xiaozhi_care::daily