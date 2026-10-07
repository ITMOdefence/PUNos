#ifndef PUNOS_GUI_PROTOCOL_H
#define PUNOS_GUI_PROTOCOL_H

#include <special/ipc.h>

#define GUI_PROTOCOL_VERSION ((u16)1)
#define GUI_MAX_DAMAGE_RECTS ((u16)64)
#define GUI_FULL_DAMAGE ((u32)1)
#define GUI_SURFACE_BIND_TOKEN_SIZE ((usize)16)

/* compositorserv: surface protocol, message types 1-19 */
#define GUI_SURFACE_CREATE                  ((IpcMessageType)1)
#define GUI_SURFACE_CREATE_RESULT           ((IpcMessageType)2)
#define GUI_SURFACE_DESTROY                 ((IpcMessageType)3)
#define GUI_SURFACE_DESTROY_RESULT          ((IpcMessageType)4)
#define GUI_SURFACE_BUFFER_IMPORT           ((IpcMessageType)5)
#define GUI_SURFACE_BUFFER_IMPORT_RESULT    ((IpcMessageType)6)
#define GUI_SURFACE_BUFFER_DESTROY          ((IpcMessageType)7)
#define GUI_SURFACE_BUFFER_DESTROY_RESULT   ((IpcMessageType)8)
#define GUI_SURFACE_ATTACH                  ((IpcMessageType)9)
#define GUI_SURFACE_DAMAGE                  ((IpcMessageType)10)
#define GUI_SURFACE_SET_OPAQUE              ((IpcMessageType)11)
#define GUI_SURFACE_REQUEST_FRAME           ((IpcMessageType)12)
#define GUI_SURFACE_COMMIT                  ((IpcMessageType)13)
#define GUI_SURFACE_COMMIT_RESULT           ((IpcMessageType)14)
#define GUI_SURFACE_FRAME_DONE              ((IpcMessageType)15)
#define GUI_SURFACE_BUFFER_RELEASED         ((IpcMessageType)16)
#define GUI_SURFACE_OUTPUT_ENTER            ((IpcMessageType)17)
#define GUI_SURFACE_OUTPUT_LEAVE            ((IpcMessageType)18)
#define GUI_SURFACE_FULL_REDRAW             ((IpcMessageType)19)

#define GUI_CONTROLLER_TOKEN_SIZE ((usize)32)

/* compositorserv: scene protocol, message types 20-29 */
#define GUI_COMPOSITOR_BIND_CONTROLLER        ((IpcMessageType)20)
#define GUI_COMPOSITOR_BIND_CONTROLLER_RESULT ((IpcMessageType)21)
#define GUI_SCENE_BIND_WINDOW                  ((IpcMessageType)22)
#define GUI_SCENE_SET_GEOMETRY                 ((IpcMessageType)23)
#define GUI_SCENE_SET_VISIBILITY               ((IpcMessageType)24)
#define GUI_SCENE_SET_STACK                    ((IpcMessageType)25)
#define GUI_SCENE_SET_OUTPUT                   ((IpcMessageType)26)
#define GUI_SCENE_REMOVE_WINDOW                ((IpcMessageType)27)
#define GUI_SCENE_RESULT                       ((IpcMessageType)28)
#define GUI_SCENE_BINDING_LOST                 ((IpcMessageType)29)

/* windowserv: window protocol, message types 1-30 */
#define GUI_WINDOW_CREATE               ((IpcMessageType)1)
#define GUI_WINDOW_CREATE_RESULT        ((IpcMessageType)2)
#define GUI_WINDOW_DESTROY              ((IpcMessageType)3)
#define GUI_WINDOW_DESTROY_RESULT       ((IpcMessageType)4)
#define GUI_WINDOW_SHOW                 ((IpcMessageType)5)
#define GUI_WINDOW_HIDE                 ((IpcMessageType)6)
#define GUI_WINDOW_SET_TITLE            ((IpcMessageType)7)
#define GUI_WINDOW_ACK_CONFIGURE        ((IpcMessageType)10)
#define GUI_WINDOW_SET_MODAL            ((IpcMessageType)13)
#define GUI_WINDOW_OPERATION_RESULT     ((IpcMessageType)14)
#define GUI_WINDOW_CONFIGURE            ((IpcMessageType)15)
#define GUI_WINDOW_LIFECYCLE_CHANGED    ((IpcMessageType)16)
#define GUI_WINDOW_CLOSE_REQUESTED      ((IpcMessageType)17)
#define GUI_WINDOW_VISIBILITY_CHANGED   ((IpcMessageType)18)
#define GUI_WINDOW_POINTER              ((IpcMessageType)19)
#define GUI_WINDOW_KEY                  ((IpcMessageType)20)
#define GUI_WINDOW_TOUCH                ((IpcMessageType)21)
#define GUI_WINDOW_RENDERING_SUSPENDED  ((IpcMessageType)22)
#define GUI_WINDOW_SURFACE_REBIND       ((IpcMessageType)23)
#define GUI_WINDOW_BIND_SHELL           ((IpcMessageType)24)
#define GUI_WINDOW_BIND_SHELL_RESULT    ((IpcMessageType)25)
#define GUI_WINDOW_RESPOND_CLOSE        ((IpcMessageType)26)
#define GUI_SHELL_ACTIVATE              ((IpcMessageType)27)
#define GUI_SHELL_SET_INSETS            ((IpcMessageType)28)
#define GUI_SHELL_NAVIGATE              ((IpcMessageType)29)
#define GUI_WINDOW_NAVIGATION_REQUESTED ((IpcMessageType)30)

