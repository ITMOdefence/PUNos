#include <windowserv.h>

#include <sys/eventloop.h>
#include <sys/process.h>

#define DEFAULT_ENDPOINT "/run/punos/windowserv.sock"
#define DEFAULT_COMPOSITOR_ENDPOINT "/run/punos/compositorserv.sock"
#define DEFAULT_IO_ENDPOINT "/run/punos/ioserv.sock"

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

static bool parse_token(const char *text, u8 *token, usize size)
{
    usize index;

    if (text == NULL || token == NULL) return false;
    for (index = 0; index < size; ++index) {
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
    return text[size * 2] == '\0';
}

static bool parse_u32(const char *text, u32 *out_value)
{
    u32 value = 0;
    usize index = 0;

    if (text == NULL || out_value == NULL || text[0] == '\0') return false;
    while (text[index] != '\0') {
        u32 digit;
        if (text[index] < '0' || text[index] > '9') return false;
        digit = (u32)(text[index] - '0');
        if (value > (0xffffffffu - digit) / 10u) return false;
        value = value * 10u + digit;
        ++index;
    }
    *out_value = value;
    return true;
}

int main(int argc, char **argv, char **envp)
{
    const char *endpoint_text = DEFAULT_ENDPOINT;
    const char *compositor_endpoint_text = DEFAULT_COMPOSITOR_ENDPOINT;
    const char *io_endpoint_text = DEFAULT_IO_ENDPOINT;
    const char *shell_token_text = NULL;
    const char *controller_token_text = NULL;
    String endpoint = { NULL, 0 };
    String compositor_endpoint = { NULL, 0 };
    String io_endpoint = { NULL, 0 };
    u8 shell_token[WINDOW_SERVICE_SHELL_TOKEN_SIZE];
    u8 controller_token[WINDOW_SERVICE_CONTROLLER_TOKEN_SIZE];
    WindowServiceConfig config;
    WindowServiceHandle service = NULL;
    i32 index;
    ErrorCode result;

    (void)envp;
    config.viewport_width = 1080;
    config.viewport_height = 1920;
    config.scale = GUI_SCALE_1X;
    config.orientation = GUI_ORIENTATION_NORMAL;
    config.safe_insets = (GuiInsets){ 0, 0, 0, 0 };

    for (index = 1; index < argc; ++index) {
        const char *value;

        if (option_value(argv[index], "--endpoint", &value)) {
            endpoint_text = value;
        } else if (option_value(argv[index], "--compositor-endpoint", &value)) {
            compositor_endpoint_text = value;
        } else if (option_value(argv[index], "--io-endpoint", &value)) {
            io_endpoint_text = value;
        } else if (option_value(argv[index], "--shell-token", &value)) {
            shell_token_text = value;
        } else if (option_value(argv[index], "--controller-token", &value)) {
            controller_token_text = value;
        } else if (option_value(argv[index], "--width", &value)) {
            if (!parse_u32(value, &config.viewport_width)) {
                return PROCESS_EXIT_INVALID_ARGUMENT;
            }
        } else if (option_value(argv[index], "--height", &value)) {
            if (!parse_u32(value, &config.viewport_height)) {
                return PROCESS_EXIT_INVALID_ARGUMENT;
            }
        } else {
            return PROCESS_EXIT_INVALID_ARGUMENT;
        }
    }

    if (!parse_token(shell_token_text, shell_token,
                     WINDOW_SERVICE_SHELL_TOKEN_SIZE) ||
        !parse_token(controller_token_text, controller_token,
                     WINDOW_SERVICE_CONTROLLER_TOKEN_SIZE)) {
        return PROCESS_EXIT_INVALID_ARGUMENT;
    }
    result = string_from_c_str(endpoint_text, &endpoint);
    if (result == CORE_OK) {
        result = string_from_c_str(compositor_endpoint_text,
                                   &compositor_endpoint);
    }
    if (result == CORE_OK) {
        result = string_from_c_str(io_endpoint_text, &io_endpoint);
    }
    if (result != CORE_OK) goto initialization_failed;

    config.endpoint = &endpoint;
    config.compositor_endpoint = &compositor_endpoint;
    config.io_endpoint = &io_endpoint;
    config.controller_token = controller_token;
    config.shell_token = shell_token;
    result = window_service_create_config(&config, &service);
    if (result != CORE_OK) goto initialization_failed;

    while ((result = window_service_poll(service, EVENTLOOP_WAIT_FOREVER)) ==
           CORE_OK) {
    }

    (void)window_service_close(&service);
    (void)string_destroy(&io_endpoint);
    (void)string_destroy(&compositor_endpoint);
    (void)string_destroy(&endpoint);
    return PROCESS_EXIT_FAILURE;

initialization_failed:
    (void)window_service_close(&service);
    (void)string_destroy(&io_endpoint);
    (void)string_destroy(&compositor_endpoint);
    (void)string_destroy(&endpoint);
    return PROCESS_EXIT_INITIALIZATION_FAILED;
}
