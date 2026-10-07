#ifndef PUNOS_COMPOSITORSERV_H
#define PUNOS_COMPOSITORSERV_H

#include <sys/core.h>
#include <sys/string.h>
#include <sys/time.h>

#define COMPOSITOR_SERVICE_CONTROLLER_TOKEN_SIZE ((usize)32)
#define COMPOSITOR_SERVICE_PRESENTER_TOKEN_SIZE ((usize)32)

typedef struct CompositorServiceHandle *CompositorServiceHandle;

typedef struct CompositorServiceConfig {
    const String *endpoint;
    const u8 *controller_token;
    const String *display_endpoint;
    const u8 *presenter_token;
} CompositorServiceConfig;

ErrorCode compositor_service_create(
    const String *endpoint,
    const u8 controller_token[COMPOSITOR_SERVICE_CONTROLLER_TOKEN_SIZE],
    CompositorServiceHandle *out_service);
ErrorCode compositor_service_create_config(
    const CompositorServiceConfig *config,
    CompositorServiceHandle *out_service);
ErrorCode compositor_service_poll(CompositorServiceHandle service,
                                  DurationMs timeout_ms);
ErrorCode compositor_service_close(CompositorServiceHandle *service);

#endif
