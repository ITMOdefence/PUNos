#ifndef PUNOS_APPFRAMEWORK_INTERNAL_H
#define PUNOS_APPFRAMEWORK_INTERNAL_H

/*
 * Internals shared by the modules of appframework and by its tests. Nothing
 * here is public API. Pure functions that a test needs to check on exact
 * values are prefixed libappframework_internal_.
 */

#include <appframework/app.h>
#include <punos/syscall/syscall.h>
#include <special/ipc.h>
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
    u64 serial;    /* order of registration, see dispatch */
} AppSource;

/* The connection to logserv and the records that have not been confirmed. */
typedef struct AppLogClient {
    IpcConnectionHandle connection;  /* NULL: not connected */
    bool open;                       /* connection is Open (else Connecting) */
    bool write_interest;             /* WRITABLE is requested for the source */
    AppEventSourceId source;         /* 0: not registered */
    MemoryBlock *queue;              /* encoded records waiting for Open */
    MemoryBlock queue_block;         /* the allocation behind queue */
    usize queued;
    usize queue_capacity;
    usize unacknowledged;            /* sent, no reply yet */
} AppLogClient;

/* One request to appserv, from app_apps_*() until its callback. */
typedef struct AppRequest {
    AppRequestId id;
    AppAppsOperation operation;
    AppAppsCallback callback;
    void *context;
    MemoryBlock payload; /* encoded request; released once it is sent */
    bool sent;
} AppRequest;

/* The connection to appserv and the FIFO of requests (a ring buffer). */
typedef struct AppAppsClient {
    IpcConnectionHandle connection;  /* NULL: not connected */
    bool open;
    bool write_interest;
    bool failed;                     /* a write failed; the event loop ends the requests */
    AppEventSourceId source;         /* 0: not registered */
    MemoryBlock block;               /* APP_APPS_MAX_PENDING requests */
    AppRequest *requests;
    usize head;
    usize count;
    usize ending;                    /* taken out of the queue, callbacks still due; slots reserved */
    AppRequestId last_request;       /* last identifier handed out */
} AppAppsClient;

struct AppHandle {
    MemoryBlock block;          /* the allocation that holds this structure */
    AppLifecycleState state;    /* owner thread only */
    pid_t owner_tid;            /* thread that called app_create() */
    EventLoopHandle loop;
    MemoryBlock sources_block;  /* AppSource[source_capacity] */
    usize source_count;
    usize source_capacity;
    u64 source_serial;          /* serial of the last registered source */
    volatile u32 quit_requested;    /* atomic: any thread, signal handler */
    volatile u32 sigterm_received;  /* atomic: set by the signal handler */
    struct sigaction previous_sigterm;
    bool sigterm_installed;
    bool destroying;                /* app_destroy() is running: no second one */
    const char *application_id;     /* NUL-terminated, inside block */
    usize application_id_length;
    const char *log_component;      /* NUL-terminated, inside block */
    usize log_component_length;
    AppLogClient logserv;
    AppAppsClient appserv;
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
/* The SIGTERM handler's access to the handle (see runtime.c); the handle stays
 * valid between the two calls. Exposed for the tests of the lifetime protocol. */
AppHandle libappframework_internal_signal_enter(void);
void libappframework_internal_signal_leave(void);

/* ------------------------------------------------------------------------ */
/* Services (modules logging and applications)                               */
/* ------------------------------------------------------------------------ */

/* Where the clients connect. The variables exist so that tests can point the
 * library at a fake peer; nothing else changes them. */
extern const char *libappframework_internal_logserv_endpoint;

/* Encodes the log.submit payload (appframework.md, "Протокол клиента logserv").
 * The block is allocated by the function; the caller frees it. */
ErrorCode libappframework_internal_log_encode(AppHandle app, AppLogLevel level,
                                              const char *message, usize message_length,
                                              i32 process_id, i32 thread_id,
                                              MemoryBlock *out_payload);

extern const char *libappframework_internal_appserv_endpoint;

/* A reply of appserv, decoded and checked. */
typedef struct AppDecoded {
    ErrorCode service_error;   /* the ErrorCode inside the reply; CORE_OK on success */
    AppAppsResult result;      /* defined if service_error is CORE_OK */
    MemoryBlock storage;       /* copies of the strings of a list or info result */
} AppDecoded;

/* Builds the payload and the message type of a request (appserv.md). id is
 * used by GetInfo and Launch, instance by Stop and GetStatus. */
ErrorCode libappframework_internal_apps_encode(AppAppsOperation operation, const char *id,
                                               usize id_length, AppInstanceId instance,
                                               MemoryBlock *out_payload,
                                               IpcMessageType *out_type);

/* Checks a reply to a request of the given operation (the strict rules of
 * "Протокол клиента appserv"). CORE_OK if it is well formed, CORE_IO_ERROR if
 * it breaks the protocol, CORE_OUT_OF_MEMORY if the copies cannot be made. */
ErrorCode libappframework_internal_apps_decode(AppAppsOperation operation, const u8 *data,
                                               usize size, AppDecoded *out);

void libappframework_internal_apps_decoded_release(AppDecoded *decoded);

/* Best effort: queues a record for logserv; the result is advisory. */
ErrorCode libappframework_internal_log(AppHandle app, AppLogLevel level,
                                       const char *message);

/* Ends pending requests with CORE_INVALID_STATE and closes the connection. */
void libappframework_internal_apps_shutdown(AppHandle app);
/* Ends the requests of a connection whose write failed (called by the event loop). */
void libappframework_internal_apps_deferred(AppHandle app);

/* One non-blocking attempt to pass queued records on, then closes. */
void libappframework_internal_logging_shutdown(AppHandle app);

#endif
