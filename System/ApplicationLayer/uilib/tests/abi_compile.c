#include <uilib/ui.h>

ErrorCode (*const abi_ui_initialize)(AppHandle, const UiConfig *,
                                     UiContextHandle *) = ui_initialize;
ErrorCode (*const abi_ui_view_create)(UiContextHandle, const UiViewConfig *,
                                      UiViewHandle *) = ui_view_create;
ErrorCode (*const abi_ui_view_destroy)(UiViewHandle *) = ui_view_destroy;
ErrorCode (*const abi_ui_shutdown)(UiContextHandle *) = ui_shutdown;

_Static_assert(UI_BUFFER_COUNT_DEFAULT == 2, "default buffer count changed");
_Static_assert(UI_BUFFER_COUNT_MAX == 4, "maximum buffer count changed");
