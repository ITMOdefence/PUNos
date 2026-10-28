LIBRARY_NAME := appframework
RUNTIME_TESTS := lifecycle_runtime create_runtime run_runtime fault_runtime logserv_runtime apps_wire_runtime apps_runtime messaging_runtime
DEPENDENCIES := libcore libmem libstr libtime libeventloop
SPECIAL_DEPENDENCIES := ipclib
# Linked into the tests only: threads for the owner-thread checks.
TEST_DEPENDENCIES := libthread libsync
# Headers only: LogSeverity and limits of loglib, ProcessTerminationKind and
# the thread-independent types the ABI checks compare against.
HEADER_DEPENDENCIES := libprocess libfile loglib

TEST_FAKES := fault_fake
TEST_LDFLAGS := -Wl,--wrap=memory_allocate -Wl,--wrap=memory_reallocate -Wl,--wrap=memory_free \
    -Wl,--wrap=eventloop_wait -Wl,--wrap=eventloop_wake -Wl,--wrap=ipc_connection_finish_connect -Wl,--wrap=ipc_connection_send -Wl,--wrap=ipc_connection_flush -Wl,--wrap=sigaction -Wl,--allow-multiple-definition

include ../application_layer.mk

# Level 3: appframework against the real logserv and appserv on the host. The
# services are built from System/SystemServices by the script itself; their
# libraries (and loglib/libfile/libprocess/libsync) have to exist first.
SYSTEM_TEST_LIBS := libfile libprocess libsync
.PHONY: check-system
check-system: $(LIBRARY)
	@set -e; for lib in $(SYSTEM_TEST_LIBS) $(DEPENDENCIES) $(TEST_DEPENDENCIES); do \
	    $(MAKE) -C $(SYSTEM_LIBS_DIR)/$$lib all; done
	$(MAKE) -C $(SPECIAL_LIBS_DIR)/loglib all
	python3 tests/system/host_integration.py --punos $(abspath $(SYSTEM_DIR)/..) \
	    --library $(abspath $(LIBRARY))
