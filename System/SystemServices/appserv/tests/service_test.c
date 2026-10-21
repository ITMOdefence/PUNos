#define CHECK_TEST_NAME "appserv"
#include "check.h"
#include <appserv/appserv.h>
#include "wire.h"
#include "fakes.h"

#define TEXT(s) { (char *)(s), sizeof(s) - 1 }
#define REGISTRY_ID "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
static const String arguments[] = { TEXT("--fixed") };
static const String environment[] = { TEXT("MODE=fixed") };
static NativeAppSpec specs[] = {
    { TEXT("demo"), TEXT("Demo"), TEXT("/punos/bin/demo"), arguments, 1, environment, 1, true }
};
static NativeAppLaunchSpec launches[] = { { TEXT("demo"), TEXT("/punos/bin/demo") } };
static AppCatalog catalog = { TEXT(REGISTRY_ID), TEXT(REGISTRY_ID), launches, specs, 1 };
static usize logs;

static void log_event(void *context, const char *event, const NativeAppSpec *app,
                       AppInstanceId id, const ProcessExitStatus *status, ErrorCode error)
{
    (void)context; (void)event; (void)app; (void)id; (void)status; (void)error;
    ++logs;
}

static AppReply request(AppService *service, u32 type, Bytes bytes)
{
    AppReply reply;
    EXPECT(200, app_service_request(service, type, bytes, &reply) == CORE_OK);
    return reply;
}

static ErrorCode reply_error(AppReply *reply)
{
    ErrorCode error = (ErrorCode)wire_read(reply->payload.data, 4);
    EXPECT(201, memory_free(&reply->payload) == CORE_OK);
    return error;
}

static Bytes id_request(u8 *bytes, AppInstanceId id)
{
    wire_write(bytes, 2, 1);
    wire_write(bytes + 2, 8, id);
    return (Bytes){ bytes, 10 };
}

