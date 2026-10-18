#include "internal.h"

/* Skeleton: replaced by the real implementation in a later step. */

ErrorCode app_log(AppHandle app, AppLogLevel level, const char *message)
{
    (void)app;
    (void)level;
    (void)message;
    return CORE_UNSUPPORTED;
}

/* Hooks used by the other modules; filled in with the logserv client. */

ErrorCode libappframework_internal_log(AppHandle app, AppLogLevel level,
                                       const char *message)
{
    (void)app;
    (void)level;
    (void)message;
    return CORE_OK;
}

void libappframework_internal_logging_shutdown(AppHandle app)
{
    (void)app;
}