#define GUI_WINDOW_TITLE_MAX ((u16)256)
#define GUI_SHELL_TOKEN_SIZE ((usize)32)
#define GUI_SCALE_1X ((u32)65536)

#define GUI_DISPLAY_PRESENT ((IpcMessageType)7)
#define GUI_DISPLAY_PRESENT_RESULT ((IpcMessageType)8)
#define GUI_DISPLAY_SUBSCRIBE_RESULT ((IpcMessageType)10)
#define GUI_DISPLAY_PRESENTED ((IpcMessageType)14)
#define GUI_DISPLAY_PRESENT_FAILED ((IpcMessageType)15)
#define GUI_DISPLAY_BACKEND_LOST ((IpcMessageType)16)
#define GUI_DISPLAY_BIND_PRESENTER ((IpcMessageType)17)
#define GUI_DISPLAY_BIND_PRESENTER_RESULT ((IpcMessageType)18)
#define GUI_DISPLAY_CONFIGURE ((IpcMessageType)5)
#define GUI_DISPLAY_LIST ((IpcMessageType)1)
#define GUI_DISPLAY_LIST_RESULT ((IpcMessageType)2)
#define GUI_DISPLAY_GET_STATE ((IpcMessageType)3)
#define GUI_DISPLAY_GET_STATE_RESULT ((IpcMessageType)4)
#define GUI_DISPLAY_CONFIGURE_RESULT ((IpcMessageType)6)
#define GUI_DISPLAY_SUBSCRIBE ((IpcMessageType)9)
#define GUI_DISPLAY_ADDED ((IpcMessageType)11)
#define GUI_DISPLAY_REMOVED ((IpcMessageType)12)
#define GUI_DISPLAY_CHANGED ((IpcMessageType)13)
#define GUI_PRESENTER_TOKEN_SIZE ((usize)32)
#define GUI_DISPLAY_NAME_MAX ((u16)128)
#define GUI_DISPLAY_MODE_MAX ((u16)128)
#define GUI_DISPLAY_LIST_MAX ((u16)32)

/* ioserv protocol, message types 1-7 */
#define GUI_IO_SUBSCRIBE ((IpcMessageType)1)
#define GUI_IO_SUBSCRIBE_RESULT ((IpcMessageType)2)
#define GUI_IO_UNSUBSCRIBE ((IpcMessageType)3)
#define GUI_IO_UNSUBSCRIBE_RESULT ((IpcMessageType)4)
#define GUI_IO_INPUT_FRAME ((IpcMessageType)5)
#define GUI_IO_DEVICE_ADDED ((IpcMessageType)6)
#define GUI_IO_DEVICE_REMOVED ((IpcMessageType)7)
#define GUI_IO_INPUT_FRAME_MAX_EVENTS ((u16)256)

typedef u32 GuiDisplayFlags;
#define GUI_DISPLAY_CONNECTED ((GuiDisplayFlags)1)
#define GUI_DISPLAY_ENABLED ((GuiDisplayFlags)2)
#define GUI_DISPLAY_PRIMARY ((GuiDisplayFlags)4)

typedef u32 GuiPixelFormat;
#define GUI_PIXEL_XRGB8888 ((GuiPixelFormat)1)
#define GUI_PIXEL_ARGB8888 ((GuiPixelFormat)2)

typedef u32 GuiInputDeviceClass;
#define GUI_INPUT_DEVICE_KEYBOARD ((GuiInputDeviceClass)1)
#define GUI_INPUT_DEVICE_POINTER ((GuiInputDeviceClass)2)
#define GUI_INPUT_DEVICE_TOUCH ((GuiInputDeviceClass)4)

typedef u8 GuiIoInputEventKind;
#define GUI_IO_INPUT_KEY ((GuiIoInputEventKind)1)
#define GUI_IO_INPUT_RELATIVE_AXIS ((GuiIoInputEventKind)2)
#define GUI_IO_INPUT_ABSOLUTE_AXIS ((GuiIoInputEventKind)3)
#define GUI_IO_INPUT_SYNC_REPORT ((GuiIoInputEventKind)4)
#define GUI_IO_INPUT_SYNC_LOST ((GuiIoInputEventKind)5)
#define GUI_IO_INPUT_RAW ((GuiIoInputEventKind)6)

typedef struct GuiInsets {
    u32 top;
    u32 right;
    u32 bottom;
    u32 left;
} GuiInsets;

typedef struct GuiRequestPrefix {
    u16 protocol_version;
    u64 request_id;
} GuiRequestPrefix;

typedef struct GuiRect {
    i32 x;
    i32 y;
    u32 width;
    u32 height;
} GuiRect;

typedef struct GuiBufferImportRequest {
    GuiRequestPrefix prefix;
    u32 width;
    u32 height;
    u32 stride;
    GuiPixelFormat pixel_format;
    u64 byte_size;
} GuiBufferImportRequest;

typedef struct GuiSurfaceDamageRequest {
    GuiRequestPrefix prefix;
    u64 surface_id;
    u32 flags;
    u16 rect_count;
    GuiRect rects[GUI_MAX_DAMAGE_RECTS];
} GuiSurfaceDamageRequest;

typedef struct GuiSurfaceIdRequest {
    GuiRequestPrefix prefix;
    u64 surface_id;
} GuiSurfaceIdRequest;

typedef struct GuiBufferIdRequest {
    GuiRequestPrefix prefix;
    u64 buffer_id;
} GuiBufferIdRequest;

typedef struct GuiSurfaceAttachRequest {
    GuiRequestPrefix prefix;
    u64 surface_id;
    u64 buffer_id;
} GuiSurfaceAttachRequest;

