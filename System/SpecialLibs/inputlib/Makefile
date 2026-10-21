LIBRARY_NAME := input
RUNTIME_TEST := input_runtime
DEPENDENCIES := libcore libmem libstr libtime libeventloop libinputdevice
TEST_FAKES := input_device_fake allocation_fake
TEST_FAKE_LDFLAGS := -Wl,--wrap=memory_allocate

include ../special_libs.mk
