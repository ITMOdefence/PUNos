#include <compositorserv.h>

#include <sys/eventloop.h>
#include <sys/process.h>

#define DEFAULT_ENDPOINT "/run/punos/compositorserv.sock"

static bool option_value(const char *argument, const char *name,
                         const char **out_value)
{
    usize index = 0;

    if (argument == NULL || name == NULL || out_value == NULL) return false;
    while (name[index] != '\0') {
        if (argument[index] != name[index]) return false;
        ++index;
    }
    if (argument[index] != '=') return false;
    *out_value = argument + index + 1;
    return true;
}

static i32 hex_digit(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static bool parse_token(const char *text,
                        u8 token[COMPOSITOR_SERVICE_CONTROLLER_TOKEN_SIZE])
{
    usize index;

    if (text == NULL) return false;
    for (index = 0; index < COMPOSITOR_SERVICE_CONTROLLER_TOKEN_SIZE; ++index) {
        i32 high;
        i32 low;

        if (text[index * 2] == '\0' || text[index * 2 + 1] == '\0') {
            return false;
        }
        high = hex_digit(text[index * 2]);
        low = hex_digit(text[index * 2 + 1]);
        if (high < 0 || low < 0) return false;
        token[index] = (u8)((high << 4) | low);
    }
    return text[COMPOSITOR_SERVICE_CONTROLLER_TOKEN_SIZE * 2] == '\0';
}

int main(int argc, char **argv, char **envp)
{
    const char *endpoint_text = DEFAULT_ENDPOINT;
    const char *controller_token_text = NULL;
    String endpoint = { NULL, 0 };
    CompositorServiceConfig config;
    CompositorServiceHandle service = NULL;
    u8 controller_token[COMPOSITOR_SERVICE_CONTROLLER_TOKEN_SIZE];
    i32 index;
    ErrorCode result;

    (void)envp;
    for (index = 1; index < argc; ++index) {
        const char *value;

        if (option_value(argv[index], "--endpoint", &value)) {
            endpoint_text = value;
        } else if (option_value(argv[index], "--controller-token", &value)) {
            controller_token_text = value;
        } else {
            return PROCESS_EXIT_INVALID_ARGUMENT;
        }
    }

    if (!parse_token(controller_token_text, controller_token)) {
        return PROCESS_EXIT_INVALID_ARGUMENT;
    }

    result = string_from_c_str(endpoint_text, &endpoint);
    if (result != CORE_OK) goto initialization_failed;

    config.endpoint = &endpoint;
    config.controller_token = controller_token;
    result = compositor_service_create_config(&config, &service);
    if (result != CORE_OK) goto initialization_failed;

    while ((result = compositor_service_poll(service, EVENTLOOP_WAIT_FOREVER)) ==
           CORE_OK) {
    }

    (void)compositor_service_close(&service);
    (void)string_destroy(&endpoint);
    return PROCESS_EXIT_FAILURE;

initialization_failed:
    (void)string_destroy(&endpoint);
    return PROCESS_EXIT_INITIALIZATION_FAILED;
}
