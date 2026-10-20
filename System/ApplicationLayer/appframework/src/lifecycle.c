#include "internal.h"

/* Allowed moves of the lifecycle table: row = from, column = to. */
static const bool transitions[5][5] = {
    /* to:               Created Running Stopping Failed Destroyed */
    /* Created   */ {    false,  true,   true,    false, false },
    /* Running   */ {    false,  false,  true,    true,  false },
    /* Stopping  */ {    false,  false,  false,   false, true  },
    /* Failed    */ {    false,  false,  true,    false, false },
    /* Destroyed */ {    false,  false,  false,   false, false },
};

/* Operation table: row = operation, column = state (Created..Failed). */
static const bool allowed[7][4] = {
    /* state:                 Created Running Stopping Failed */
    /* RUN           */ {     true,   false,  false,   false },
    /* REQUEST_QUIT  */ {     true,   true,   true,    true  },
    /* DESTROY       */ {     true,   false,  true,    true  },
    /* EVENT_ADD     */ {     true,   true,   false,   false },
    /* EVENT_REMOVE  */ {     true,   true,   true,    true  },
    /* LOG           */ {     true,   true,   false,   false },
    /* APPS          */ {     true,   true,   false,   false },
};

bool libappframework_internal_transition_allowed(AppLifecycleState from,
                                                 AppLifecycleState to)
{
    if ((u32)from > (u32)APP_LIFECYCLE_DESTROYED ||
        (u32)to > (u32)APP_LIFECYCLE_DESTROYED) {
        return false;
    }
    return transitions[from][to];
}

ErrorCode libappframework_internal_operation_allowed(AppOperationKind operation,
                                                     AppLifecycleState state)
{
    if ((u32)operation > (u32)APP_OPERATION_APPS ||
        (u32)state > (u32)APP_LIFECYCLE_DESTROYED) {
        return CORE_INVALID_ARGUMENT;
    }
    if (state == APP_LIFECYCLE_DESTROYED) {
        return CORE_INVALID_STATE;
    }
    return allowed[operation][state] ? CORE_OK : CORE_INVALID_STATE;
}