typedef struct GuiSurfaceRegionRequest {
    GuiRequestPrefix prefix;
    u64 surface_id;
    u16 rect_count;
    GuiRect rects[GUI_MAX_DAMAGE_RECTS];
} GuiSurfaceRegionRequest;

typedef struct GuiSurfaceFrameRequest {
    GuiRequestPrefix prefix;
    u64 surface_id;
} GuiSurfaceFrameRequest;

typedef struct GuiSurfaceCommitRequest {
    GuiRequestPrefix prefix;
    u64 surface_id;
    u64 commit_serial;
} GuiSurfaceCommitRequest;


typedef struct GuiSceneRect {
    i32 x;
    i32 y;
    u32 width;
    u32 height;
} GuiSceneRect;

typedef u32 GuiSceneVisibility;
#define GUI_SCENE_HIDDEN ((GuiSceneVisibility)0)
#define GUI_SCENE_VISIBLE ((GuiSceneVisibility)1)

typedef u32 GuiSceneLayer;
#define GUI_SCENE_LAYER_BACKGROUND ((GuiSceneLayer)1)
#define GUI_SCENE_LAYER_APP ((GuiSceneLayer)2)
#define GUI_SCENE_LAYER_DIALOG ((GuiSceneLayer)3)
#define GUI_SCENE_LAYER_SYSTEM ((GuiSceneLayer)4)
#define GUI_SCENE_LAYER_LOCK ((GuiSceneLayer)5)

typedef u32 GuiSceneBindingLostReason;
#define GUI_SCENE_BINDING_LOST_SURFACE_DESTROYED \
    ((GuiSceneBindingLostReason)1)
#define GUI_SCENE_BINDING_LOST_CLIENT_DISCONNECTED \
    ((GuiSceneBindingLostReason)2)

typedef struct GuiControllerBindRequest {
    GuiRequestPrefix prefix;
    u8 controller_token[GUI_CONTROLLER_TOKEN_SIZE];
} GuiControllerBindRequest;

typedef struct GuiSceneBindRequest {
    GuiRequestPrefix prefix;
    u64 window_id;
    u64 surface_id;
    u8 surface_bind_token[GUI_SURFACE_BIND_TOKEN_SIZE];
} GuiSceneBindRequest;

typedef struct GuiSceneGeometryRequest {
    GuiRequestPrefix prefix;
    u64 window_id;
    GuiSceneRect geometry;
    i32 clip_x;
    i32 clip_y;
    u32 clip_width;
    u32 clip_height;
} GuiSceneGeometryRequest;

typedef struct GuiSceneVisibilityRequest {
    GuiRequestPrefix prefix;
    u64 window_id;
    GuiSceneVisibility visibility;
} GuiSceneVisibilityRequest;

typedef struct GuiSceneStackRequest {
    GuiRequestPrefix prefix;
    u64 window_id;
    GuiSceneLayer layer;
    u64 above_window_id;
} GuiSceneStackRequest;

typedef struct GuiSceneOutputRequest {
    GuiRequestPrefix prefix;
    u64 window_id;
    u64 display_id;
} GuiSceneOutputRequest;

typedef struct GuiSceneWindowRequest {
    GuiRequestPrefix prefix;
    u64 window_id;
} GuiSceneWindowRequest;

typedef struct GuiSceneBindingLostEvent {
    u64 serial;
    u64 window_id;
    u64 surface_id;
    GuiSceneBindingLostReason reason;
} GuiSceneBindingLostEvent;

typedef struct GuiDisplayPresentRequest {
    GuiRequestPrefix prefix;
    u64 display_id;
    u64 generation;
    u32 width;
    u32 height;
    u32 stride;
    GuiPixelFormat pixel_format;
    u64 byte_size;
    u32 flags;
    u16 damage_count;
    GuiRect damage[GUI_MAX_DAMAGE_RECTS];
} GuiDisplayPresentRequest;

typedef struct GuiDisplayMode {
    u32 width_px;
    u32 height_px;
    u32 refresh_millihz;
} GuiDisplayMode;

typedef struct GuiDisplayInfo {
    u64 display_id;
    u64 generation;
    GuiDisplayFlags flags;
    u32 physical_width_mm;
    u32 physical_height_mm;
    GuiDisplayMode current_mode;
    u16 name_length;
    u16 mode_count;
    u8 name[GUI_DISPLAY_NAME_MAX];
    GuiDisplayMode modes[GUI_DISPLAY_MODE_MAX];
} GuiDisplayInfo;

typedef struct GuiBasicResult {
    ErrorCode error;
    u64 request_id;
} GuiBasicResult;

typedef struct GuiSurfaceCreateResult {
    GuiBasicResult result;
    u64 surface_id;
    u8 surface_bind_token[GUI_SURFACE_BIND_TOKEN_SIZE];
} GuiSurfaceCreateResult;

typedef struct GuiBufferImportResult {
    GuiBasicResult result;
    u64 buffer_id;
} GuiBufferImportResult;

typedef struct GuiSurfaceFrameDoneEvent {
    u64 request_id;
    u64 surface_id;
    u64 commit_serial;
    u64 presented_at_ms;
} GuiSurfaceFrameDoneEvent;

typedef struct GuiBufferReleasedEvent {
    u64 serial;
    u64 buffer_id;
} GuiBufferReleasedEvent;

typedef struct GuiSurfaceOutputEvent {
    u64 serial;
    u64 surface_id;
    u64 display_id;
} GuiSurfaceOutputEvent;

typedef struct GuiSurfaceRedrawEvent {
    u64 serial;
    u64 surface_id;
    u32 reason;
} GuiSurfaceRedrawEvent;

