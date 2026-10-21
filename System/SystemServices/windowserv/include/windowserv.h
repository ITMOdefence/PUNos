#ifndef PUNOS_WINDOWSERV_H
#define PUNOS_WINDOWSERV_H

#include <gui/protocol.h>
#include <sys/core.h>
#include <sys/string.h>
#include <sys/time.h>

#define WINDOW_SERVICE_SHELL_TOKEN_SIZE ((usize)32)
#define WINDOW_SERVICE_CONTROLLER_TOKEN_SIZE ((usize)32)

typedef struct WindowServiceHandle *WindowServiceHandle;

typedef struct WindowServiceConfig {
    const String *endpoint;
    const String *compositor_endpoint;
    const String *io_endpoint;
    const u8 *controller_token;
    const u8 *shell_token;
    u32 viewport_width;
    u32 viewport_height;
    u32 scale;
    GuiDisplayOrientation orientation;
    GuiInsets safe_insets;
} WindowServiceConfig;

ErrorCode window_service_create(
    const String *endpoint,
    const u8 shell_token[WINDOW_SERVICE_SHELL_TOKEN_SIZE],
    WindowServiceHandle *out_service);
ErrorCode window_service_create_config(
    const WindowServiceConfig *config,
    WindowServiceHandle *out_service);
ErrorCode window_service_poll(WindowServiceHandle service,
                              DurationMs timeout_ms);
ErrorCode window_service_close(WindowServiceHandle *service);

ErrorCode window_service_set_display_metrics(
    WindowServiceHandle service, u32 width, u32 height, u32 scale,
    GuiDisplayOrientation orientation, GuiInsets safe_insets);
ErrorCode window_service_route_touch(WindowServiceHandle service,
                                     const GuiWindowTouchEvent *event);
ErrorCode window_service_route_pointer(WindowServiceHandle service,
                                       const GuiWindowPointerEvent *event);
ErrorCode window_service_route_key(WindowServiceHandle service,
                                   const GuiWindowKeyEvent *event);

#endif
