# care-daily

Daily Care component for XiaoZhi Care.

- DP-001 introduced the domain model.
- DP-002 added NVS persistence through a repository boundary.
- DP-003 adds the first deterministic Daily Routine Engine.

## Public headers

- `care_daily/daily_time.h`
- `care_daily/routine_id.h`
- `care_daily/routine_type.h`
- `care_daily/routine_state.h`
- `care_daily/routine_schedule.h`
- `care_daily/routine_placement.h`
- `care_daily/care_routine.h`
- `care_daily/routine_repository.h`
- `care_daily/nvs_routine_repository.h`
- `care_daily/daily_routine_engine.h`
- `care_daily/daily_routine_test.h`
- `care_daily/daily_routine_engine_test.h`

## Validation

The component includes temporary validation functions:

- `RunDailyRoutinePersistenceTest()` from DP-002
- `RunDailyRoutineEngineTest()` from DP-003

They do not run automatically.

## DP-006

Adds persistent routine execution history:

- RoutineExecution
- RoutineExecutionRepository
- NvsRoutineExecutionRepository
- RunDailyRoutineExecutionTest()

Execution events are history records. They do not prove medical ingestion.