typedef struct GuiDisplayListHeader {
    GuiBasicResult result;
    u64 snapshot_serial;
    u16 display_count;
} GuiDisplayListHeader;

typedef u8 GuiDisplayOrientation;
#define GUI_ORIENTATION_NORMAL ((GuiDisplayOrientation)0)
#define GUI_ORIENTATION_RIGHT ((GuiDisplayOrientation)1)
#define GUI_ORIENTATION_INVERTED ((GuiDisplayOrientation)2)
#define GUI_ORIENTATION_LEFT ((GuiDisplayOrientation)3)

typedef struct GuiPresenterBindRequest {
    GuiRequestPrefix prefix;
    u8 presenter_token[GUI_PRESENTER_TOKEN_SIZE];
} GuiPresenterBindRequest;

typedef struct GuiDisplayConfigureRequest {
    GuiRequestPrefix prefix;
    u64 display_id;
    u64 generation;
    bool enabled;
    GuiDisplayOrientation orientation;
    GuiDisplayMode mode;
} GuiDisplayConfigureRequest;

typedef struct GuiDisplayPresentedEvent {
    u64 request_id;
    u64 display_id;
    u64 generation;
    u64 presented_at_ms;
} GuiDisplayPresentedEvent;

typedef struct GuiDisplayPresentFailedEvent {
    u64 request_id;
    u64 display_id;
    u64 generation;
    ErrorCode error;
} GuiDisplayPresentFailedEvent;

typedef struct GuiDisplayBackendLostEvent {
    u64 serial;
    ErrorCode error;
} GuiDisplayBackendLostEvent;

typedef struct GuiDisplayRemovedEvent {
    u64 serial;
    u64 display_id;
    u64 last_generation;
} GuiDisplayRemovedEvent;

typedef struct GuiIoSubscribeRequest {
    u16 protocol_version;
    GuiInputDeviceClass device_class_mask;
} GuiIoSubscribeRequest;

typedef struct GuiIoResult {
    ErrorCode error;
} GuiIoResult;

typedef struct GuiIoDeviceEvent {
    u64 device_id;
    u16 bus;
    u16 vendor;
    u16 product;
    u16 version;
    GuiInputDeviceClass classes;
} GuiIoDeviceEvent;

typedef struct GuiIoInputEvent {
    GuiIoInputEventKind kind;
    u16 code;
    i32 value;
} GuiIoInputEvent;

typedef struct GuiIoInputFrameEvent {
    u64 device_id;
    u64 sequence;
    u16 event_count;
    GuiIoInputEvent events[GUI_IO_INPUT_FRAME_MAX_EVENTS];
} GuiIoInputFrameEvent;

typedef u8 GuiWindowRole;
#define GUI_WINDOW_ROLE_APP ((GuiWindowRole)1)
#define GUI_WINDOW_ROLE_DIALOG ((GuiWindowRole)2)
#define GUI_WINDOW_ROLE_TRANSIENT ((GuiWindowRole)3)
#define GUI_WINDOW_ROLE_HOME ((GuiWindowRole)4)
#define GUI_WINDOW_ROLE_SYSTEM_BAR ((GuiWindowRole)5)
#define GUI_WINDOW_ROLE_SYSTEM_OVERLAY ((GuiWindowRole)6)
#define GUI_WINDOW_ROLE_LOCK ((GuiWindowRole)7)
#define GUI_WINDOW_ROLE_IME ((GuiWindowRole)8)

typedef u8 GuiWindowLifecycle;
#define GUI_WINDOW_LIFECYCLE_HIDDEN ((GuiWindowLifecycle)1)
#define GUI_WINDOW_LIFECYCLE_BACKGROUND ((GuiWindowLifecycle)2)
#define GUI_WINDOW_LIFECYCLE_FOREGROUND ((GuiWindowLifecycle)3)
#define GUI_WINDOW_LIFECYCLE_SUSPENDED ((GuiWindowLifecycle)4)
#define GUI_WINDOW_LIFECYCLE_CLOSING ((GuiWindowLifecycle)5)

typedef u32 GuiWindowFlags;
#define GUI_WINDOW_FLAG_EDGE_TO_EDGE ((GuiWindowFlags)0x1)
#define GUI_WINDOW_FLAG_ACCEPTS_KEYBOARD ((GuiWindowFlags)0x2)
#define GUI_WINDOW_FLAG_HANDLES_BACK ((GuiWindowFlags)0x4)

typedef u32 GuiModifierMask;
#define GUI_MODIFIER_SHIFT ((GuiModifierMask)1)
#define GUI_MODIFIER_CONTROL ((GuiModifierMask)2)
#define GUI_MODIFIER_ALT ((GuiModifierMask)4)
#define GUI_MODIFIER_SUPER ((GuiModifierMask)8)
#define GUI_MODIFIER_CAPS_LOCK ((GuiModifierMask)16)
#define GUI_MODIFIER_NUM_LOCK ((GuiModifierMask)32)

typedef u8 GuiTouchPhase;
#define GUI_TOUCH_DOWN ((GuiTouchPhase)1)
#define GUI_TOUCH_MOVE ((GuiTouchPhase)2)
#define GUI_TOUCH_UP ((GuiTouchPhase)3)
#define GUI_TOUCH_CANCEL ((GuiTouchPhase)4)

typedef u8 GuiPointerPhase;
#define GUI_POINTER_MOVE ((GuiPointerPhase)1)
#define GUI_POINTER_DOWN ((GuiPointerPhase)2)
#define GUI_POINTER_UP ((GuiPointerPhase)3)
#define GUI_POINTER_CANCEL ((GuiPointerPhase)4)

