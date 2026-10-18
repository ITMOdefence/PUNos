LIBRARY_NAME := appframework
RUNTIME_TESTS := lifecycle_runtime create_runtime run_runtime fault_runtime
DEPENDENCIES := libcore libmem libstr libtime libeventloop
SPECIAL_DEPENDENCIES := ipclib
# Linked into the tests only: threads for the owner-thread checks.
TEST_DEPENDENCIES := libthread libsync
# Headers only: LogSeverity and limits of loglib, ProcessTerminationKind and
# the thread-independent types the ABI checks compare against.
HEADER_DEPENDENCIES := libprocess libfile loglib

TEST_FAKES := fault_fake
TEST_LDFLAGS := -Wl,--wrap=memory_allocate -Wl,--wrap=memory_reallocate -Wl,--wrap=memory_free \
    -Wl,--wrap=eventloop_wait -Wl,--wrap=eventloop_wake -Wl,--wrap=sigaction -Wl,--allow-multiple-definition

include ../application_layer.mk
