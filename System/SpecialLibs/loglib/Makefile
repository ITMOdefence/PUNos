LIBRARY_NAME := log
RUNTIME_TEST := log_runtime
DEPENDENCIES := libcore libmem libstr libfile libtime libprocess libthread
TEST_FAKES := file_fake
TEST_FAKE_LDFLAGS := -Wl,--wrap=file_open -Wl,--wrap=file_close

include ../special_libs.mk
