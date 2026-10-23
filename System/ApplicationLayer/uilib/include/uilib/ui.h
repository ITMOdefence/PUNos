#ifndef PUNOS_UILIB_UI_H
#define PUNOS_UILIB_UI_H

#include <appframework/app.h>
#include <gui/protocol.h>

#define UI_BUFFER_COUNT_DEFAULT ((u32)2)
#define UI_BUFFER_COUNT_MAX ((u32)4)

typedef struct UiContextHandle *UiContextHandle;
typedef struct UiViewHandle *UiViewHandle;

typedef GuiWindowRole UiViewRole;
#define UI_VIEW_ROLE_APP GUI_WINDOW_ROLE_APP
#define UI_VIEW_ROLE_DIALOG GUI_WINDOW_ROLE_DIALOG
#define UI_VIEW_ROLE_TRANSIENT GUI_WINDOW_ROLE_TRANSIENT

typedef GuiWindowFlags UiViewFlags;
#define UI_VIEW_EDGE_TO_EDGE GUI_WINDOW_FLAG_EDGE_TO_EDGE
#define UI_VIEW_ACCEPTS_KEYBOARD GUI_WINDOW_FLAG_ACCEPTS_KEYBOARD
#define UI_VIEW_HANDLES_BACK GUI_WINDOW_FLAG_HANDLES_BACK

typedef enum UiEventKind {
    UI_EVENT_READY = 1,
    UI_EVENT_CONFIGURE = 2,
    UI_EVENT_LIFECYCLE = 3,
    UI_EVENT_NAVIGATION = 4,
    UI_EVENT_SERVICE_ERROR = 5
} UiEventKind;

typedef struct UiEvent {
    UiEventKind kind;
    ErrorCode error;
    union {
        GuiWindowConfigureEvent configure;
        GuiWindowLifecycleEvent lifecycle;
        GuiWindowNavigationEvent navigation;
    };
} UiEvent;

typedef void (*UiEventHandler)(UiViewHandle view, const UiEvent *event,
                               void *context);

typedef struct UiConfig {
    u32 preferred_buffer_count;
} UiConfig;

typedef struct UiViewConfig {
    const char *title;
    UiViewRole role;
    UiViewHandle parent;
    UiViewFlags flags;
    UiEventHandler handler;
    void *context;
} UiViewConfig;

/*
 * Attaches the GUI clients to app's event loop. The returned context owns its
 * IPC connections, but not AppHandle. One context may exist for an AppHandle.
 */
ErrorCode ui_initialize(AppHandle app, const UiConfig *config,
                        UiContextHandle *out_context);

/* Queues asynchronous surface and window creation on the app event loop. */
ErrorCode ui_view_create(UiContextHandle context, const UiViewConfig *config,
                         UiViewHandle *out_view);

ErrorCode ui_view_destroy(UiViewHandle *view);

/* Removes event sources, closes both GUI connections and clears the handle. */
ErrorCode ui_shutdown(UiContextHandle *context);

#endif
