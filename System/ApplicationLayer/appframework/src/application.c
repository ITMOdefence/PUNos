#include "internal.h"

/* Keeps the first error of a cleanup that must run to the end. */
static void keep_first(ErrorCode *result, ErrorCode error)
{
    if (*result == CORE_OK) {
        *result = error;
    }
}

ErrorCode libappframework_internal_enter(AppHandle app, AppOperationKind operation)
{
    if (app == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (gettid() != app->owner_tid) {
        return CORE_INVALID_STATE;
    }
    return libappframework_internal_operation_allowed(operation, app->state);
}

/* Copies a checked string after the structure; the block is zero-filled, so the
 * terminating NUL is already there. */
static const char *place_text(MemoryBlock *block, usize *offset, const char *text,
                              usize length)
{
    char *destination = (char *)block->data + *offset;
    Bytes to = { (u8 *)destination, length };
    Bytes from = { (u8 *)text, length };

    if (memory_copy(to, from) != CORE_OK) {
        return NULL;
    }
    *offset += length + 1;
    return destination;
}

ErrorCode app_create(const AppConfig *config, AppHandle *out_app)
{
    usize id_length;
    usize component_length;
    usize size;
    usize offset;
    MemoryBlock block;
    AppHandle app;
    ErrorCode result;

    if (config == NULL || out_app == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    result = libappframework_internal_text_check(config->application_id, 1,
                                                 APP_ID_MAX_LENGTH, &id_length);
    if (result != CORE_OK) {
        return result;
    }
    result = libappframework_internal_text_check(config->log_component, 1,
                                                 APP_LOG_COMPONENT_MAX_LENGTH,
                                                 &component_length);
    if (result != CORE_OK) {
        return result;
    }

    size = sizeof(struct AppHandle) + id_length + 1 + component_length + 1;
    result = memory_allocate(size, &block);
    if (result != CORE_OK) {
        return result;
    }
    app = (AppHandle)block.data;
    app->block = block;
    app->owner_tid = gettid();
    app->state = APP_LIFECYCLE_CREATED;
    offset = sizeof(struct AppHandle);
    app->application_id = place_text(&block, &offset, config->application_id, id_length);
    app->application_id_length = id_length;
    app->log_component = place_text(&block, &offset, config->log_component, component_length);
    app->log_component_length = component_length;
    if (app->application_id == NULL || app->log_component == NULL) {
        result = CORE_IO_ERROR;
    } else {
        result = eventloop_create(&app->loop);
    }
    if (result == CORE_OK) {
        result = libappframework_internal_runtime_attach(app);
        if (result != CORE_OK) {
            (void)eventloop_close(&app->loop);
        }
    }
    if (result != CORE_OK) {
        (void)memory_free(&block);
        return result;
    }

    (void)libappframework_internal_log(app, APP_LOG_LEVEL_INFO, "AppHandle created");
    *out_app = app;
    return CORE_OK;
}

ErrorCode app_destroy(AppHandle *app)
{
    AppHandle handle;
    MemoryBlock block;
    ErrorCode result;

    if (app == NULL || *app == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    handle = *app;
    result = libappframework_internal_enter(handle, APP_OPERATION_DESTROY);
    if (result != CORE_OK) {
        return result;
    }

    handle->state = APP_LIFECYCLE_STOPPING;
    (void)libappframework_internal_log(handle, APP_LOG_LEVEL_INFO, "AppHandle destroyed");
    libappframework_internal_apps_shutdown(handle);
    libappframework_internal_logging_shutdown(handle);

    result = CORE_OK;
    keep_first(&result, libappframework_internal_runtime_detach(handle));
    keep_first(&result, eventloop_close(&handle->loop));
    keep_first(&result, memory_free(&handle->sources_block));
    handle->state = APP_LIFECYCLE_DESTROYED;
    block = handle->block;
    keep_first(&result, memory_free(&block));
    *app = NULL;
    return result;
}
