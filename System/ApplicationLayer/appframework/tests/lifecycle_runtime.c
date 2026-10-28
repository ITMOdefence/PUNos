/*
 * Runtime test of the pure lifecycle rules of appframework: the state
 * transitions and the table of operations allowed in each state
 * (DevDoc/ApplicationLayer/appframework.md, "Жизненный цикл").
 *
 * Check groups:
 *   1-5   state transitions, one row per source state
 *   6-9   transitions with values outside the enumeration
 *   10-16 operation table, one row per operation
 *   20-23 operation table with values outside the enumerations
 *   30    the Destroyed state refuses every operation
 */

#define CHECK_TEST_NAME "appframework-lifecycle"
#include <check.h>

#include "internal.h"

/* The moves of the specification's table, listed one by one. */
static bool spec_transition(AppLifecycleState from, AppLifecycleState to)
{
    if (from == APP_LIFECYCLE_CREATED) {
        return to == APP_LIFECYCLE_RUNNING || to == APP_LIFECYCLE_STOPPING;
    }
    if (from == APP_LIFECYCLE_RUNNING) {
        return to == APP_LIFECYCLE_STOPPING || to == APP_LIFECYCLE_FAILED;
    }
    if (from == APP_LIFECYCLE_STOPPING) {
        return to == APP_LIFECYCLE_DESTROYED;
    }
    if (from == APP_LIFECYCLE_FAILED) {
        return to == APP_LIFECYCLE_STOPPING;
    }
    return false;
}

/* True if every move from one state matches the specification. */
static bool transition_row_matches(AppLifecycleState from)
{
    for (u32 to = 0; to <= (u32)APP_LIFECYCLE_DESTROYED; ++to) {
        if (libappframework_internal_transition_allowed(from, (AppLifecycleState)to) !=
            spec_transition(from, (AppLifecycleState)to)) {
            return false;
        }
    }
    return true;
}

/* The operation table, written per rule instead of per cell. */
static bool spec_allowed(AppOperationKind operation, AppLifecycleState state)
{
    switch (operation) {
    case APP_OPERATION_RUN:
        return state == APP_LIFECYCLE_CREATED;
    case APP_OPERATION_REQUEST_QUIT:
    case APP_OPERATION_EVENT_REMOVE:
        return true;
    case APP_OPERATION_DESTROY:
        return state != APP_LIFECYCLE_RUNNING;
    case APP_OPERATION_EVENT_ADD:
    case APP_OPERATION_LOG:
    case APP_OPERATION_APPS:
    case APP_OPERATION_MSG:
        return state == APP_LIFECYCLE_CREATED || state == APP_LIFECYCLE_RUNNING;
    }
    return false;
}

/* True if the operation gives the specified result in every live state. */
static bool operation_row_matches(AppOperationKind operation)
{
    for (u32 state = 0; state <= (u32)APP_LIFECYCLE_FAILED; ++state) {
        ErrorCode expected = spec_allowed(operation, (AppLifecycleState)state)
                                 ? CORE_OK
                                 : CORE_INVALID_STATE;
        if (libappframework_internal_operation_allowed(operation, (AppLifecycleState)state) !=
            expected) {
            return false;
        }
    }
    return true;
}

int main(void)
{
    /* 1-5: transition rows. */
    EXPECT(1, transition_row_matches(APP_LIFECYCLE_CREATED));
    EXPECT(2, transition_row_matches(APP_LIFECYCLE_RUNNING));
    EXPECT(3, transition_row_matches(APP_LIFECYCLE_STOPPING));
    EXPECT(4, transition_row_matches(APP_LIFECYCLE_FAILED));
    EXPECT(5, transition_row_matches(APP_LIFECYCLE_DESTROYED));

    /* Spot checks that do not share the loop above. */
    EXPECT(6, libappframework_internal_transition_allowed(APP_LIFECYCLE_CREATED,
                                                          APP_LIFECYCLE_RUNNING));
    EXPECT(7, !libappframework_internal_transition_allowed(APP_LIFECYCLE_RUNNING,
                                                           APP_LIFECYCLE_CREATED));
    EXPECT(8, !libappframework_internal_transition_allowed((AppLifecycleState)5,
                                                           APP_LIFECYCLE_STOPPING));
    EXPECT(9, !libappframework_internal_transition_allowed(APP_LIFECYCLE_CREATED,
                                                           (AppLifecycleState)99));

    /* 10-16: operation rows. */
    EXPECT(10, operation_row_matches(APP_OPERATION_RUN));
    EXPECT(11, operation_row_matches(APP_OPERATION_REQUEST_QUIT));
    EXPECT(12, operation_row_matches(APP_OPERATION_DESTROY));
    EXPECT(13, operation_row_matches(APP_OPERATION_EVENT_ADD));
    EXPECT(14, operation_row_matches(APP_OPERATION_EVENT_REMOVE));
    EXPECT(15, operation_row_matches(APP_OPERATION_LOG));
    EXPECT(16, operation_row_matches(APP_OPERATION_APPS));
    EXPECT(24, operation_row_matches(APP_OPERATION_MSG));

    /* Spot checks of the cells that matter most for the contract. */
    EXPECT(17, libappframework_internal_operation_allowed(APP_OPERATION_RUN,
                                                          APP_LIFECYCLE_RUNNING) ==
                   CORE_INVALID_STATE);
    EXPECT(18, libappframework_internal_operation_allowed(APP_OPERATION_DESTROY,
                                                          APP_LIFECYCLE_RUNNING) ==
                   CORE_INVALID_STATE);
    EXPECT(19, libappframework_internal_operation_allowed(APP_OPERATION_APPS,
                                                          APP_LIFECYCLE_CREATED) == CORE_OK);

    /* 20-23: values outside the enumerations. */
    EXPECT(20, libappframework_internal_operation_allowed((AppOperationKind)8,
                                                          APP_LIFECYCLE_CREATED) ==
                   CORE_INVALID_ARGUMENT);
    EXPECT(21, libappframework_internal_operation_allowed(APP_OPERATION_LOG,
                                                          (AppLifecycleState)5) ==
                   CORE_INVALID_ARGUMENT);
    EXPECT(22, libappframework_internal_operation_allowed((AppOperationKind)0xFFFFFFFFu,
                                                          APP_LIFECYCLE_CREATED) ==
                   CORE_INVALID_ARGUMENT);
    EXPECT(23, libappframework_internal_operation_allowed(APP_OPERATION_LOG,
                                                          (AppLifecycleState)0xFFFFFFFFu) ==
                   CORE_INVALID_ARGUMENT);

    /* 30: Destroyed refuses everything, quitting included. */
    for (u32 operation = 0; operation <= (u32)APP_OPERATION_MSG; ++operation) {
        EXPECT(30, libappframework_internal_operation_allowed((AppOperationKind)operation,
                                                              APP_LIFECYCLE_DESTROYED) ==
                       CORE_INVALID_STATE);
    }

    check_passed();
    return 0;
}
