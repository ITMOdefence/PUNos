#include <sys/graphics.h>

_Static_assert(sizeof(GraphicsMode) == 12, "GraphicsMode ABI");
_Static_assert(sizeof(GraphicsFrameDescriptor) == 24, "frame descriptor ABI");
_Static_assert(sizeof(GraphicsRect) == 16, "GraphicsRect ABI");
_Static_assert(sizeof(GraphicsEvent) == 24, "GraphicsEvent ABI");
_Static_assert(sizeof(GraphicsDeviceHandle) == sizeof(void *), "device handle");
_Static_assert(sizeof(GraphicsFrameHandle) == sizeof(void *), "frame handle");
_Static_assert(GRAPHICS_PRESENT_QUEUE_DEPTH == 2, "queue depth");

int graphics_abi_compile_test(void)
{
    return 0;
}
