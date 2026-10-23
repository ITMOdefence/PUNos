#ifndef PUNOS_UILIB_UI_H
#define PUNOS_UILIB_UI_H

#include <appframework/app.h>

#define UI_BUFFER_COUNT_DEFAULT ((u32)2)
#define UI_BUFFER_COUNT_MAX ((u32)4)

typedef struct UiContextHandle *UiContextHandle;

typedef struct UiConfig {
    u32 preferred_buffer_count;
} UiConfig;

/*
 * Attaches the GUI clients to app's event loop. The returned context owns its
 * IPC connections, but not AppHandle. One context may exist for an AppHandle.
 */
ErrorCode ui_initialize(AppHandle app, const UiConfig *config,
                        UiContextHandle *out_context);

/* Removes event sources, closes both GUI connections and clears the handle. */
ErrorCode ui_shutdown(UiContextHandle *context);

#endif
