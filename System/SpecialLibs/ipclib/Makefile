LIBRARY_NAME := ipc
RUNTIME_TEST := ipc_runtime
DEPENDENCIES := libcore libmem libstr libeventloop
TEST_FAKES := socket_fake
# tests/socket_fake.c replaces getsockopt(); see special_libs.mk for why that
# needs the flag.
TEST_FAKE_LDFLAGS := -Wl,--allow-multiple-definition -Wl,--wrap=send -Wl,--wrap=listen

include ../special_libs.mk