typedef u8 GuiKeyPhase;
#define GUI_KEY_DOWN ((GuiKeyPhase)1)
#define GUI_KEY_UP ((GuiKeyPhase)2)
#define GUI_KEY_REPEAT ((GuiKeyPhase)3)

typedef u8 GuiWindowLifecycleReason;
#define GUI_WINDOW_REASON_SHELL_ACTIVATION ((GuiWindowLifecycleReason)1)
#define GUI_WINDOW_REASON_HOME ((GuiWindowLifecycleReason)2)
#define GUI_WINDOW_REASON_LOCK ((GuiWindowLifecycleReason)3)
#define GUI_WINDOW_REASON_DISPLAY_LOSS ((GuiWindowLifecycleReason)4)
#define GUI_WINDOW_REASON_RECOVERY ((GuiWindowLifecycleReason)5)
#define GUI_WINDOW_REASON_CLIENT_REQUEST ((GuiWindowLifecycleReason)6)

typedef u8 GuiNavigationAction;
#define GUI_NAVIGATION_BACK ((GuiNavigationAction)1)
#define GUI_NAVIGATION_HOME ((GuiNavigationAction)2)
#define GUI_NAVIGATION_OVERVIEW ((GuiNavigationAction)3)

typedef u8 GuiCloseResponse;
#define GUI_CLOSE_ALLOW ((GuiCloseResponse)1)
#define GUI_CLOSE_DENY ((GuiCloseResponse)2)
#define GUI_CLOSE_DEFER ((GuiCloseResponse)3)

typedef struct GuiWindowCreateRequest {
    GuiRequestPrefix prefix;
    u64 surface_id;
    u8 surface_bind_token[GUI_SURFACE_BIND_TOKEN_SIZE];
    GuiWindowRole role;
    u64 parent_id;
    GuiWindowFlags flags;
    u16 title_length;
    u8 title[GUI_WINDOW_TITLE_MAX];
} GuiWindowCreateRequest;

typedef struct GuiWindowCreateResult {
    GuiBasicResult result;
    u64 window_id;
} GuiWindowCreateResult;

typedef struct GuiWindowIdRequest {
    GuiRequestPrefix prefix;
    u64 window_id;
} GuiWindowIdRequest;

typedef struct GuiWindowSetTitleRequest {
    GuiRequestPrefix prefix;
    u64 window_id;
    u16 title_length;
    u8 title[GUI_WINDOW_TITLE_MAX];
} GuiWindowSetTitleRequest;

typedef struct GuiWindowAckConfigureRequest {
    GuiRequestPrefix prefix;
    u64 window_id;
    u64 configure_serial;
} GuiWindowAckConfigureRequest;

typedef struct GuiWindowModalRequest {
    GuiRequestPrefix prefix;
    u64 window_id;
    u64 parent_id;
} GuiWindowModalRequest;

typedef struct GuiWindowSurfaceRebindRequest {
    GuiRequestPrefix prefix;
    u64 window_id;
    u64 surface_id;
    u8 surface_bind_token[GUI_SURFACE_BIND_TOKEN_SIZE];
} GuiWindowSurfaceRebindRequest;

typedef struct GuiShellBindRequest {
    GuiRequestPrefix prefix;
    u8 shell_token[GUI_SHELL_TOKEN_SIZE];
} GuiShellBindRequest;

typedef struct GuiShellActivateRequest {
    GuiRequestPrefix prefix;
    u64 window_id;
} GuiShellActivateRequest;

typedef struct GuiShellInsetsRequest {
    GuiRequestPrefix prefix;
    u64 display_id;
    GuiInsets insets;
} GuiShellInsetsRequest;

typedef struct GuiShellNavigateRequest {
    GuiRequestPrefix prefix;
    GuiNavigationAction action;
    u64 target_id;
} GuiShellNavigateRequest;

typedef struct GuiWindowCloseResponseRequest {
    GuiRequestPrefix prefix;
    u64 window_id;
    u64 close_serial;
    GuiCloseResponse response;
} GuiWindowCloseResponseRequest;

typedef struct GuiWindowConfigureEvent {
    u64 window_id;
    u64 configure_serial;
    u32 width;
    u32 height;
    u32 scale;
    GuiDisplayOrientation orientation;
    GuiInsets safe_insets;
} GuiWindowConfigureEvent;

typedef struct GuiWindowLifecycleEvent {
    u64 serial;
    u64 window_id;
    GuiWindowLifecycle lifecycle;
    GuiWindowLifecycleReason reason;
} GuiWindowLifecycleEvent;

typedef struct GuiWindowCloseEvent {
    u64 close_serial;
    u64 window_id;
} GuiWindowCloseEvent;

typedef struct GuiWindowVisibilityEvent {
    u64 serial;
    u64 window_id;
    bool visible;
} GuiWindowVisibilityEvent;

typedef struct GuiWindowPointerEvent {
    u64 window_id;
    u64 input_serial;
    u64 timestamp_ms;
    u32 pointer_id;
    GuiPointerPhase phase;
    u32 buttons;
    GuiModifierMask modifiers;
    i32 x;
    i32 y;
} GuiWindowPointerEvent;

typedef struct GuiWindowKeyEvent {
    u64 window_id;
    u64 input_serial;
    u64 timestamp_ms;
    u32 key_code;
    GuiKeyPhase phase;
    GuiModifierMask modifiers;
} GuiWindowKeyEvent;

