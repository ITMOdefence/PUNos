#include "internal.h"

/* Skeleton: replaced by the real implementation in a later step. */

ErrorCode app_apps_list(AppHandle app, AppAppsCallback callback, void *context,
                        AppRequestId *out_request)
{
    (void)app;
    (void)callback;
    (void)context;
    (void)out_request;
    return CORE_UNSUPPORTED;
}

ErrorCode app_apps_get_info(AppHandle app, const char *application_id,
                            AppAppsCallback callback, void *context,
                            AppRequestId *out_request)
{
    (void)app;
    (void)application_id;
    (void)callback;
    (void)context;
    (void)out_request;
    return CORE_UNSUPPORTED;
}

ErrorCode app_apps_launch(AppHandle app, const char *application_id,
                          AppAppsCallback callback, void *context,
                          AppRequestId *out_request)
{
    (void)app;
    (void)application_id;
    (void)callback;
    (void)context;
    (void)out_request;
    return CORE_UNSUPPORTED;
}

ErrorCode app_apps_stop(AppHandle app, AppInstanceId instance,
                        AppAppsCallback callback, void *context,
                        AppRequestId *out_request)
{
    (void)app;
    (void)instance;
    (void)callback;
    (void)context;
    (void)out_request;
    return CORE_UNSUPPORTED;
}

ErrorCode app_apps_get_status(AppHandle app, AppInstanceId instance,
                              AppAppsCallback callback, void *context,
                              AppRequestId *out_request)
{
    (void)app;
    (void)instance;
    (void)callback;
    (void)context;
    (void)out_request;
    return CORE_UNSUPPORTED;
}

/* Hook used by app_destroy(); filled in with the appserv client. */

void libappframework_internal_apps_shutdown(AppHandle app)
{
    (void)app;
}
