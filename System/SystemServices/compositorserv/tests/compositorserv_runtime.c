#define CHECK_TEST_NAME "compositorserv"
#include "../../../SystemLibs/testing/check.h"

#include <compositorserv.h>
#include <special/ipc.h>

static void test_accept_connection(void)
{
    String endpoint;
    CompositorServiceHandle service;
    IpcConnectionHandle client;
    IpcConnectionState state = IPC_CONNECTION_CONNECTING;
    u8 token[COMPOSITOR_SERVICE_CONTROLLER_TOKEN_SIZE] = { 1 };
    usize attempt;

    EXPECT(1, string_from_c_str("/tmp/punos-compositorserv-test", &endpoint) ==
              CORE_OK);
    EXPECT(2, compositor_service_create(&endpoint, token, &service) == CORE_OK);
    EXPECT(3, ipc_connection_begin_connect(&endpoint, &client) == CORE_OK);

    for (attempt = 0; attempt < 1000 && state == IPC_CONNECTION_CONNECTING;
         ++attempt) {
        EXPECT(4, compositor_service_poll(service, 0) == CORE_OK);
        EXPECT(5, ipc_connection_finish_connect(client, &state) == CORE_OK);
    }

    EXPECT(6, state == IPC_CONNECTION_OPEN);
    EXPECT(7, ipc_connection_close(&client) == CORE_OK);
    EXPECT(8, compositor_service_poll(service, 0) == CORE_OK);
    EXPECT(9, compositor_service_close(&service) == CORE_OK);
    EXPECT(10, string_destroy(&endpoint) == CORE_OK);
}

int main(void)
{
    test_accept_connection();
    check_passed();
    return 0;
}