typedef struct GuiWindowTouchEvent {
    u64 window_id;
    u64 input_serial;
    u64 timestamp_ms;
    u32 touch_id;
    GuiTouchPhase phase;
    i32 x;
    i32 y;
} GuiWindowTouchEvent;

typedef struct GuiWindowServiceEvent {
    u64 serial;
    u64 window_id;
    GuiWindowLifecycleReason reason;
} GuiWindowServiceEvent;

typedef struct GuiWindowNavigationEvent {
    u64 serial;
    u64 window_id;
    GuiNavigationAction action;
} GuiWindowNavigationEvent;

ErrorCode gui_encode_buffer_import(const GuiBufferImportRequest *request,
                                   Bytes destination, usize *out_size);
ErrorCode gui_decode_buffer_import(const IpcMessage *message,
                                   GuiBufferImportRequest *out_request);

ErrorCode gui_encode_surface_id_request(const GuiSurfaceIdRequest *request,
                                        Bytes destination, usize *out_size);
ErrorCode gui_decode_surface_id_request(const IpcMessage *message,
                                        IpcMessageType expected_type,
                                        GuiSurfaceIdRequest *out_request);

ErrorCode gui_encode_buffer_id_request(const GuiBufferIdRequest *request,
                                       Bytes destination, usize *out_size);
ErrorCode gui_decode_buffer_id_request(const IpcMessage *message,
                                       IpcMessageType expected_type,
                                       GuiBufferIdRequest *out_request);

ErrorCode gui_encode_surface_attach(const GuiSurfaceAttachRequest *request,
                                    Bytes destination, usize *out_size);
ErrorCode gui_decode_surface_attach(const IpcMessage *message,
                                    GuiSurfaceAttachRequest *out_request);

ErrorCode gui_encode_surface_damage(const GuiSurfaceDamageRequest *request,
                                    Bytes destination, usize *out_size);
ErrorCode gui_decode_surface_damage(const IpcMessage *message,
                                    GuiSurfaceDamageRequest *out_request);

ErrorCode gui_encode_surface_region(const GuiSurfaceRegionRequest *request,
                                    Bytes destination, usize *out_size);
ErrorCode gui_decode_surface_region(const IpcMessage *message,
                                    GuiSurfaceRegionRequest *out_request);

ErrorCode gui_encode_surface_frame_request(
    const GuiSurfaceFrameRequest *request, Bytes destination,
    usize *out_size);
ErrorCode gui_decode_surface_frame_request(
    const IpcMessage *message, GuiSurfaceFrameRequest *out_request);

ErrorCode gui_encode_surface_commit(const GuiSurfaceCommitRequest *request,
                                    Bytes destination, usize *out_size);
ErrorCode gui_decode_surface_commit(const IpcMessage *message,
                                    GuiSurfaceCommitRequest *out_request);

ErrorCode gui_encode_surface_create_result(
    const GuiSurfaceCreateResult *result, Bytes destination,
    usize *out_size);
ErrorCode gui_decode_surface_create_result(
    const IpcMessage *message, GuiSurfaceCreateResult *out_result);

ErrorCode gui_encode_buffer_import_result(
    const GuiBufferImportResult *result, Bytes destination,
    usize *out_size);
ErrorCode gui_decode_buffer_import_result(
    const IpcMessage *message, GuiBufferImportResult *out_result);

ErrorCode gui_encode_surface_frame_done(
    const GuiSurfaceFrameDoneEvent *event, Bytes destination,
    usize *out_size);
ErrorCode gui_decode_surface_frame_done(
    const IpcMessage *message, GuiSurfaceFrameDoneEvent *out_event);

ErrorCode gui_encode_buffer_released(
    const GuiBufferReleasedEvent *event, Bytes destination,
    usize *out_size);
ErrorCode gui_decode_buffer_released(
    const IpcMessage *message, GuiBufferReleasedEvent *out_event);

ErrorCode gui_encode_surface_output_event(
    const GuiSurfaceOutputEvent *event, Bytes destination,
    usize *out_size);
ErrorCode gui_decode_surface_output_event(
    const IpcMessage *message, IpcMessageType expected_type,
    GuiSurfaceOutputEvent *out_event);

ErrorCode gui_encode_surface_redraw(
    const GuiSurfaceRedrawEvent *event, Bytes destination,
    usize *out_size);
ErrorCode gui_decode_surface_redraw(
    const IpcMessage *message, GuiSurfaceRedrawEvent *out_event);

ErrorCode gui_encode_controller_bind(
    const GuiControllerBindRequest *request, Bytes destination,
    usize *out_size);
ErrorCode gui_decode_controller_bind(
    const IpcMessage *message, GuiControllerBindRequest *out_request);

ErrorCode gui_encode_scene_bind(
    const GuiSceneBindRequest *request, Bytes destination, usize *out_size);
ErrorCode gui_decode_scene_bind(
    const IpcMessage *message, GuiSceneBindRequest *out_request);

ErrorCode gui_encode_scene_geometry(
    const GuiSceneGeometryRequest *request, Bytes destination,
    usize *out_size);
ErrorCode gui_decode_scene_geometry(
    const IpcMessage *message, GuiSceneGeometryRequest *out_request);

ErrorCode gui_encode_scene_visibility(
    const GuiSceneVisibilityRequest *request, Bytes destination,
    usize *out_size);
ErrorCode gui_decode_scene_visibility(
    const IpcMessage *message, GuiSceneVisibilityRequest *out_request);

ErrorCode gui_encode_scene_stack(
    const GuiSceneStackRequest *request, Bytes destination,
    usize *out_size);
ErrorCode gui_decode_scene_stack(
    const IpcMessage *message, GuiSceneStackRequest *out_request);

