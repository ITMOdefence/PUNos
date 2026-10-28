CC ?= cc

NATIVE_DIR := ../NativeRuntime
CRT_DIR := $(NATIVE_DIR)/CRT
SYSCALL_DIR := $(NATIVE_DIR)/SyscallWrappers
CRT0 := $(CRT_DIR)/build/crt0.o
CRT_LIBRARY := $(CRT_DIR)/build/libpunos_crt.a
SYSCALL_LIBRARY := $(SYSCALL_DIR)/build/libpunos_syscall.a
SYSCALL_SELFTEST := $(SYSCALL_DIR)/build/full_wrappers_runtime
SYSTEM_LIBS_DIR := ../SystemLibs
# Runtime tests of System Libs, packaged as /punos/bin/<library>-selftest.
SYSTEM_LIBS_SELFTESTS := $(SYSTEM_LIBS_DIR)/libcore/build/core_runtime \
                         $(SYSTEM_LIBS_DIR)/libmath/build/math_runtime \
                         $(SYSTEM_LIBS_DIR)/libmem/build/memory_runtime \
                         $(SYSTEM_LIBS_DIR)/librandom/build/random_runtime \
                         $(SYSTEM_LIBS_DIR)/libgraphics/build/graphics_runtime \
                         $(SYSTEM_LIBS_DIR)/libstr/build/string_runtime \
                         $(SYSTEM_LIBS_DIR)/libfile/build/file_runtime \
                         $(SYSTEM_LIBS_DIR)/libtime/build/time_runtime \
                         $(SYSTEM_LIBS_DIR)/libprocess/build/process_runtime \
                         $(SYSTEM_LIBS_DIR)/libthread/build/thread_runtime \
                         $(SYSTEM_LIBS_DIR)/libsync/build/sync_runtime \
                         $(SYSTEM_LIBS_DIR)/libeventloop/build/eventloop_runtime \
                         $(SYSTEM_LIBS_DIR)/libinputdevice/build/input_device_runtime \
                         $(SYSTEM_LIBS_DIR)/libv4l2/build/v4l2_runtime
SPECIAL_LIBS_DIR := ../SpecialLibs
APPLICATION_LAYER_DIR := ../ApplicationLayer
SYSTEM_SERVICES_DIR := ../SystemServices
REGISTRY ?= $(abspath $(SYSTEM_SERVICES_DIR)/platform_registry.json)
# Runtime tests of Special Libs, packaged as /punos/bin/<name>-selftest.
SPECIAL_LIBS_SELFTESTS := $(SPECIAL_LIBS_DIR)/ipclib/build/ipc_runtime \
                         $(SPECIAL_LIBS_DIR)/loglib/build/log_runtime \
                         $(SPECIAL_LIBS_DIR)/inputlib/build/input_runtime \
                         $(SPECIAL_LIBS_DIR)/camlib/build/camera_runtime \
                         $(SPECIAL_LIBS_DIR)/displaylib/build/display_runtime

# Runtime tests of appframework, packaged as /punos/bin/appfw-<name>-selftest.
APPFRAMEWORK_SELFTESTS := $(APPLICATION_LAYER_DIR)/appframework/build/lifecycle_runtime \
                          $(APPLICATION_LAYER_DIR)/appframework/build/create_runtime \
                          $(APPLICATION_LAYER_DIR)/appframework/build/run_runtime \
                          $(APPLICATION_LAYER_DIR)/appframework/build/fault_runtime \
                          $(APPLICATION_LAYER_DIR)/appframework/build/logserv_runtime \
                          $(APPLICATION_LAYER_DIR)/appframework/build/apps_wire_runtime \
                          $(APPLICATION_LAYER_DIR)/appframework/build/apps_runtime

CPPFLAGS := -I$(NATIVE_DIR)/include -I$(SYSCALL_DIR)/include
CFLAGS := -std=c11 -ffreestanding -nostdinc -fno-builtin \
          -fno-stack-protector -fno-pic -m64 -Wall -Wextra -Werror
