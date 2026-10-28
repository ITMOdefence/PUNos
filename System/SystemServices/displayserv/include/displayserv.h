#ifndef PUNOS_DISPLAYSERV_H
#define PUNOS_DISPLAYSERV_H

#include <sys/core.h>
#include <sys/string.h>
#include <sys/time.h>

#define DISPLAY_SERVICE_PRESENTER_TOKEN_SIZE ((usize)32)

typedef struct DisplayServiceHandle *DisplayServiceHandle;

typedef struct DisplayServiceConfig {
    const String *endpoint;
    const String *backend_name;
    const String *device_path;
    const u8 *presenter_token;
} DisplayServiceConfig;

ErrorCode display_service_create(const String *endpoint,
                                 const u8 presenter_token[32],
                                 DisplayServiceHandle *out_service);
ErrorCode display_service_create_config(const DisplayServiceConfig *config,
                                        DisplayServiceHandle *out_service);
ErrorCode display_service_poll(DisplayServiceHandle service,
                               DurationMs timeout_ms);
ErrorCode display_service_close(DisplayServiceHandle *service);

#endif
