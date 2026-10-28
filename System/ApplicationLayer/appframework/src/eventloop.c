#include "internal.h"

#define SOURCE_TABLE_INITIAL_CAPACITY 8
#define USER_FD_MASK (EVENT_READABLE | EVENT_WRITABLE)

static AppSource *table(AppHandle app)
{
    return (AppSource *)app->sources_block.data;
}

AppSource *libappframework_internal_source_find(AppHandle app, AppEventSourceId id)
{
    for (usize i = 0; i < app->source_count; ++i) {
        if (table(app)[i].id == id) {
            return &table(app)[i];
        }
    }
    return NULL;
}

static ErrorCode table_grow(AppHandle app)
{
    usize capacity = app->source_capacity == 0 ? SOURCE_TABLE_INITIAL_CAPACITY
                                               : app->source_capacity * 2;
    usize bytes;
    ErrorCode result;

    if (capacity < app->source_capacity) {
        return CORE_OVERFLOW;
    }
    result = core_size_multiply(capacity, sizeof(AppSource), &bytes);
    if (result != CORE_OK) {
        return result;
    }
    result = memory_reallocate(&app->sources_block, bytes);
    if (result == CORE_OK) {
        app->source_capacity = capacity;
    }
    return result;
}

static ErrorCode table_add(AppHandle app, AppEventSourceId id, AppEventHandler handler,
                           void *context, bool internal)
{
    if (app->source_count == app->source_capacity) {
        ErrorCode result = table_grow(app);

        if (result != CORE_OK) {
            return result;
        }
    }
    table(app)[app->source_count++] =
        (AppSource){ .id = id, .handler = handler, .context = context, .internal = internal,
                    .serial = ++app->source_serial };
    return CORE_OK;
}

/* Order does not matter, so the last entry fills the gap. */
static void table_remove(AppHandle app, AppSource *entry)
{
    *entry = table(app)[--app->source_count];
}

ErrorCode libappframework_internal_register_fd(AppHandle app, i32 fd, EventMask mask,
                                               AppEventHandler handler, void *context,
                                               bool internal, AppEventSourceId *out_source)
{
    AppEventSourceId id;
    ErrorCode result = eventloop_add_fd(app->loop, fd, mask, &id);

    if (result != CORE_OK) {
        return result;
    }
    result = table_add(app, id, handler, context, internal);
    if (result != CORE_OK) {
        (void)eventloop_remove_source(app->loop, id);
        return result;
    }
    *out_source = id;
    return CORE_OK;
}

ErrorCode libappframework_internal_register_timer(AppHandle app, DurationMs delay_ms,
                                                  DurationMs period_ms,
                                                  AppEventHandler handler, void *context,
                                                  AppEventSourceId *out_source)
{
    AppEventSourceId id;
    ErrorCode result = eventloop_create_timer(app->loop, delay_ms, period_ms, &id);

    if (result != CORE_OK) {
        return result;
    }
    result = table_add(app, id, handler, context, true);
    if (result != CORE_OK) {
        (void)eventloop_remove_source(app->loop, id);
        return result;
    }
    *out_source = id;
    return CORE_OK;
}

ErrorCode libappframework_internal_update_fd(AppHandle app, AppEventSourceId source,
                                             EventMask mask)
{
    return eventloop_update_fd(app->loop, source, mask);
}

ErrorCode libappframework_internal_unregister(AppHandle app, AppEventSourceId source)
{
    AppSource *entry = libappframework_internal_source_find(app, source);
    ErrorCode result;

    if (entry == NULL) {
        return CORE_NOT_FOUND;
    }
    result = eventloop_remove_source(app->loop, source);
    table_remove(app, entry); /* gone from the table even if the loop objected */
    return result;
}

void libappframework_internal_dispatch(AppHandle app, const Event *events, usize count)
{
    /* Sources registered while this batch runs have no events in it. The loop only
     * promises ids unique among registered sources, so an id of a source removed
     * earlier in the batch may come back for a new one. */
    u64 batch_serial = app->source_serial;

    /* Requests of a connection whose write failed end here, not in the call that failed. */
    if (!libappframework_internal_quit_requested(app)) {
        libappframework_internal_apps_deferred(app);
        libappframework_internal_msg_deferred(app);
    }
    for (usize i = 0; i < count; ++i) {
        AppSource *entry;
        AppEventHandler handler;
        void *context;

        /* After a quit request no new handler starts; the rest of the batch is
         * dropped (the sources are level-triggered, nothing is lost). */
        if (libappframework_internal_quit_requested(app)) {
            return;
        }
        if (events[i].kind == EVENT_SOURCE_KIND_WAKE) {
            continue; /* only interrupts the wait */
        }
        /* Looked up by id on every event: a handler earlier in this batch may
         * have removed this source (and an id may then belong to a newer one). */
        entry = libappframework_internal_source_find(app, events[i].source_id);
        if (entry == NULL || entry->serial > batch_serial) {
            continue;
        }
        handler = entry->handler;
        context = entry->context;
        handler(app, events[i].source_id, events[i].mask, context);
    }
}

ErrorCode app_event_add_fd(AppHandle app, i32 fd, EventMask mask, AppEventHandler handler,
                           void *context, AppEventSourceId *out_source)
{
    ErrorCode result = libappframework_internal_enter(app, APP_OPERATION_EVENT_ADD);

    if (result != CORE_OK) {
        return result;
    }
    if (fd < 0 || mask == 0 || (mask & ~USER_FD_MASK) != 0 || handler == NULL ||
        out_source == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    return libappframework_internal_register_fd(app, fd, mask, handler, context, false,
                                                out_source);
}

ErrorCode app_event_create_timer(AppHandle app, DurationMs delay_ms, DurationMs period_ms,
                                 AppEventHandler handler, void *context,
                                 AppEventSourceId *out_source)
{
    AppEventSourceId id;
    ErrorCode result = libappframework_internal_enter(app, APP_OPERATION_EVENT_ADD);

    if (result != CORE_OK) {
        return result;
    }
    if (handler == NULL || out_source == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    result = eventloop_create_timer(app->loop, delay_ms, period_ms, &id);
    if (result != CORE_OK) {
        return result;
    }
    result = table_add(app, id, handler, context, false);
    if (result != CORE_OK) {
        (void)eventloop_remove_source(app->loop, id);
        return result;
    }
    *out_source = id;
    return CORE_OK;
}

ErrorCode app_event_remove(AppHandle app, AppEventSourceId source)
{
    AppSource *entry;
    ErrorCode result = libappframework_internal_enter(app, APP_OPERATION_EVENT_REMOVE);

    if (result != CORE_OK) {
        return result;
    }
    if (source == 0) {
        return CORE_INVALID_ARGUMENT;
    }
    entry = libappframework_internal_source_find(app, source);
    if (entry == NULL || entry->internal) {
        return CORE_NOT_FOUND; /* an internal source is not the application's */
    }
    return libappframework_internal_unregister(app, source);
}
