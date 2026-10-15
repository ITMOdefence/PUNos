#include <camserv/protocol.h>
#include <special/camera.h>
#include <special/ipc.h>
#include <sys/eventloop.h>
#include <sys/memory.h>
#include <sys/process.h>
#include <sys/string.h>
#include <punos/syscall/syscall.h>
#include "../../common/logger.h"
#include "../../common/wire.h"

#ifndef CAM_ENDPOINT
#define CAM_ENDPOINT "/run/punos/camserv.sock"
#endif

typedef struct Client {
    MemoryBlock block;
    struct Client *next;
    IpcConnectionHandle connection;
    EventSourceId source;
    bool subscribed;
    bool buffered;
} Client;

typedef struct CameraService {
    EventLoopHandle loop;
    IpcServerHandle server;
    EventSourceId listener;
    CameraHandle camera;
    EventSourceId camera_source;
    CameraStreamConfig config;
    Client *clients;
    ServiceLogger logger;
    bool streaming;
    bool lost;
    bool failed;
} CameraService;

static volatile i32 stopping;
static void stop_signal(i32 number) { (void)number; stopping = 1; }

static bool begins(const char *text, const char *prefix)
{
    while (*prefix) if (*text++ != *prefix++) return false;
    return true;
}

static bool equal(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static bool number(const char *text, u32 *out)
{
    u32 value = 0;
    if (!*text) return false;
    while (*text) {
        if (*text < '0' || *text > '9' || value > (0xffffffffu - (u32)(*text - '0')) / 10)
            return false;
        value = value * 10 + (u32)(*text++ - '0');
    }
    if (!value) return false;
    *out = value;
    return true;
}

static bool arguments(int argc, char **argv, const char **path,
                      CameraStreamConfig *requested)
{
    u32 seen = 0;
    if (argc != 6) return false;
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (begins(arg, "--device=")) {
            if (seen & 1 || !arg[9]) return false;
            *path = arg + 9; seen |= 1;
        } else if (begins(arg, "--format=")) {
            if (seen & 2) return false;
            const char *value = arg + 9;
            if (equal(value, "Yuyv")) requested->format = CAMERA_PIXEL_FORMAT_YUYV;
            else if (equal(value, "Mjpeg")) requested->format = CAMERA_PIXEL_FORMAT_MJPEG;
            else if (equal(value, "Nv12")) requested->format = CAMERA_PIXEL_FORMAT_NV12;
            else return false;
            seen |= 2;
        } else if (begins(arg, "--width=")) {
            if (seen & 4 || !number(arg + 8, &requested->width)) return false;
            seen |= 4;
        } else if (begins(arg, "--height=")) {
            if (seen & 8 || !number(arg + 9, &requested->height)) return false;
            seen |= 8;
        } else if (begins(arg, "--fps=")) {
            if (seen & 16 || !number(arg + 6, &requested->frames_per_second)) return false;
            seen |= 16;
        } else return false;
    }
    return seen == 31;
}

static usize append_number(char *out, usize at, u32 value)
{
    char digits[10]; usize count = 0;
    do { digits[count++] = (char)('0' + value % 10); value /= 10; } while (value);
    while (count) out[at++] = digits[--count];
    return at;
}

static usize append_text(char *out, usize at, const char *value)
{
    while (*value) out[at++] = *value++;
    return at;
}

static void log_config(CameraService *service)
{
    char text[128]; usize at = 0;
    at = append_text(text, at, "started format=");
    at = append_text(text, at, service->config.format == CAMERA_PIXEL_FORMAT_YUYV ? "Yuyv" :
                      service->config.format == CAMERA_PIXEL_FORMAT_MJPEG ? "Mjpeg" : "Nv12");
    at = append_text(text, at, " width="); at = append_number(text, at, service->config.width);
    at = append_text(text, at, " height="); at = append_number(text, at, service->config.height);
    at = append_text(text, at, " fps="); at = append_number(text, at, service->config.frames_per_second);
    at = append_text(text, at, " max_frame_bytes=");
    at = append_number(text, at, service->config.max_frame_bytes);
    text[at] = '\0';
    (void)service_logger_record(&service->logger, text, CORE_OK);
}

