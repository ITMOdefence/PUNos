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

typedef u32 GuiDisplayFlags;
#define GUI_DISPLAY_CONNECTED ((GuiDisplayFlags)1)
#define GUI_DISPLAY_ENABLED ((GuiDisplayFlags)2)
#define GUI_DISPLAY_PRIMARY ((GuiDisplayFlags)4)

typedef u32 GuiPixelFormat;
#define GUI_PIXEL_XRGB8888 ((GuiPixelFormat)1)
#define GUI_PIXEL_ARGB8888 ((GuiPixelFormat)2)

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

#endif
