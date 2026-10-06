#ifndef PUNOS_GUI_PROTOCOL_H
#define PUNOS_GUI_PROTOCOL_H

#include <special/ipc.h>

#define GUI_PROTOCOL_VERSION ((u16)1)
#define GUI_MAX_DAMAGE_RECTS ((u16)64)
#define GUI_FULL_DAMAGE ((u32)1)

#define GUI_SURFACE_BUFFER_IMPORT ((IpcMessageType)5)
#define GUI_SURFACE_DAMAGE ((IpcMessageType)10)
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
ErrorCode gui_encode_surface_damage(const GuiSurfaceDamageRequest *request,
                                    Bytes destination, usize *out_size);
ErrorCode gui_decode_surface_damage(const IpcMessage *message,
                                    GuiSurfaceDamageRequest *out_request);
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
