#define CHECK_TEST_NAME "libgraphics"
#include "../../testing/check.h"

#include <sys/graphics.h>
#include <sys/memory.h>

static void test_software_backend(void)
{
    GraphicsDeviceConfig config = { { 4, 3, 60000 }, true };
    GraphicsDeviceHandle device;
    GraphicsOutputInfo output;
    SharedMemory shared;
    GraphicsFrameDescriptor descriptor = {
        4, 3, 16, GRAPHICS_PIXEL_XRGB8888, 48
    };
    GraphicsFrameHandle frame;
    GraphicsDamage damage = { NULL, 0, true };
    GraphicsEventStatus status;
    GraphicsEvent event;
    i32 transferred_fd;
    i32 event_fd;
    usize index;

    EXPECT(1, graphics_device_open(&config, &device) == CORE_OK);
    EXPECT(2, graphics_device_get_fd(device, &event_fd) == CORE_OK &&
              event_fd >= 0);
    EXPECT(3, graphics_device_get_output(device, &output) == CORE_OK &&
              output.output_id == GRAPHICS_SOFTWARE_OUTPUT_ID &&
              output.connected && output.enabled &&
              output.current_mode.height_px == 3);
    EXPECT(4, memory_shared_create(48, &shared) == CORE_OK);
    for (index = 0; index < shared.size; ++index) {
        shared.data[index] = (u8)index;
    }
    transferred_fd = fcntl(shared.fd, F_DUPFD_CLOEXEC, 0);
    EXPECT(5, transferred_fd >= 0);
    EXPECT(6, graphics_frame_import(device, &descriptor, transferred_fd,
                                     &frame) == CORE_OK);
    EXPECT(7, graphics_output_present(device, output.output_id, frame,
                                       &damage, 99) == CORE_OK);
    EXPECT(8, graphics_frame_destroy(&frame) == CORE_INVALID_STATE);
    EXPECT(9, graphics_device_next_event(device, &status, &event) == CORE_OK &&
              status == GRAPHICS_EVENT_READY &&
              event.type == GRAPHICS_EVENT_PRESENT_COMPLETE &&
              event.present_id == 99 && event.error == CORE_OK);
    EXPECT(10, graphics_frame_destroy(&frame) == CORE_OK && frame == NULL);
    EXPECT(11, graphics_device_next_event(device, &status, &event) == CORE_OK &&
               status == GRAPHICS_EVENT_NONE);
    EXPECT(12, memory_shared_close(&shared) == CORE_OK);
    EXPECT(13, graphics_device_close(&device) == CORE_OK && device == NULL);
}

void main(void)
{
    test_software_backend();
    check_passed();
}
