#include <uilib/ui.h>

ErrorCode (*const abi_ui_initialize)(AppHandle, const UiConfig *,
                                     UiContextHandle *) = ui_initialize;
ErrorCode (*const abi_ui_view_create)(UiContextHandle, const UiViewConfig *,
                                      UiViewHandle *) = ui_view_create;
ErrorCode (*const abi_ui_view_destroy)(UiViewHandle *) = ui_view_destroy;
ErrorCode (*const abi_ui_view_acquire_buffer)(UiViewHandle,
                                              UiMappedBuffer *) = ui_view_acquire_buffer;
ErrorCode (*const abi_ui_view_damage)(UiViewHandle, const GuiRect *,
                                      usize) = ui_view_damage;
ErrorCode (*const abi_ui_view_present)(UiViewHandle,
                                       UiBufferHandle) = ui_view_present;
ErrorCode (*const abi_ui_view_publish)(UiViewHandle) = ui_view_publish;
ErrorCode (*const abi_ui_view_hide)(UiViewHandle) = ui_view_hide;
ErrorCode (*const abi_ui_view_set_title)(UiViewHandle,
                                         const char *) = ui_view_set_title;
ErrorCode (*const abi_ui_view_respond_close)(UiViewHandle, u64,
                                             GuiCloseResponse) = ui_view_respond_close;
ErrorCode (*const abi_ui_get_latest_configure)(UiViewHandle,
    GuiWindowConfigureEvent *) = ui_get_latest_configure;
ErrorCode (*const abi_ui_shutdown)(UiContextHandle *) = ui_shutdown;

_Static_assert(UI_BUFFER_COUNT_DEFAULT == 2, "default buffer count changed");
_Static_assert(UI_BUFFER_COUNT_MAX == 4, "maximum buffer count changed");
