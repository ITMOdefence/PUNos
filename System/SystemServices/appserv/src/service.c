#include <appserv/appserv.h>
#include "wire.h"

struct AppInstance {
    MemoryBlock allocation;
    AppInstance *next;
    const NativeAppSpec *app;
    AppInstanceId id;
    ProcessHandle process;
    AppState state;
    ProcessExitStatus status;
};

static void record(AppService *service, const char *event, AppInstance *instance,
                   ErrorCode error)
{
    if (service->log != NULL)
        service->log(service->log_context, event,
                     instance == NULL ? NULL : instance->app,
                     instance == NULL ? 0 : instance->id,
                     instance != NULL && instance->state == APP_EXITED
                         ? &instance->status : NULL, error);
}

ErrorCode app_service_init(AppService *service, const AppCatalog *catalog,
                           AppLogFn log, void *context)
{
    ErrorCode error;
    if (service == NULL) return CORE_INVALID_ARGUMENT;
    *service = (AppService){ 0 };
    error = app_catalog_validate(catalog);
    if (error != CORE_OK) return error;
    service->catalog = catalog;
    service->next_id = 1;
    service->log = log;
    service->log_context = context;
    service->ready = true;
    return CORE_OK;
}

static ErrorCode select_app(AppService *service, Bytes payload,
                             const NativeAppSpec **out_app)
{
    String id = { NULL, 0 };
    ErrorCode error;
    bool same;
    usize length;
    if (payload.size < 4) return CORE_INVALID_ARGUMENT;
    if (wire_read(payload.data, 2) != APP_PROTOCOL_VERSION) return CORE_UNSUPPORTED;
    length = (usize)wire_read(payload.data + 2, 2);
    if (length == 0 || length > APP_TEXT_MAX_LENGTH || payload.size != length + 4)
        return CORE_INVALID_ARGUMENT;
    error = string_from_utf8((Bytes){ payload.data + 4, length }, &id);
    if (error != CORE_OK) return error;
    error = CORE_NOT_FOUND;
    for (usize i = 0; i < service->catalog->count; ++i) {
        if (string_equals(&id, &service->catalog->applications[i].application_id,
                          &same) == CORE_OK && same) {
            *out_app = &service->catalog->applications[i];
            error = CORE_OK;
            break;
        }
    }
    (void)string_destroy(&id);
    return error;
}

static ErrorCode select_instance(AppService *service, Bytes payload,
                                  AppInstance **out_instance)
{
    AppInstanceId id;
    if (payload.size != 10) return CORE_INVALID_ARGUMENT;
    if (wire_read(payload.data, 2) != APP_PROTOCOL_VERSION) return CORE_UNSUPPORTED;
    id = wire_read(payload.data + 2, 8);
    if (id == 0) return CORE_INVALID_ARGUMENT;
    for (AppInstance *item = service->instances; item != NULL; item = item->next) {
        if (item->id == id) {
            *out_instance = item;
            return CORE_OK;
        }
    }
    return CORE_NOT_FOUND;
}

static usize info_size(const NativeAppSpec *app)
{
    return 4 + app->application_id.length + app->display_name.length;
}

static usize write_info(u8 *bytes, const NativeAppSpec *app)
{
    usize offset = 2;
    wire_write(bytes, 2, app->application_id.length);
    for (usize i = 0; i < app->application_id.length; ++i)
        bytes[offset++] = (u8)app->application_id.data[i];
    wire_write(bytes + offset, 2, app->display_name.length);
    offset += 2;
    for (usize i = 0; i < app->display_name.length; ++i)
        bytes[offset++] = (u8)app->display_name.data[i];
    return offset;
}

static ErrorCode launch(AppService *service, const NativeAppSpec *app, u8 *reply)
{
    MemoryBlock block;
    AppInstance *item;
    ErrorCode error;
    ProcessLaunch spec = { &app->executable, app->default_arguments,
                          app->argument_count, app->environment,
                          app->environment_count };
    if (service->next_id == 0) return CORE_OUT_OF_MEMORY;
    error = memory_allocate(sizeof(AppInstance), &block);
    if (error != CORE_OK) return CORE_OUT_OF_MEMORY;
    item = (AppInstance *)block.data;
    *item = (AppInstance){ .allocation = block, .app = app, .state = APP_STARTING };
    error = process_start(&spec, &item->process);
    if (error != CORE_OK) {
        error = error == CORE_OUT_OF_MEMORY ? error : CORE_IO_ERROR;
        record(service, "launch_failed", item, error);
        (void)memory_free(&block);
        return error;
    }
    item->id = service->next_id++;
    item->state = APP_RUNNING;
    item->next = service->instances;
    service->instances = item;
    wire_write(reply + 4, 8, item->id);
    record(service, "launched", item, CORE_OK);
    return CORE_OK;
}

