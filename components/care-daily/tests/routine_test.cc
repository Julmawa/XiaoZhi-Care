#include "care_daily/care_routine.h"

namespace xiaozhi_care::daily::tests {

void RoutineDomainCompileTest() {
    CareRoutine routine;
    routine.id = RoutineId("rtn-test-001");
    routine.type = RoutineType::Medication;
    routine.state = RoutineState::Active;
    routine.title = "Morning medication";
    routine.schedule.time = DailyTime(7, 30);

    (void)routine.IsValid();
    (void)routine.IsActive();
}

}  // namespace xiaozhi_care::daily::tests
