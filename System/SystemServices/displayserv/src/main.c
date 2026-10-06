#include <displayserv.h>

#include <sys/eventloop.h>
#include <sys/process.h>

#define DEFAULT_ENDPOINT "/run/punos/displayserv.sock"
#define DEFAULT_BACKEND "software"

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

static bool parse_token(const char *text, u8 token[32])
{
    usize index;
    if (text == NULL) return false;
    for (index = 0; index < 32; ++index) {
        if (text[index * 2] == '\0' || text[index * 2 + 1] == '\0') {
            return false;
        }
        i32 high = hex_digit(text[index * 2]);
        i32 low = hex_digit(text[index * 2 + 1]);
        if (high < 0 || low < 0) return false;
        token[index] = (u8)((high << 4) | low);
    }
    return text[64] == '\0';
}

int main(int argc, char **argv, char **envp)
{
    const char *endpoint_text = DEFAULT_ENDPOINT;
    const char *backend_text = DEFAULT_BACKEND;
    const char *device_text = NULL;
    const char *token_text = NULL;
    String endpoint = { NULL, 0 };
    String backend = { NULL, 0 };
    String device = { NULL, 0 };
    DisplayServiceConfig config;
    DisplayServiceHandle service = NULL;
    u8 token[32];
    i32 index;
    ErrorCode result;

    (void)envp;
    for (index = 1; index < argc; ++index) {
        const char *value;
        if (option_value(argv[index], "--endpoint", &value)) {
            endpoint_text = value;
        } else if (option_value(argv[index], "--backend", &value)) {
            backend_text = value;
        } else if (option_value(argv[index], "--device", &value)) {
            device_text = value;
        } else if (option_value(argv[index], "--presenter-token", &value)) {
            token_text = value;
        } else {
            return PROCESS_EXIT_INVALID_ARGUMENT;
        }
    }
    if (!parse_token(token_text, token)) return PROCESS_EXIT_INVALID_ARGUMENT;
    result = string_from_c_str(endpoint_text, &endpoint);
    if (result == CORE_OK) result = string_from_c_str(backend_text, &backend);
    if (result == CORE_OK && device_text != NULL) {
        result = string_from_c_str(device_text, &device);
    }
    if (result != CORE_OK) goto initialization_failed;

    config.endpoint = &endpoint;
    config.backend_name = &backend;
    config.device_path = device_text == NULL ? NULL : &device;
    config.presenter_token = token;
    result = display_service_create_config(&config, &service);
    if (result != CORE_OK) goto initialization_failed;
    while ((result = display_service_poll(service, EVENTLOOP_WAIT_FOREVER)) ==
           CORE_OK) {
    }
    (void)display_service_close(&service);
    (void)string_destroy(&device);
    (void)string_destroy(&backend);
    (void)string_destroy(&endpoint);
    return PROCESS_EXIT_FAILURE;

initialization_failed:
    (void)string_destroy(&device);
    (void)string_destroy(&backend);
    (void)string_destroy(&endpoint);
    return PROCESS_EXIT_INITIALIZATION_FAILED;
}