BUILD_DIR := build
INIT_OBJECT := $(BUILD_DIR)/init.o
STUB_OBJECT := $(BUILD_DIR)/initserv_stub.o
INIT := $(BUILD_DIR)/init
STUB := $(BUILD_DIR)/initserv-smoke

.PHONY: all clean native-runtime system-services $(SYSTEM_LIBS_SELFTESTS) $(SPECIAL_LIBS_SELFTESTS) $(APPFRAMEWORK_SELFTESTS)

all: $(INIT) $(BUILD_DIR)/init-smoke $(STUB) $(BUILD_DIR)/services-bootcheck $(SYSCALL_SELFTEST) $(SYSTEM_LIBS_SELFTESTS) $(SPECIAL_LIBS_SELFTESTS) $(APPFRAMEWORK_SELFTESTS) system-services

system-services:
	$(MAKE) -C $(SYSTEM_SERVICES_DIR) REGISTRY=$(REGISTRY) all runtime-test

$(BUILD_DIR):
	mkdir -p $@

native-runtime:
	$(MAKE) -C $(CRT_DIR) all
	$(MAKE) -C $(SYSCALL_DIR) all

$(CRT0) $(CRT_LIBRARY) $(SYSCALL_LIBRARY): native-runtime

$(SYSCALL_SELFTEST):
	$(MAKE) -C $(SYSCALL_DIR) full-test

$(SYSTEM_LIBS_SELFTESTS):
	$(MAKE) -C $(dir $(@D)) runtime-test

$(SPECIAL_LIBS_SELFTESTS):
	$(MAKE) -C $(dir $(@D)) runtime-test

$(APPFRAMEWORK_SELFTESTS):
	$(MAKE) -C $(dir $(@D)) runtime-test

$(INIT_OBJECT): Init/init.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(STUB_OBJECT): InitservStub/initserv_stub.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(INIT): $(INIT_OBJECT) $(CRT0) $(CRT_LIBRARY) $(SYSCALL_LIBRARY)
	$(CC) -nostdlib -no-pie -Wl,-e,_start \
		$(CRT0) $(INIT_OBJECT) $(CRT_LIBRARY) $(SYSCALL_LIBRARY) -o $@

$(STUB): $(STUB_OBJECT) $(CRT0) $(CRT_LIBRARY) $(SYSCALL_LIBRARY)
	$(CC) -nostdlib -no-pie -Wl,-e,_start \
		$(CRT0) $(STUB_OBJECT) $(CRT_LIBRARY) $(SYSCALL_LIBRARY) -o $@

clean:
	rm -rf $(BUILD_DIR)

$(BUILD_DIR)/init_smoke.o: Init/init.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DPUNOS_SMOKE_BOOT -c $< -o $@
$(BUILD_DIR)/init-smoke: $(BUILD_DIR)/init_smoke.o $(CRT0) $(CRT_LIBRARY) $(SYSCALL_LIBRARY)
	$(CC) -nostdlib -no-pie -Wl,-e,_start $(CRT0) $(BUILD_DIR)/init_smoke.o $(CRT_LIBRARY) $(SYSCALL_LIBRARY) -o $@
$(BUILD_DIR)/services-bootcheck: ServicesProbe/probe.c system-services
	$(CC) $(CPPFLAGS) $(CFLAGS) $(foreach lib,core mem str time thread eventloop,-I../SystemLibs/lib$(lib)/include) -I../SpecialLibs/ipclib/include -c $< -o $(BUILD_DIR)/services_probe.o
	$(CC) -nostdlib -no-pie -Wl,-e,_start $(CRT0) $(BUILD_DIR)/services_probe.o -Wl,--start-group $(foreach lib,core mem str time thread eventloop,../SystemLibs/lib$(lib)/build/libpunos_$(lib).a) ../SpecialLibs/ipclib/build/libpunos_special_ipc.a $(CRT_LIBRARY) $(SYSCALL_LIBRARY) -Wl,--end-group -o $@