ErrorCode app_service_request(AppService *service, u32 type, Bytes payload,
                              AppReply *out_reply)
{
    const NativeAppSpec *app = NULL;
    AppInstance *instance = NULL;
    ErrorCode error = CORE_OK;
    usize size = 4;
    u32 reply_type = type;
    bool known = type == APP_LIST || type == APP_GET_INFO || type == APP_LAUNCH ||
                 type == APP_STOP || type == APP_GET_STATUS;
    if (service == NULL || service->catalog == NULL || out_reply == NULL)
        return CORE_INVALID_ARGUMENT;
    *out_reply = (AppReply){ 0 };
    if (known) reply_type = type + 1;
    if (payload.size != 0 && payload.data == NULL) error = CORE_INVALID_ARGUMENT;
    else if (!known) error = CORE_INVALID_ARGUMENT;
    else if (!service->ready) error = CORE_INVALID_STATE;
    else if (type == APP_LIST) {
        if (payload.size != 0) error = CORE_INVALID_ARGUMENT;
        else {
            size = 6;
            for (usize i = 0; i < service->catalog->count; ++i) {
                if (core_size_add(size, info_size(&service->catalog->applications[i]),
                                   &size) != CORE_OK) return CORE_OUT_OF_MEMORY;
            }
        }
    } else if (type == APP_GET_INFO || type == APP_LAUNCH) {
        error = select_app(service, payload, &app);
        if (error == CORE_OK) size = type == APP_GET_INFO ? 4 + info_size(app) : 12;
    } else {
        error = select_instance(service, payload, &instance);
        if (error == CORE_OK && type == APP_GET_STATUS) size = 10;
    }
    /* Reserve reply memory before a launch/stop can have any side effects. */
    out_reply->type = reply_type;
    if (memory_allocate(size, &out_reply->payload) != CORE_OK) return CORE_OUT_OF_MEMORY;
    if (error == CORE_OK) {
        u8 *bytes = out_reply->payload.data;
        if (type == APP_LIST) {
            usize offset = 6;
            wire_write(bytes + 4, 2, service->catalog->count);
            for (usize i = 0; i < service->catalog->count; ++i)
                offset += write_info(bytes + offset, &service->catalog->applications[i]);
        } else if (type == APP_GET_INFO) (void)write_info(bytes + 4, app);
        else if (type == APP_LAUNCH) error = launch(service, app, bytes);
        else if (type == APP_STOP) {
            if (instance->state == APP_EXITED) error = CORE_INVALID_STATE;
            else {
                error = process_terminate(instance->process);
                if (error == CORE_OK) instance->state = APP_TERMINATING;
                else error = error == CORE_OUT_OF_MEMORY ? error : CORE_IO_ERROR;
            }
            record(service, "stop_requested", instance, error);
        } else if (type == APP_GET_STATUS) {
            bytes[4] = (u8)instance->state;
            if (instance->state == APP_EXITED) {
                bytes[5] = (u8)instance->status.kind;
                wire_write(bytes + 6, 4, (u32)instance->status.value);
            }
        }
    }
    wire_write(out_reply->payload.data, 4, (u32)error);
    /* Retain the owning allocation length separately from the wire length. */
    out_reply->size = error == CORE_OK ? out_reply->payload.size : 4;
    if (error == CORE_INVALID_ARGUMENT || error == CORE_UNSUPPORTED)
        record(service, "invalid_request", NULL, error);
    return CORE_OK;
}

ErrorCode app_service_poll(AppService *service)
{
    if (service == NULL || service->catalog == NULL) return CORE_INVALID_ARGUMENT;
    for (AppInstance *item = service->instances; item != NULL; item = item->next) {
        bool finished = false;
        ErrorCode error;
        if (item->state == APP_EXITED) continue;
        error = process_poll(&item->process, &finished, &item->status);
        if (error == CORE_INTERRUPTED) continue;
        if (error != CORE_OK) return error;
        if (finished) {
            item->state = APP_EXITED;
            record(service, "exited", item, CORE_OK);
        }
    }
    return CORE_OK;
}

ErrorCode app_service_shutdown(AppService *service)
{
    ErrorCode first_error = CORE_OK;
    AppInstance **link;
    if (service == NULL) return CORE_INVALID_ARGUMENT;
    service->ready = false;
    /* All children receive SIGTERM before any blocking wait. */
    for (AppInstance *item = service->instances; item != NULL; item = item->next) {
        if (item->state != APP_EXITED) {
            ErrorCode error = process_terminate(item->process);
            if (error == CORE_OK || error == CORE_NOT_FOUND) item->state = APP_TERMINATING;
            /* ESRCH is safe to wait: the process has already finished. */
            else if (error != CORE_NOT_FOUND && first_error == CORE_OK) first_error = error;
        }
    }
    link = &service->instances;
    while (*link != NULL) {
        AppInstance *item = *link;
        if (item->state == APP_RUNNING) {
            /* A failed SIGTERM must not cause a blocking wait on a live child. */
            link = &item->next;
            continue;
        }
        if (item->state != APP_EXITED) {
            ErrorCode error;
            do { error = process_wait(&item->process, &item->status); }
            while (error == CORE_INTERRUPTED);
            if (error != CORE_OK) {
                if (first_error == CORE_OK) first_error = error;
                link = &item->next;
                continue;
            }
            item->state = APP_EXITED;
            record(service, "exited", item, CORE_OK);
        }
        *link = item->next;
        MemoryBlock block = item->allocation;
        (void)memory_free(&block);
    }
    return first_error;
}