int main(int argc, char **argv, char **envp)
{
    AppService service;
    AppReply reply;
    u8 app_bytes[] = { 1, 0, 4, 0, 'd', 'e', 'm', 'o' };
    u8 instance_bytes[10];
    Bytes app_request = { app_bytes, sizeof(app_bytes) };
    (void)argc; (void)argv; (void)envp;
    fake_reset();
    EXPECT(1, app_catalog_validate(&catalog) == CORE_OK);
    AppCatalog invalid = catalog;
    invalid.expected_registry_id = (String)TEXT("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    EXPECT(2, app_catalog_validate(&invalid) == CORE_INVALID_ARGUMENT);
    invalid = catalog; invalid.count = 65;
    EXPECT(3, app_catalog_validate(&invalid) == CORE_INVALID_ARGUMENT);
    specs[0].terminates_on_sigterm = false;
    EXPECT(4, app_catalog_validate(&catalog) == CORE_INVALID_ARGUMENT);
    specs[0].terminates_on_sigterm = true;
    launches[0].executable = (String)TEXT("/wrong");
    EXPECT(5, app_catalog_validate(&catalog) == CORE_INVALID_ARGUMENT);
    launches[0].executable = specs[0].executable;
    NativeAppSpec duplicates[] = { specs[0], specs[0] };
    NativeAppLaunchSpec duplicate_launches[] = { launches[0], launches[0] };
    invalid = catalog; invalid.count = 2; invalid.applications = duplicates;
    invalid.launch_specs = duplicate_launches;
    EXPECT(6, app_catalog_validate(&invalid) == CORE_INVALID_ARGUMENT);
    specs[0].display_name = (String){ (char *)"\300\257", 2 };
    EXPECT(7, app_catalog_validate(&catalog) == CORE_INVALID_ARGUMENT);
    specs[0].display_name = (String)TEXT("Demo");
    EXPECT(8, app_service_init(&service, &catalog, log_event, NULL) == CORE_OK);

    reply = request(&service, APP_LIST, (Bytes){ NULL, 0 });
    EXPECT(9, reply.type == APP_LIST_RESULT && reply.payload.size == 18 &&
               wire_read(reply.payload.data + 4, 2) == 1 &&
               wire_read(reply.payload.data + 6, 2) == 4);
    EXPECT(10, reply_error(&reply) == CORE_OK);
    reply = request(&service, APP_GET_INFO, app_request);
    EXPECT(11, reply.type == APP_GET_INFO_RESULT && reply.payload.size == 16);
    EXPECT(12, reply_error(&reply) == CORE_OK);
    reply = request(&service, APP_LIST, app_request);
    EXPECT(13, reply_error(&reply) == CORE_INVALID_ARGUMENT);
    app_bytes[0] = 2;
    reply = request(&service, APP_LAUNCH, app_request);
    EXPECT(14, reply_error(&reply) == CORE_UNSUPPORTED && fake_starts == 0);
    app_bytes[0] = 1;
    reply = request(&service, APP_LAUNCH, (Bytes){ app_bytes, 7 });
    EXPECT(15, reply_error(&reply) == CORE_INVALID_ARGUMENT);
    app_bytes[4] = 'x';
    reply = request(&service, APP_GET_INFO, app_request);
    EXPECT(16, reply_error(&reply) == CORE_NOT_FOUND);
    app_bytes[4] = 0;
    reply = request(&service, APP_LAUNCH, app_request);
    EXPECT(17, reply_error(&reply) == CORE_INVALID_ARGUMENT);
    app_bytes[4] = 0xff;
    reply = request(&service, APP_LAUNCH, app_request);
    EXPECT(18, reply_error(&reply) == CORE_INVALID_ARGUMENT);
    app_bytes[4] = 'd';
    reply = request(&service, 99, (Bytes){ NULL, 0 });
    EXPECT(19, reply.type == 99 && reply_error(&reply) == CORE_INVALID_ARGUMENT);
    reply = request(&service, APP_STOP, id_request(instance_bytes, 0));
    EXPECT(20, reply_error(&reply) == CORE_INVALID_ARGUMENT);
    reply = request(&service, APP_GET_STATUS, id_request(instance_bytes, 999));
    EXPECT(21, reply_error(&reply) == CORE_NOT_FOUND);

    fake_allocate_after = 0;
    EXPECT(22, app_service_request(&service, APP_LIST, (Bytes){ NULL, 0 }, &reply) == CORE_OUT_OF_MEMORY &&
                reply.type == APP_LIST_RESULT);
    fake_allocate_after = 1; /* UTF-8 id succeeds, reply allocation fails. */
    EXPECT(23, app_service_request(&service, APP_LAUNCH, app_request, &reply) == CORE_OUT_OF_MEMORY &&
                fake_starts == 0 && reply.type == APP_LAUNCH_RESULT);
    fake_allocate_after = 2; /* Instance allocation fails, reply can be returned. */
    reply = request(&service, APP_LAUNCH, app_request);
    EXPECT(24, reply_error(&reply) == CORE_OUT_OF_MEMORY && fake_starts == 0);
    fake_allocate_after = -1;
    fake_start_error = CORE_PERMISSION_DENIED;
    reply = request(&service, APP_LAUNCH, app_request);
    EXPECT(25, reply_error(&reply) == CORE_IO_ERROR && service.instances == NULL);
    fake_start_error = CORE_OUT_OF_MEMORY;
    reply = request(&service, APP_LAUNCH, app_request);
    EXPECT(26, reply_error(&reply) == CORE_OUT_OF_MEMORY && service.instances == NULL);
    fake_start_error = CORE_OK;
    reply = request(&service, APP_LAUNCH, app_request);
    AppInstanceId first = wire_read(reply.payload.data + 4, 8);
    EXPECT(27, first == 1 && reply_error(&reply) == CORE_OK);
    EXPECT(28, fake_launch.executable == &specs[0].executable &&
                fake_launch.arguments == arguments && fake_launch.argument_count == 1 &&
                fake_launch.environment == environment && fake_launch.environment_count == 1);
    reply = request(&service, APP_GET_STATUS, id_request(instance_bytes, first));
    EXPECT(29, reply.payload.size == 10 && reply.payload.data[4] == APP_RUNNING && reply_error(&reply) == CORE_OK);
    EXPECT(30, app_service_poll(&service) == CORE_OK && fake_releases == 0 && fake_waits == 0);
    fake_terminate_error = CORE_IO_ERROR;
    reply = request(&service, APP_STOP, id_request(instance_bytes, first));
    EXPECT(31, reply_error(&reply) == CORE_IO_ERROR);
    fake_terminate_error = CORE_OK;
    reply = request(&service, APP_STOP, id_request(instance_bytes, first));
    EXPECT(32, reply_error(&reply) == CORE_OK && fake_waits == 0);
    reply = request(&service, APP_GET_STATUS, id_request(instance_bytes, first));
    EXPECT(33, reply.payload.data[4] == APP_TERMINATING && reply_error(&reply) == CORE_OK);
    fake_finished = true;
    fake_exit_status = (ProcessExitStatus){ PROCESS_TERMINATION_KIND_SIGNALED, 15 };
    EXPECT(34, app_service_poll(&service) == CORE_OK && fake_releases == 1);
    usize polls = fake_polls;
    EXPECT(35, app_service_poll(&service) == CORE_OK && fake_polls == polls && fake_releases == 1);
    reply = request(&service, APP_GET_STATUS, id_request(instance_bytes, first));
    EXPECT(36, reply.payload.data[4] == APP_EXITED && reply.payload.data[5] == 1 &&
                wire_read(reply.payload.data + 6, 4) == 15 && reply_error(&reply) == CORE_OK);
    reply = request(&service, APP_STOP, id_request(instance_bytes, first));
    EXPECT(37, reply_error(&reply) == CORE_INVALID_STATE);

    fake_finished = false;
    reply = request(&service, APP_LAUNCH, app_request);
    EXPECT(38, wire_read(reply.payload.data + 4, 8) == 2 && reply_error(&reply) == CORE_OK);
    reply = request(&service, APP_LAUNCH, app_request);
    EXPECT(39, wire_read(reply.payload.data + 4, 8) == 3 && reply_error(&reply) == CORE_OK);
    fake_terminate_error = CORE_PERMISSION_DENIED;
    usize waits = fake_waits;
    EXPECT(40, app_service_shutdown(&service) == CORE_PERMISSION_DENIED && fake_waits == waits &&
                service.instances != NULL && !service.ready);
    fake_terminate_error = CORE_OK;
    fake_wait_interrupt = true;
    EXPECT(41, app_service_shutdown(&service) == CORE_OK && service.instances == NULL &&
                fake_releases == 3 && fake_waits == waits + 3 && !fake_bad_ownership);
    reply = request(&service, APP_LAUNCH, app_request);
    EXPECT(42, reply_error(&reply) == CORE_INVALID_STATE);
    EXPECT(43, app_service_shutdown(&service) == CORE_OK && fake_releases == 3 && logs >= 10);
    fake_reset();
    EXPECT(44, app_service_init(&service, &catalog, NULL, NULL) == CORE_OK);
    service.next_id = 0;
    reply = request(&service, APP_LAUNCH, app_request);
    EXPECT(45, reply_error(&reply) == CORE_OUT_OF_MEMORY && fake_starts == 0);
    EXPECT(46, app_service_shutdown(&service) == CORE_OK);
    check_passed();
    return 0;
}