ErrorCode gui_encode_scene_output(
    const GuiSceneOutputRequest *request, Bytes destination,
    usize *out_size);
ErrorCode gui_decode_scene_output(
    const IpcMessage *message, GuiSceneOutputRequest *out_request);

ErrorCode gui_encode_scene_window(
    const GuiSceneWindowRequest *request, Bytes destination,
    usize *out_size);
ErrorCode gui_decode_scene_window(
    const IpcMessage *message, GuiSceneWindowRequest *out_request);

ErrorCode gui_encode_scene_binding_lost(
    const GuiSceneBindingLostEvent *event, Bytes destination,
    usize *out_size);
ErrorCode gui_decode_scene_binding_lost(
    const IpcMessage *message, GuiSceneBindingLostEvent *out_event);

ErrorCode gui_encode_display_present(const GuiDisplayPresentRequest *request,
                                     Bytes destination, usize *out_size);
ErrorCode gui_decode_display_present(const IpcMessage *message,
                                     GuiDisplayPresentRequest *out_request);
ErrorCode gui_encode_presenter_bind(const GuiPresenterBindRequest *request,
                                    Bytes destination, usize *out_size);
ErrorCode gui_decode_presenter_bind(const IpcMessage *message,
                                    GuiPresenterBindRequest *out_request);
ErrorCode gui_encode_display_configure(
    const GuiDisplayConfigureRequest *request, Bytes destination,
    usize *out_size);
ErrorCode gui_decode_display_configure(
    const IpcMessage *message, GuiDisplayConfigureRequest *out_request);
ErrorCode gui_encode_basic_result(const GuiBasicResult *result,
                                  Bytes destination, usize *out_size);
ErrorCode gui_decode_basic_result(const IpcMessage *message,
                                  IpcMessageType expected_type,
                                  GuiBasicResult *out_result);
ErrorCode gui_encode_display_presented(
    const GuiDisplayPresentedEvent *event, Bytes destination,
    usize *out_size);
ErrorCode gui_decode_display_presented(
    const IpcMessage *message, GuiDisplayPresentedEvent *out_event);
ErrorCode gui_encode_display_present_failed(
    const GuiDisplayPresentFailedEvent *event, Bytes destination,
    usize *out_size);
ErrorCode gui_decode_display_present_failed(
    const IpcMessage *message, GuiDisplayPresentFailedEvent *out_event);
ErrorCode gui_encode_display_backend_lost(
    const GuiDisplayBackendLostEvent *event, Bytes destination,
    usize *out_size);
ErrorCode gui_decode_display_backend_lost(
    const IpcMessage *message, GuiDisplayBackendLostEvent *out_event);
ErrorCode gui_encode_versioned_request(GuiRequestPrefix prefix,
                                       Bytes destination, usize *out_size);
ErrorCode gui_decode_versioned_request(const IpcMessage *message,
                                       IpcMessageType expected_type,
                                       GuiRequestPrefix *out_prefix);
ErrorCode gui_encode_display_id_request(GuiRequestPrefix prefix,
                                        u64 display_id, Bytes destination,
                                        usize *out_size);
ErrorCode gui_decode_display_id_request(const IpcMessage *message,
                                        IpcMessageType expected_type,
                                        GuiRequestPrefix *out_prefix,
                                        u64 *out_display_id);
ErrorCode gui_encode_display_info(const GuiDisplayInfo *info,
                                  Bytes destination, usize *out_size);
ErrorCode gui_decode_display_info(Bytes source, GuiDisplayInfo *out_info,
                                  usize *out_consumed);
ErrorCode gui_decode_display_list_header(const IpcMessage *message,
                                         GuiDisplayListHeader *out_header,
                                         usize *out_first_display);
ErrorCode gui_encode_display_list_header(const GuiDisplayListHeader *header,
                                         Bytes destination, usize *out_size);
ErrorCode gui_encode_display_info_result(const GuiBasicResult *result,
                                         const GuiDisplayInfo *info,
                                         Bytes destination, usize *out_size);
ErrorCode gui_decode_display_info_result(const IpcMessage *message,
                                         IpcMessageType expected_type,
                                         GuiBasicResult *out_result,
                                         GuiDisplayInfo *out_info);
ErrorCode gui_encode_display_info_event(u64 serial,
                                        const GuiDisplayInfo *info,
                                        Bytes destination, usize *out_size);
ErrorCode gui_decode_display_info_event(const IpcMessage *message,
                                        IpcMessageType expected_type,
                                        u64 *out_serial,
                                        GuiDisplayInfo *out_info);
ErrorCode gui_encode_display_removed(const GuiDisplayRemovedEvent *event,
                                     Bytes destination, usize *out_size);
ErrorCode gui_decode_display_removed(const IpcMessage *message,
                                     GuiDisplayRemovedEvent *out_event);

ErrorCode gui_encode_io_subscribe(
    const GuiIoSubscribeRequest *request, Bytes destination, usize *out_size);
ErrorCode gui_decode_io_subscribe(
    const IpcMessage *message, GuiIoSubscribeRequest *out_request);
ErrorCode gui_encode_io_result(const GuiIoResult *result,
                               Bytes destination, usize *out_size);
ErrorCode gui_decode_io_result(const IpcMessage *message,
                               IpcMessageType expected_type,
                               GuiIoResult *out_result);
ErrorCode gui_encode_io_device_event(
    const GuiIoDeviceEvent *event, Bytes destination, usize *out_size);
ErrorCode gui_decode_io_device_event(
    const IpcMessage *message, IpcMessageType expected_type,
    GuiIoDeviceEvent *out_event);
