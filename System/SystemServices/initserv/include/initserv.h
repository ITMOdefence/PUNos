#ifndef PUNOS_INITSERV_H
#define PUNOS_INITSERV_H
#include <sys/process.h>
#include <sys/eventloop.h>
typedef struct ServiceSpec {
    String service_id;
    ProcessLaunch launch;
    bool enabled, critical, restart;
} ServiceSpec;
typedef struct PlatformRegistry {
    String id;
    const ServiceSpec *services;
    usize count;
} PlatformRegistry;
#define INIT_MONITOR_MS 1000
#define INIT_RESTART_MAX_MS 30000
static inline DurationMs init_restart_next(DurationMs delay)
{ return delay < 15000 ? delay * 2 : INIT_RESTART_MAX_MS; }
#ifndef INIT_STARTUP_TIMEOUT_MS
#define INIT_STARTUP_TIMEOUT_MS 5000
#endif
#ifndef INIT_LOG_ENDPOINT
#define INIT_LOG_ENDPOINT "/run/punos/logserv.sock"
#endif
extern const PlatformRegistry init_registry;
#endif
