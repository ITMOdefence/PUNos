#include <initserv.h>
#include <special/ipc.h>
#include <sys/random.h>
#include <sys/thread.h>
#include <punos/syscall/syscall.h>
#include "wire.h"

typedef struct ServiceState {
    ProcessHandle process;
    EventSourceId restart_timer;
    Deadline restart_at;
    DurationMs delay;
    bool pending;
    bool exit_pending;
    ProcessExitStatus last_exit;
} ServiceState;
static volatile i32 stopping;
static bool finishing_log;
static char presenter_argument[96];
static char controller_argument[96];
static char shell_argument[96];
static String presenter_argument_string;
static String controller_argument_string;
static String shell_argument_string;
static void stop_signal(i32 number) { (void)number; stopping = 1; }
static bool equal(const String *text, const char *literal)
{
    usize i = 0;
    while (literal[i] != '\0' && i < text->length && text->data[i] == literal[i]) ++i;
    return i == text->length && literal[i] == '\0';
}
static ErrorCode make_token_argument(const char *prefix, char output[96],
                                     String *out_string)
{
    static const char hexadecimal[] = "0123456789abcdef";
    u8 token[32];
    usize length = 0;
    ErrorCode error = random_fill((Bytes){ token, sizeof(token) });

    if (error != CORE_OK) return error;
    while (prefix[length] != '\0') {
        if (length + sizeof(token) * 2 + 1 > 96) return CORE_OVERFLOW;
        output[length] = prefix[length];
        ++length;
    }
    for (usize index = 0; index < sizeof(token); ++index) {
        output[length++] = hexadecimal[token[index] >> 4];
        output[length++] = hexadecimal[token[index] & 15];
    }
    output[length] = '\0';
    *out_string = (String){ output, length };
    return CORE_OK;
}
static ErrorCode initialize_gui_tokens(void)
{
    ErrorCode error = make_token_argument(
        "--presenter-token=", presenter_argument, &presenter_argument_string);
    if (error == CORE_OK) error = make_token_argument(
        "--controller-token=", controller_argument,
        &controller_argument_string);
    if (error == CORE_OK) error = make_token_argument(
        "--shell-token=", shell_argument, &shell_argument_string);
    return error;
}
static const String *expanded_argument(const String *argument)
{
    if (equal(argument, "--presenter-token=@PUNOS_PRESENTER_TOKEN@")) {
        return &presenter_argument_string;
    }
    if (equal(argument, "--controller-token=@PUNOS_CONTROLLER_TOKEN@")) {
        return &controller_argument_string;
    }
    if (equal(argument, "--shell-token=@PUNOS_SHELL_TOKEN@")) {
        return &shell_argument_string;
    }
    return argument;
}
static ErrorCode start_service(const ServiceSpec *spec,
                               ProcessHandle *out_process)
{
    String arguments[32];
    ProcessLaunch launch;

    if (spec == NULL || out_process == NULL ||
        spec->launch.argument_count > 32) return CORE_INVALID_ARGUMENT;
    launch = spec->launch;
    for (usize index = 0; index < launch.argument_count; ++index) {
        arguments[index] = *expanded_argument(&launch.arguments[index]);
    }
    if (launch.argument_count != 0) launch.arguments = arguments;
    return process_start(&launch, out_process);
}
static ErrorCode valid_text(const String *text, bool nonempty)
{
    String copy = { 0 };
    if (text == NULL || text->data == NULL || text->length > STRING_MAX_LENGTH ||
        (nonempty && text->length == 0) || text->data[text->length] != '\0')
        return CORE_INVALID_ARGUMENT;
    ErrorCode error = string_from_utf8((Bytes){ (u8 *)text->data, text->length }, &copy);
    if (error == CORE_OK) error = string_destroy(&copy);
    return error;
}
static ErrorCode validate_registry(void)
{
    if (init_registry.count == 0 || init_registry.count > 64 || init_registry.services == NULL ||
        init_registry.id.length != 64 || valid_text(&init_registry.id, true) != CORE_OK)
        return CORE_INVALID_ARGUMENT;
    const ServiceSpec *log = &init_registry.services[0];
    if (!equal(&log->service_id, "logserv") || !log->enabled || !log->critical || !log->restart)
        return CORE_INVALID_ARGUMENT;
    for (usize i = 0; i < init_registry.count; ++i) {
        const ServiceSpec *spec = &init_registry.services[i];
        if (valid_text(&spec->service_id, true) != CORE_OK || spec->service_id.length > 64 ||
            valid_text(spec->launch.executable, true) != CORE_OK) return CORE_INVALID_ARGUMENT;
        for (usize j = 0; j < i; ++j) {
            bool same;
            if (string_equals(&spec->service_id, &init_registry.services[j].service_id, &same) != CORE_OK || same)
                return CORE_INVALID_ARGUMENT;
        }
        const ProcessLaunch *launch = &spec->launch;
        usize size;
        if ((launch->argument_count && launch->arguments == NULL) ||
            (launch->environment_count && launch->environment == NULL) ||
            core_size_multiply(launch->argument_count, sizeof(String), &size) != CORE_OK ||
            core_size_multiply(launch->environment_count, sizeof(String), &size) != CORE_OK)
            return CORE_INVALID_ARGUMENT;
        for (usize j = 0; j < launch->argument_count; ++j)
            if (valid_text(&launch->arguments[j], false) != CORE_OK) return CORE_INVALID_ARGUMENT;
        for (usize j = 0; j < launch->environment_count; ++j) {
            const String *entry = &launch->environment[j];
            if (valid_text(entry, true) != CORE_OK) return CORE_INVALID_ARGUMENT;
            usize split = 0;
            while (split < entry->length && entry->data[split] != '=') ++split;
            if (split == 0 || split == entry->length) return CORE_INVALID_ARGUMENT;
        }
    }
    return CORE_OK;
}

