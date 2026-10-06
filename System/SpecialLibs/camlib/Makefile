LIBRARY_NAME := camera
RUNTIME_TEST := camera_runtime
DEPENDENCIES := libcore libmem libstr libtime libeventloop libv4l2
TEST_FAKES := v4l2_fake memory_fake
TEST_FAKE_LDFLAGS := -Wl,--wrap=memory_allocate -Wl,--wrap=memory_free

include ../special_libs.mk
