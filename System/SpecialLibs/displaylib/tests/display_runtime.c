#define CHECK_TEST_NAME "displaylib"
#include "../../../SystemLibs/testing/check.h"

#include <special/display.h>
#include <sys/memory.h>

static void test_software_display(void)
{
    String backend_name;
    DisplayBackendConfig config;
    DisplayBackendHandle backend;
    DisplayOutputInfoList outputs;
    SharedMemory shared;
    DisplayFrameDescriptor descriptor = {
        1080, 1920, 4320, DISPLAY_PIXEL_XRGB8888, 8294400
    };
    DisplayFrameHandle frame;
    DisplayDamage damage = { NULL, 0, true };
    DisplayEventStatus status;
    DisplayBackendEvent event;
    i32 frame_fd;
    i32 event_fd;

    EXPECT(1, string_from_c_str("software", &backend_name) == CORE_OK);
    config.backend_name = &backend_name;
    config.device_path = NULL;
    EXPECT(2, display_backend_open(&config, &backend) == CORE_OK);
    EXPECT(3, display_backend_get_fd(backend, &event_fd) == CORE_OK &&
              event_fd >= 0);
    EXPECT(4, display_backend_list_outputs(backend, &outputs) == CORE_OK &&
              outputs.count == 1 && outputs.items[0].connected &&
              outputs.items[0].current_mode.width_px == 1080);
    EXPECT(5, memory_shared_create(descriptor.byte_size, &shared) == CORE_OK);
    shared.data[0] = 0x44;
    frame_fd = fcntl(shared.fd, F_DUPFD_CLOEXEC, 0);
    EXPECT(6, frame_fd >= 0);
    EXPECT(7, display_frame_import(backend, &descriptor, frame_fd, &frame) ==
              CORE_OK);
    EXPECT(8, display_output_present(backend,
        outputs.items[0].backend_output_id, frame, &damage, 77) == CORE_OK);
    EXPECT(9, display_frame_destroy(&frame) == CORE_INVALID_STATE);
    EXPECT(10, display_backend_next_event(backend, &status, &event) == CORE_OK &&
               status == DISPLAY_EVENT_READY &&
               event.type == DISPLAY_BACKEND_EVENT_PRESENT_COMPLETE &&
               event.present_id == 77);
    EXPECT(11, display_frame_destroy(&frame) == CORE_OK);
    EXPECT(12, memory_shared_close(&shared) == CORE_OK);
    EXPECT(13, display_output_info_list_destroy(&outputs) == CORE_OK);
    EXPECT(14, display_backend_close(&backend) == CORE_OK);
    EXPECT(15, string_destroy(&backend_name) == CORE_OK);
}

void main(void)
{
    test_software_display();
    check_passed();
}