/* Temporary log connection; all socket and loop operations run on the owner.
 * Timer deadlines are also checked in the outer loop, so consuming another
 * ready timer while waiting for an acknowledgement cannot lose a restart. */
static ErrorCode communicate(EventLoopHandle loop, const IpcMessage *request, DurationMs timeout)
{
    String endpoint = { (char *)INIT_LOG_ENDPOINT, sizeof(INIT_LOG_ENDPOINT)-1 };
    IpcConnectionHandle connection = NULL;
    EventSourceId source = 0;
    Deadline deadline;
    ErrorCode error = time_deadline_after(timeout, &deadline);
    if (error != CORE_OK) return error;
    error = ipc_connection_begin_connect(&endpoint, &connection);
    if (error != CORE_OK) return error;
    i32 fd;
    error = ipc_connection_get_fd(connection, &fd);
    if (error == CORE_OK) error = eventloop_add_fd(loop, fd, EVENT_READABLE|EVENT_WRITABLE, &source);
    if (error != CORE_OK) goto done;
    bool connected = false, sent = false;
    while (!stopping || finishing_log) {
        bool expired;
        error = time_deadline_expired(deadline, &expired);
        if (error != CORE_OK) break;
        if (expired) { error = CORE_TIMEOUT; break; }
        Event events[32]; usize count;
        error = eventloop_wait(loop, events, 32, 50, &count);
        if (error == CORE_INTERRUPTED) continue;
        if (error != CORE_OK) break;
        for (usize i = 0; i < count; ++i) {
            if (events[i].source_id != source) continue;
            if (!connected) {
                IpcConnectionState state;
                error = ipc_connection_finish_connect(connection, &state);
                if (error != CORE_OK) goto done;
                if (state == IPC_CONNECTION_STATE_CONNECTING) continue;
                if (state != IPC_CONNECTION_STATE_OPEN) { error = CORE_IO_ERROR; goto done; }
                connected = true;
            }
            if (request == NULL) { error = CORE_OK; goto done; }
            if (!sent) {
                error = ipc_connection_send(connection, request);
                if (error != CORE_OK) goto done;
                sent = true;
            }
            bool pending;
            error = ipc_connection_flush(connection, &pending);
            if (error != CORE_OK) goto done;
            error = eventloop_update_fd(loop, source, EVENT_READABLE | (pending ? EVENT_WRITABLE : 0));
            if (error != CORE_OK) goto done;
            IpcMessage response = { 0 }; IpcReceiveStatus status;
            error = ipc_connection_receive(connection, &status, &response);
            if (error != CORE_OK) goto done;
            if (status == IPC_RECEIVE_STATUS_PEER_CLOSED) { error = CORE_IO_ERROR; goto done; }
            if (status == IPC_RECEIVE_STATUS_MESSAGE) {
                error = response.type == 2 && response.payload.size == 4
                    ? (ErrorCode)wire_read(response.payload.data, 4) : CORE_IO_ERROR;
                (void)ipc_message_destroy(&response);
                goto done;
            }
        }
    }
    error = CORE_INTERRUPTED;
done:
    if (source) (void)eventloop_remove_source(loop, source);
    (void)ipc_connection_close(&connection);
    return error;
}
static void text(u8 *bytes, usize *offset, const char *value)
{ while (*value != '\0') bytes[(*offset)++] = (u8)*value++; }
static void number(u8 *bytes, usize *offset, u64 value)
{
    u8 digits[20]; usize count = 0;
    do { digits[count++] = (u8)('0' + value%10); value /= 10; } while (value);
    while (count) bytes[(*offset)++] = digits[--count];
}
static ErrorCode trace(EventLoopHandle loop, const char *event, const ServiceSpec *spec,
                        const ProcessExitStatus *status, ErrorCode error)
{
    u8 bytes[512] = { 0 }; usize offset = 25;
    wire_write(bytes, 2, 1); wire_write(bytes+2, 4, getpid()); wire_write(bytes+6, 4, gettid());
    bytes[10] = error == CORE_OK ? 2 : 4; wire_write(bytes+11, 2, 8);
    usize component = 17; text(bytes, &component, "initserv");
    text(bytes, &offset, event);
    if (spec != NULL) {
        text(bytes, &offset, " service_id=");
        for (usize i=0; i<spec->service_id.length; ++i) bytes[offset++]=(u8)spec->service_id.data[i];
    }
    text(bytes, &offset, " error="); number(bytes, &offset, error);
    if (status != NULL) {
        text(bytes, &offset, " exit_kind="); number(bytes, &offset, status->kind);
        text(bytes, &offset, " exit_value="); number(bytes, &offset, (u32)status->value);
    }
    wire_write(bytes+13, 4, offset-25);
    IpcMessage request = { 1, { bytes, offset } };
    return communicate(loop, &request, 1000);
}
static ErrorCode log_ready(EventLoopHandle loop, ServiceState *log)
{
    Deadline deadline;
    ErrorCode error = time_deadline_after(INIT_STARTUP_TIMEOUT_MS, &deadline);
    if (error != CORE_OK) return error;
    for (;;) {
        bool finished, expired;
        ProcessExitStatus status;
        error = process_poll(&log->process, &finished, &status);
        if (error == CORE_INTERRUPTED) continue;
        if (error != CORE_OK || finished) return CORE_IO_ERROR;
        error = communicate(loop, NULL, 100);
        if (error == CORE_OK) return CORE_OK;
        if (stopping) return CORE_INTERRUPTED;
        if (time_deadline_expired(deadline, &expired) != CORE_OK || expired) return CORE_TIMEOUT;
        Event events[32]; usize count;
        error = eventloop_wait(loop, events, 32, 50, &count);
        if (error != CORE_OK && error != CORE_INTERRUPTED) return error;
    }
}
static ErrorCode schedule(EventLoopHandle loop, ServiceState *state)
{
    if (state->restart_timer) (void)eventloop_remove_source(loop, state->restart_timer);
    ErrorCode error = time_deadline_after(state->delay, &state->restart_at);
    if (error == CORE_OK) error = eventloop_create_timer(loop, state->delay, 0, &state->restart_timer);
    if (error == CORE_OK) {
        state->pending = true;
        state->delay = init_restart_next(state->delay);
    }
    return error;
}
static void print(const char *value)
{
    usize size = 0; while (value[size]) ++size;
    (void)write(STDERR_FILENO, value, size);
}
int main(int argc, char **argv, char **envp)
{
    (void)argv; (void)envp;
    if (argc != 1) return PROCESS_EXIT_INVALID_ARGUMENT;
    if (validate_registry() != CORE_OK) return PROCESS_EXIT_INITIALIZATION_FAILED;
    if (initialize_gui_tokens() != CORE_OK)
        return PROCESS_EXIT_INITIALIZATION_FAILED;
    ServiceState states[64] = { 0 };
    EventLoopHandle loop = NULL;
    EventSourceId monitor = 0;
    int result = PROCESS_EXIT_INITIALIZATION_FAILED;
    bool journal_ready = false;
    struct sigaction action = { .sa_handler = stop_signal };
    if (sigaction(SIGTERM, &action, NULL) != 0 || sigaction(SIGINT, &action, NULL) != 0) return result;
    if (eventloop_create(&loop) != CORE_OK) return result;
    if (eventloop_create_timer(loop, INIT_MONITOR_MS, INIT_MONITOR_MS, &monitor) != CORE_OK) goto cleanup;
    for (usize i=0; i<init_registry.count; ++i) states[i].delay=INIT_MONITOR_MS;
    if (start_service(&init_registry.services[0], &states[0].process) != CORE_OK) goto cleanup;
    if (log_ready(loop, &states[0]) != CORE_OK) goto cleanup;
    journal_ready = true;
    if (trace(loop, "started", &init_registry.services[0], NULL, CORE_OK) != CORE_OK) goto cleanup;
    print("PUNos initserv: logserv ready\n");
    for (usize i=1; i<init_registry.count && !stopping; ++i) {
        const ServiceSpec *spec=&init_registry.services[i];
        if (!spec->enabled) continue;
        ErrorCode error=start_service(spec, &states[i].process);
        (void)trace(loop, error==CORE_OK?"started":"start failed", spec, NULL, error);
        if (error!=CORE_OK && spec->restart && schedule(loop,&states[i])!=CORE_OK) goto cleanup;
    }
    print("PUNos initserv: services started\n");
    result=PROCESS_EXIT_SUCCESS;
    Deadline next_poll;
    if (time_deadline_after(INIT_MONITOR_MS,&next_poll)!=CORE_OK) { result=PROCESS_EXIT_FAILURE; goto cleanup; }
    while (!stopping) {
        Event events[64]; usize count;
        ErrorCode error=eventloop_wait(loop,events,64,100,&count);
        if (error==CORE_INTERRUPTED) continue;
        if (error!=CORE_OK) { result=PROCESS_EXIT_FAILURE; break; }
        bool due;
        if (time_deadline_expired(next_poll,&due)!=CORE_OK) { result=PROCESS_EXIT_FAILURE; break; }
        if (due) {
            for (usize i=0;i<init_registry.count;++i) {
                if (states[i].process==NULL) continue;
                bool finished=false; ProcessExitStatus status;
                error=process_poll(&states[i].process,&finished,&status);
                if (error==CORE_INTERRUPTED) continue;
                if (error!=CORE_OK) { result=PROCESS_EXIT_FAILURE; stopping=1; break; }
                if (finished) {
                    states[i].last_exit=status; states[i].exit_pending=true;
                    if (i==0) journal_ready=false;
                    if (journal_ready) {
                        (void)trace(loop,"exited",&init_registry.services[i],&status,CORE_OK);
                        states[i].exit_pending=false;
                    }
                    if (init_registry.services[i].restart && schedule(loop,&states[i])!=CORE_OK) {
                        result=PROCESS_EXIT_FAILURE; stopping=1; break;
                    }
                }
            }
            if (time_deadline_after(INIT_MONITOR_MS,&next_poll)!=CORE_OK) { result=PROCESS_EXIT_FAILURE; break; }
        }
        for (usize i=0;i<init_registry.count && !stopping;++i) {
            if (!states[i].pending) continue;
            if (time_deadline_expired(states[i].restart_at,&due)!=CORE_OK) { result=PROCESS_EXIT_FAILURE; stopping=1; break; }
            if (!due || (i!=0 && (states[0].process==NULL || states[0].pending))) continue;
            states[i].pending=false;
            (void)eventloop_remove_source(loop,states[i].restart_timer); states[i].restart_timer=0;
            error=start_service(&init_registry.services[i],&states[i].process);
            if (error==CORE_OK) {
                states[i].delay=INIT_MONITOR_MS;
                if (i==0 && log_ready(loop,&states[0])!=CORE_OK) {
                    result=PROCESS_EXIT_INITIALIZATION_FAILED; stopping=1; break;
                }
                if (i==0) {
                    journal_ready=true;
                    for (usize j=0;j<init_registry.count;++j) if (states[j].exit_pending) {
                        (void)trace(loop,"exited",&init_registry.services[j],&states[j].last_exit,CORE_OK);
                        states[j].exit_pending=false;
                    }
                }
                (void)trace(loop,"restarted",&init_registry.services[i],NULL,CORE_OK);
            } else if (i==0) { result=PROCESS_EXIT_INITIALIZATION_FAILED; stopping=1; break; }
            else {
                (void)trace(loop,"restart failed",&init_registry.services[i],NULL,error);
                if (schedule(loop,&states[i])!=CORE_OK) { result=PROCESS_EXIT_FAILURE; stopping=1; break; }
            }
        }
    }
cleanup:
    /* No diagnostic IPC before logserv has actually been confirmed ready. */
    if (journal_ready && states[0].process!=NULL) {
        finishing_log=true;
        (void)trace(loop,"stopping",NULL,NULL,CORE_OK);
        finishing_log=false;
    }
    for (usize i=init_registry.count;i>0;--i) {
        ServiceState *state=&states[i-1];
        if (state->restart_timer) (void)eventloop_remove_source(loop,state->restart_timer);
        if (state->process!=NULL) (void)process_terminate(state->process);
    }
    for (usize i=init_registry.count;i>0;--i) {
        ServiceState *state=&states[i-1];
        if (state->process==NULL) continue;
        ProcessExitStatus status; ErrorCode error;
        do { error=process_wait(&state->process,&status); } while (error==CORE_INTERRUPTED);
        if (error!=CORE_OK) result=PROCESS_EXIT_FAILURE;
    }
    (void)eventloop_close(&loop);
    return result;
}
