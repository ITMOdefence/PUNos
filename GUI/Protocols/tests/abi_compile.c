#include <gui/protocol.h>

_Static_assert(GUI_PROTOCOL_VERSION == 1, "protocol version");
_Static_assert(GUI_MAX_DAMAGE_RECTS == 64, "damage limit");
_Static_assert(sizeof(GuiRequestPrefix) == 16, "native prefix ABI");
_Static_assert(sizeof(GuiRect) == 16, "rect ABI");
_Static_assert(GUI_SURFACE_BUFFER_IMPORT == 5, "buffer.import type");
_Static_assert(GUI_SURFACE_DAMAGE == 10, "surface.damage type");
_Static_assert(GUI_DISPLAY_PRESENT == 7, "display.present type");
_Static_assert(sizeof(GuiDisplayMode) == 12, "display mode ABI");
_Static_assert(sizeof(GuiDisplayPresentedEvent) == 32, "presented event ABI");
_Static_assert(GUI_PRESENTER_TOKEN_SIZE == 32, "presenter token size");
_Static_assert(GUI_DISPLAY_NAME_MAX == 128, "display name limit");
_Static_assert(GUI_DISPLAY_MODE_MAX == 128, "display mode limit");
_Static_assert(GUI_DISPLAY_LIST_MAX == 32, "display list limit");

int gui_protocol_abi_compile_test(void)
{
    return 0;
}
