#ifndef PUNOS_APPFRAMEWORK_INTERNAL_H
#define PUNOS_APPFRAMEWORK_INTERNAL_H

/*
 * Internals shared by the modules of appframework and by its tests. Nothing
 * here is public API. Pure functions that a test needs to check on exact
 * values are prefixed libappframework_internal_.
 */

#include <appframework/app.h>
#include <punos/syscall/syscall.h>
#include <sys/memory.h>

/* ------------------------------------------------------------------------ */
/* Lifecycle (module lifecycle)                                              */
/* ------------------------------------------------------------------------ */

/* The states of the lifecycle table in the specification. */
typedef enum AppLifecycleState {
    APP_LIFECYCLE_CREATED = 0,
    APP_LIFECYCLE_RUNNING = 1,
    APP_LIFECYCLE_STOPPING = 2,
    APP_LIFECYCLE_FAILED = 3,
    APP_LIFECYCLE_DESTROYED = 4
} AppLifecycleState;

/* Rows of the operation table in the specification. */
typedef enum AppOperationKind {
    APP_OPERATION_RUN = 0,
    APP_OPERATION_REQUEST_QUIT = 1,
    APP_OPERATION_DESTROY = 2,
    APP_OPERATION_EVENT_ADD = 3,    /* app_event_add_fd, app_event_create_timer */
    APP_OPERATION_EVENT_REMOVE = 4,
    APP_OPERATION_LOG = 5,
    APP_OPERATION_APPS = 6          /* app_apps_* */
} AppOperationKind;

/* True if the lifecycle table allows the move from one state to the other. */
bool libappframework_internal_transition_allowed(AppLifecycleState from,
                                                 AppLifecycleState to);

/*
 * CORE_OK if the operation is allowed in the state, CORE_INVALID_STATE if not,
 * CORE_INVALID_ARGUMENT for a value outside the enumerations. The Destroyed
 * state has no valid handle, so every operation is refused in it.
 */
ErrorCode libappframework_internal_operation_allowed(AppOperationKind operation,
                                                     AppLifecycleState state);

/* ------------------------------------------------------------------------ */
/* The context                                                               */
/* ------------------------------------------------------------------------ */

/* Events fetched from the event loop by one wait. */
#define APP_EVENT_BATCH_SIZE 16

/* One registered source: an application's or one of the library's own. */
typedef struct AppSource {
    AppEventSourceId id;
    AppEventHandler handler;
    void *context;
    bool internal; /* never visible to, or removable by, the application */
} AppSource;

struct AppHandle {
    MemoryBlock block;          /* the allocation that holds this structure */
    AppLifecycleState state;    /* owner thread only */
    pid_t owner_tid;            /* thread that called app_create() */
    EventLoopHandle loop;
    MemoryBlock sources_block;  /* AppSource[source_capacity] */
    usize source_count;
    usize source_capacity;
    volatile u32 quit_requested;    /* atomic: any thread, signal handler */
    volatile u32 sigterm_received;  /* atomic: set by the signal handler */
    struct sigaction previous_sigterm;
    bool sigterm_installed;
    const char *application_id;     /* NUL-terminated, inside block */
    usize application_id_length;
    const char *log_component;      /* NUL-terminated, inside block */
    usize log_component_length;
};

/*
 * Checks the handle, the calling thread and the lifecycle state for an
 * operation, in this order: NULL (CORE_INVALID_ARGUMENT), not the owner thread
 * (CORE_INVALID_STATE), state table (CORE_INVALID_STATE).
 */
ErrorCode libappframework_internal_enter(AppHandle app, AppOperationKind operation);

/* ------------------------------------------------------------------------ */
/* Text (module application)                                                 */
/* ------------------------------------------------------------------------ */

/* True if the bytes are well-formed UTF-8 without NUL (same rules as libstr). */
bool libappframework_internal_utf8_valid(const u8 *data, usize size);

/*
 * Checks a NUL-terminated UTF-8 string of min_length..max_length bytes. Never
 * reads more than max_length + 1 bytes. CORE_INVALID_ARGUMENT for NULL, a
 * length outside the bounds or malformed UTF-8.
 */
ErrorCode libappframework_internal_text_check(const char *text, usize min_length,
                                              usize max_length, usize *out_length);

/* ------------------------------------------------------------------------ */
/* Event sources (module eventloop)                                          */
/* ------------------------------------------------------------------------ */

AppSource *libappframework_internal_source_find(AppHandle app, AppEventSourceId id);

/* Registers an fd in the event loop and in the source table, undoing the first
 * if the second fails. */
ErrorCode libappframework_internal_register_fd(AppHandle app, i32 fd, EventMask mask,
                                               AppEventHandler handler, void *context,
                                               bool internal, AppEventSourceId *out_source);

ErrorCode libappframework_internal_update_fd(AppHandle app, AppEventSourceId source,
                                             EventMask mask);

/* Removes a source from the event loop and the table. */
ErrorCode libappframework_internal_unregister(AppHandle app, AppEventSourceId source);

/* Calls the handler of every event of one wait, unless a quit was requested. */
void libappframework_internal_dispatch(AppHandle app, const Event *events, usize count);

/* ------------------------------------------------------------------------ */
/* Runtime (module runtime)                                                  */
/* ------------------------------------------------------------------------ */

/* Makes app the one active handle and installs the SIGTERM handler. */
ErrorCode libappframework_internal_runtime_attach(AppHandle app);

/* Restores the previous SIGTERM handler and releases the active-handle slot. */
ErrorCode libappframework_internal_runtime_detach(AppHandle app);

/* True once app_request_quit() or SIGTERM asked for the loop to stop. */
bool libappframework_internal_quit_requested(AppHandle app);

/* ------------------------------------------------------------------------ */
/* Services (modules logging and applications)                               */
/* ------------------------------------------------------------------------ */

/* Best effort: queues a record for logserv; the result is advisory. */
ErrorCode libappframework_internal_log(AppHandle app, AppLogLevel level,
                                       const char *message);

/* Ends pending requests with CORE_INVALID_STATE and closes the connection. */
void libappframework_internal_apps_shutdown(AppHandle app);

/* One non-blocking attempt to pass queued records on, then closes. */
void libappframework_internal_logging_shutdown(AppHandle app);

#endif