ErrorCode gui_encode_io_input_frame(
    const GuiIoInputFrameEvent *event, Bytes destination, usize *out_size);
ErrorCode gui_decode_io_input_frame(
    const IpcMessage *message, GuiIoInputFrameEvent *out_event);

ErrorCode gui_encode_window_create(
    const GuiWindowCreateRequest *request, Bytes destination,
    usize *out_size);
ErrorCode gui_decode_window_create(
    const IpcMessage *message, GuiWindowCreateRequest *out_request);
ErrorCode gui_encode_window_create_result(
    const GuiWindowCreateResult *result, Bytes destination,
    usize *out_size);
ErrorCode gui_decode_window_create_result(
    const IpcMessage *message, GuiWindowCreateResult *out_result);
ErrorCode gui_encode_window_id_request(
    const GuiWindowIdRequest *request, Bytes destination, usize *out_size);
ErrorCode gui_decode_window_id_request(
    const IpcMessage *message, IpcMessageType expected_type,
    GuiWindowIdRequest *out_request);
ErrorCode gui_encode_window_set_title(
    const GuiWindowSetTitleRequest *request, Bytes destination,
    usize *out_size);
ErrorCode gui_decode_window_set_title(
    const IpcMessage *message, GuiWindowSetTitleRequest *out_request);
ErrorCode gui_encode_window_ack_configure(
    const GuiWindowAckConfigureRequest *request, Bytes destination,
    usize *out_size);
ErrorCode gui_decode_window_ack_configure(
    const IpcMessage *message, GuiWindowAckConfigureRequest *out_request);
ErrorCode gui_encode_window_modal(
    const GuiWindowModalRequest *request, Bytes destination, usize *out_size);
ErrorCode gui_decode_window_modal(
    const IpcMessage *message, GuiWindowModalRequest *out_request);
ErrorCode gui_encode_window_surface_rebind(
    const GuiWindowSurfaceRebindRequest *request, Bytes destination,
    usize *out_size);
ErrorCode gui_decode_window_surface_rebind(
    const IpcMessage *message, GuiWindowSurfaceRebindRequest *out_request);
ErrorCode gui_encode_shell_bind(
    const GuiShellBindRequest *request, Bytes destination, usize *out_size);
ErrorCode gui_decode_shell_bind(
    const IpcMessage *message, GuiShellBindRequest *out_request);
ErrorCode gui_encode_shell_activate(
    const GuiShellActivateRequest *request, Bytes destination,
    usize *out_size);
ErrorCode gui_decode_shell_activate(
    const IpcMessage *message, GuiShellActivateRequest *out_request);
ErrorCode gui_encode_shell_insets(
    const GuiShellInsetsRequest *request, Bytes destination, usize *out_size);
ErrorCode gui_decode_shell_insets(
    const IpcMessage *message, GuiShellInsetsRequest *out_request);
ErrorCode gui_encode_shell_navigate(
    const GuiShellNavigateRequest *request, Bytes destination,
    usize *out_size);
ErrorCode gui_decode_shell_navigate(
    const IpcMessage *message, GuiShellNavigateRequest *out_request);
ErrorCode gui_encode_window_close_response(
    const GuiWindowCloseResponseRequest *request, Bytes destination,
    usize *out_size);
ErrorCode gui_decode_window_close_response(
    const IpcMessage *message, GuiWindowCloseResponseRequest *out_request);
ErrorCode gui_encode_window_configure(
    const GuiWindowConfigureEvent *event, Bytes destination, usize *out_size);
ErrorCode gui_decode_window_configure(
    const IpcMessage *message, GuiWindowConfigureEvent *out_event);
ErrorCode gui_encode_window_lifecycle(
    const GuiWindowLifecycleEvent *event, Bytes destination, usize *out_size);
ErrorCode gui_decode_window_lifecycle(
    const IpcMessage *message, GuiWindowLifecycleEvent *out_event);
ErrorCode gui_encode_window_close_event(
    const GuiWindowCloseEvent *event, Bytes destination, usize *out_size);
ErrorCode gui_decode_window_close_event(
    const IpcMessage *message, GuiWindowCloseEvent *out_event);
ErrorCode gui_encode_window_visibility(
    const GuiWindowVisibilityEvent *event, Bytes destination, usize *out_size);
ErrorCode gui_decode_window_visibility(
    const IpcMessage *message, GuiWindowVisibilityEvent *out_event);
ErrorCode gui_encode_window_pointer(
    const GuiWindowPointerEvent *event, Bytes destination, usize *out_size);
ErrorCode gui_decode_window_pointer(
    const IpcMessage *message, GuiWindowPointerEvent *out_event);
ErrorCode gui_encode_window_key(
    const GuiWindowKeyEvent *event, Bytes destination, usize *out_size);
ErrorCode gui_decode_window_key(
    const IpcMessage *message, GuiWindowKeyEvent *out_event);
ErrorCode gui_encode_window_touch(
    const GuiWindowTouchEvent *event, Bytes destination, usize *out_size);
ErrorCode gui_decode_window_touch(
    const IpcMessage *message, GuiWindowTouchEvent *out_event);
ErrorCode gui_encode_window_service_event(
    const GuiWindowServiceEvent *event, Bytes destination, usize *out_size);
ErrorCode gui_decode_window_service_event(
    const IpcMessage *message, GuiWindowServiceEvent *out_event);
ErrorCode gui_encode_window_navigation(
    const GuiWindowNavigationEvent *event, Bytes destination, usize *out_size);
ErrorCode gui_decode_window_navigation(
    const IpcMessage *message, GuiWindowNavigationEvent *out_event);

#endif