static void close_client(CameraService *service, Client **link)
{
    Client *client = *link;
    bool pending;
    (void)ipc_connection_flush(client->connection, &pending);
    (void)eventloop_remove_source(service->loop, client->source);
    (void)ipc_connection_close(&client->connection);
    *link = client->next;
    MemoryBlock block = client->block;
    (void)memory_free(&block);
}

static ErrorCode queue(Client *client, u32 type, u8 *bytes, usize size)
{
    IpcMessage message = { type, { bytes, size } };
    return ipc_connection_send(client->connection, &message);
}

static ErrorCode flush_client(CameraService *service, Client *client)
{
    bool pending;
    ErrorCode error = ipc_connection_flush(client->connection, &pending);
    if (error != CORE_OK) return error;
    return eventloop_update_fd(service->loop, client->source,
                               EVENT_READABLE | (pending ? EVENT_WRITABLE : 0));
}

static ErrorCode accept_clients(CameraService *service)
{
    for (usize i = 0; i < 64 && !stopping; ++i) {
        IpcConnectionHandle connection;
        ErrorCode error = ipc_server_accept(service->server, &connection);
        if (error == CORE_WOULD_BLOCK) return CORE_OK;
        if (error != CORE_OK) return error;
        MemoryBlock block;
        error = memory_allocate(sizeof(Client), &block);
        if (error != CORE_OK) { (void)ipc_connection_close(&connection); return error; }
        Client *client = (Client *)block.data;
        *client = (Client){ .block = block, .connection = connection };
        i32 fd;
        error = ipc_connection_get_fd(connection, &fd);
        if (error == CORE_OK)
            error = eventloop_add_fd(service->loop, fd, EVENT_READABLE, &client->source);
        if (error != CORE_OK) {
            (void)ipc_connection_close(&connection); (void)memory_free(&block);
            return error;
        }
        client->next = service->clients;
        service->clients = client;
    }
    return CORE_OK;
}

static ErrorCode request(CameraService *service, Client *client, const IpcMessage *message)
{
    u32 result_type;
    ErrorCode error = cam_request_error(message, &result_type);
    u8 bytes[CAM_INFO_SIZE]; usize size = 4;
    if (error == CORE_OK && message->type == CAM_SUBSCRIBE && client->subscribed)
        error = CORE_INVALID_STATE;
    if (error == CORE_OK && message->type == CAM_UNSUBSCRIBE && !client->subscribed)
        error = CORE_INVALID_STATE;
    if (error != CORE_OK) (void)service_logger_record(&service->logger, "rejected IPC request", error);
    if (result_type == CAM_GET_INFO_RESULT) {
        cam_encode_info(bytes, error, error == CORE_OK ? &service->config : NULL);
        size = CAM_INFO_SIZE;
    } else wire_write(bytes, 4, error);
    ErrorCode sent = queue(client, result_type, bytes, size);
    if (sent != CORE_OK) return sent;
    if (error == CORE_OK && message->type == CAM_SUBSCRIBE) {
        cam_encode_config(bytes, &service->config);
        sent = queue(client, CAM_READY, bytes, CAM_CONFIG_SIZE);
        if (sent == CORE_OK) client->subscribed = true;
    } else if (error == CORE_OK && message->type == CAM_UNSUBSCRIBE)
        client->subscribed = false;
    return sent;
}

static ErrorCode serve_client(CameraService *service, Client *client)
{
    client->buffered = true;
    for (usize i = 0; i < 64 && !stopping; ++i) {
        IpcReceiveStatus status;
        IpcMessage message = { 0 };
        ErrorCode error = ipc_connection_receive(client->connection, &status, &message);
        if (error != CORE_OK) {
            (void)service_logger_record(&service->logger, "IPC receive failed", error);
            return error;
        }
        if (status == IPC_RECEIVE_STATUS_PEER_CLOSED) return CORE_NOT_FOUND;
        if (status == IPC_RECEIVE_STATUS_NO_MESSAGE) { client->buffered = false; break; }
        error = request(service, client, &message);
        (void)ipc_message_destroy(&message);
        if (error != CORE_OK) return error;
    }
    return flush_client(service, client);
}

