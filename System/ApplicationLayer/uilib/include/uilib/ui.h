#ifndef PUNOS_UILIB_UI_H
#define PUNOS_UILIB_UI_H

#include <appframework/app.h>
#include <gui/protocol.h>

#define UI_BUFFER_COUNT_DEFAULT ((u32)2)
#define UI_BUFFER_COUNT_MAX ((u32)4)

typedef struct UiContextHandle *UiContextHandle;
typedef struct UiViewHandle *UiViewHandle;
typedef struct UiBufferHandle *UiBufferHandle;

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
    UI_EVENT_SERVICE_ERROR = 5,
    UI_EVENT_FRAME_DONE = 6,
    UI_EVENT_REDRAW = 7,
    UI_EVENT_CLOSE_REQUESTED = 8,
    UI_EVENT_TOUCH = 9,
    UI_EVENT_POINTER = 10,
    UI_EVENT_KEY = 11,
    UI_EVENT_VISIBILITY = 12,
    UI_EVENT_BUFFER_AVAILABLE = 13
} UiEventKind;

typedef struct UiEvent {
    UiEventKind kind;
    ErrorCode error;
    union {
        GuiWindowConfigureEvent configure;
        GuiWindowLifecycleEvent lifecycle;
        GuiWindowNavigationEvent navigation;
        GuiSurfaceFrameDoneEvent frame;
        GuiSurfaceRedrawEvent redraw;
        GuiWindowCloseEvent close;
        GuiWindowTouchEvent touch;
        GuiWindowPointerEvent pointer;
        GuiWindowKeyEvent key;
        GuiWindowVisibilityEvent visibility;
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

typedef struct UiMappedBuffer {
    UiBufferHandle buffer;
    u8 *data;
    usize size;
    u32 width;
    u32 height;
    u32 stride;
    GuiPixelFormat pixel_format;
} UiMappedBuffer;

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

ErrorCode ui_view_acquire_buffer(UiViewHandle view,
                                 UiMappedBuffer *out_mapping);
ErrorCode ui_view_damage(UiViewHandle view, const GuiRect *rects,
                         usize count);
ErrorCode ui_view_present(UiViewHandle view, UiBufferHandle buffer);
ErrorCode ui_view_publish(UiViewHandle view);
ErrorCode ui_view_hide(UiViewHandle view);
ErrorCode ui_view_set_title(UiViewHandle view, const char *title);
ErrorCode ui_view_respond_close(UiViewHandle view, u64 close_serial,
                                GuiCloseResponse response);
ErrorCode ui_get_latest_configure(UiViewHandle view,
                                  GuiWindowConfigureEvent *out_configure);

/* Removes event sources, closes both GUI connections and clears the handle. */
ErrorCode ui_shutdown(UiContextHandle *context);

#endif
