#include <special/display.h>

_Static_assert(sizeof(DisplayMode) == 12, "DisplayMode ABI");
_Static_assert(sizeof(DisplayFrameDescriptor) == 24, "frame descriptor ABI");
_Static_assert(sizeof(DisplayRect) == 16, "DisplayRect ABI");
_Static_assert(sizeof(DisplayBackendEvent) == 24, "backend event ABI");
_Static_assert(sizeof(DisplayBackendHandle) == sizeof(void *), "backend handle");
_Static_assert(sizeof(DisplayFrameHandle) == sizeof(void *), "frame handle");
_Static_assert(DISPLAY_DAMAGE_MAX_RECTS == 64, "damage limit");
_Static_assert(DISPLAY_PRESENT_QUEUE_DEPTH == 2, "queue depth");

int display_abi_compile_test(void)
{
    return 0;
}