static void broadcast(CameraService *service, u32 type, u8 *bytes, usize size)
{
    Client **link = &service->clients;
    while (*link) {
        Client *client = *link;
        ErrorCode error = CORE_OK;
        if (client->subscribed) {
            error = queue(client, type, bytes, size);
            if (error == CORE_OK) error = flush_client(service, client);
        }
        if (error != CORE_OK) {
            (void)service_logger_record(&service->logger, "slow or failed client", error);
            close_client(service, link);
        } else link = &client->next;
    }
}

static void read_camera(CameraService *service)
{
    while (!stopping) {
        CameraReadStatus status;
        CameraFrame frame = { 0 };
        ErrorCode error = camera_dequeue_frame(service->camera, &status, &frame);
        if (error != CORE_OK) {
            (void)service_logger_record(&service->logger, "camera read failed", error);
            service->failed = true;
            return;
        }
        if (status == CAMERA_READ_STATUS_NO_FRAME) return;
        if (status == CAMERA_READ_STATUS_DEVICE_LOST) {
            u8 reason[4]; wire_write(reason, 4, CORE_IO_ERROR);
            broadcast(service, CAM_DEVICE_LOST, reason, 4);
            (void)service_logger_record(&service->logger, "camera device lost", CORE_IO_ERROR);
            service->lost = true;
            return;
        }
        if (frame.data.size > service->config.max_frame_bytes ||
            frame.data.size > IPC_MESSAGE_MAX_SIZE - CAM_FRAME_HEADER_SIZE) {
            (void)service_logger_record(&service->logger, "oversized camera frame", CORE_OVERFLOW);
        } else if (service->clients) {
            MemoryBlock block;
            usize size;
            error = memory_allocate(CAM_FRAME_HEADER_SIZE + frame.data.size, &block);
            if (error == CORE_OK) {
                error = cam_encode_frame(block.data, block.size, &frame, &size);
                if (error == CORE_OK) broadcast(service, CAM_FRAME, block.data, size);
                (void)memory_free(&block);
            }
            if (error != CORE_OK)
                (void)service_logger_record(&service->logger, "camera frame failed", error);
        }
        error = camera_frame_release(service->camera, &frame);
        if (error != CORE_OK) {
            (void)service_logger_record(&service->logger, "camera frame release failed", error);
            service->failed = true;
            return;
        }
    }
}

int main(int argc, char **argv, char **envp)
{
    (void)envp;
    CameraService service = { 0 };
    CameraStreamConfig requested = { 0 };
    const char *device_path = NULL;
    if (!arguments(argc, argv, &device_path, &requested)) return PROCESS_EXIT_INVALID_ARGUMENT;
    int result = PROCESS_EXIT_INITIALIZATION_FAILED;
    ErrorCode error;
    struct sigaction action = { .sa_handler = stop_signal };
    String path = { 0 };
    String endpoint = { (char *)CAM_ENDPOINT, sizeof(CAM_ENDPOINT)-1 };
    if (sigaction(SIGTERM, &action, NULL) != 0 || sigaction(SIGINT, &action, NULL) != 0)
        goto cleanup;
    error = eventloop_create(&service.loop);
    if (error != CORE_OK) goto cleanup;
    error = service_logger_open(&service.logger, service.loop, "camserv");
    if (error != CORE_OK) goto cleanup;
    for (usize tries = 0; !service.logger.connected && tries < 50 && !stopping; ++tries) {
        Event events[4]; usize count;
        error = eventloop_wait(service.loop, events, 4, 100, &count);
        if (error != CORE_OK) goto cleanup;
        for (usize i = 0; i < count; ++i)
            if (events[i].source_id == service.logger.source &&
                service_logger_event(&service.logger) != CORE_OK) goto cleanup;
    }
    if (!service.logger.connected || stopping) goto cleanup;
    error = string_from_c_str(device_path, &path);
    if (error != CORE_OK) goto cleanup;
    CameraConfig camera_config = { &path };
    error = camera_open(&camera_config, &service.camera);
    if (error != CORE_OK) {
        (void)service_logger_record(&service.logger, "camera open failed", error);
        goto cleanup;
    }
    error = camera_configure(service.camera, &requested, &service.config);
    if (error != CORE_OK || service.config.max_frame_bytes == 0 ||
        service.config.max_frame_bytes > IPC_MESSAGE_MAX_SIZE - CAM_FRAME_HEADER_SIZE) {
        (void)service_logger_record(&service.logger, "camera configuration failed",
                                    error == CORE_OK ? CORE_UNSUPPORTED : error);
        goto cleanup;
    }
    error = camera_start(service.camera);
    if (error != CORE_OK) {
        (void)service_logger_record(&service.logger, "camera start failed", error);
        goto cleanup;
    }
    service.streaming = true;
    i32 fd;
    error = camera_get_fd(service.camera, &fd);
    if (error == CORE_OK)
        error = eventloop_add_fd(service.loop, fd, EVENT_READABLE, &service.camera_source);
    if (error == CORE_OK) error = ipc_server_create(&endpoint, &service.server);
    if (error == CORE_OK) error = ipc_server_get_fd(service.server, &fd);
    if (error == CORE_OK)
        error = eventloop_add_fd(service.loop, fd, EVENT_READABLE, &service.listener);
    if (error != CORE_OK) {
        (void)service_logger_record(&service.logger, "camera endpoint failed", error);
        goto cleanup;
    }
    log_config(&service);
    result = PROCESS_EXIT_SUCCESS;
    while (!stopping && !service.lost && !service.failed) {
        Event events[32]; usize count;
        bool buffered = false;
        for (Client *c = service.clients; c; c = c->next) if (c->buffered) buffered = true;
        error = eventloop_wait(service.loop, events, 32, buffered ? 0 : 100, &count);
        if (error == CORE_INTERRUPTED) continue;
        if (error != CORE_OK) { result = PROCESS_EXIT_FAILURE; break; }
        for (usize i = 0; i < count && !stopping && !service.lost && !service.failed; ++i) {
            EventSourceId source = events[i].source_id;
            if (source == service.listener) {
                if (accept_clients(&service) != CORE_OK) { result = PROCESS_EXIT_FAILURE; stopping = 1; }
            } else if (source == service.camera_source) read_camera(&service);
            else if (source == service.logger.source) (void)service_logger_event(&service.logger);
            else {
                Client **link = &service.clients;
                while (*link && (*link)->source != source) link = &(*link)->next;
                if (*link && serve_client(&service, *link) != CORE_OK) close_client(&service, link);
            }
        }
        Client **link = &service.clients;
        while (*link) {
            Client *client = *link;
            if (client->buffered && serve_client(&service, client) != CORE_OK)
                close_client(&service, link);
            else link = &client->next;
        }
    }
    if (service.lost || service.failed) result = PROCESS_EXIT_FAILURE;
cleanup:
    if (service.listener) (void)eventloop_remove_source(service.loop, service.listener);
    while (service.clients) close_client(&service, &service.clients);
    if (service.camera_source) (void)eventloop_remove_source(service.loop, service.camera_source);
    if (service.camera) {
        if (service.failed) {
            (void)camera_abort(&service.camera);
        } else {
            if (service.streaming && !service.lost) (void)camera_stop(service.camera);
            if (camera_close(&service.camera) == CORE_INVALID_STATE)
                (void)camera_abort(&service.camera);
        }
    }
    if (service.server) (void)ipc_server_close(&service.server);
    if (service.logger.connected) {
        if (!service.lost) (void)service_logger_record(&service.logger, "stopped", CORE_OK);
        service_logger_drain(&service.logger);
    }
    if (service.logger.connection) service_logger_close(&service.logger);
    if (service.loop) (void)eventloop_close(&service.loop);
    if (path.data) (void)string_destroy(&path);
    return result;
}
