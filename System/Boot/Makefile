CC ?= cc

NATIVE_DIR := ../NativeRuntime
CRT_DIR := $(NATIVE_DIR)/CRT
SYSCALL_DIR := $(NATIVE_DIR)/SyscallWrappers
CRT0 := $(CRT_DIR)/build/crt0.o
CRT_LIBRARY := $(CRT_DIR)/build/libpunos_crt.a
SYSCALL_LIBRARY := $(SYSCALL_DIR)/build/libpunos_syscall.a
SYSCALL_SELFTEST := $(SYSCALL_DIR)/build/full_wrappers_runtime
SYSTEM_LIBS_DIR := ../SystemLibs
SYSTEM_SERVICES_DIR := ../SystemServices
DISPLAYSERV := $(SYSTEM_SERVICES_DIR)/displayserv/build/displayserv
DISPLAYSERV_SELFTEST := $(SYSTEM_SERVICES_DIR)/displayserv/build/displayserv_runtime
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

CPPFLAGS := -I$(NATIVE_DIR)/include -I$(SYSCALL_DIR)/include
CFLAGS := -std=c11 -ffreestanding -nostdinc -fno-builtin \
          -fno-stack-protector -fno-pic -m64 -Wall -Wextra -Werror
BUILD_DIR := build
INIT_OBJECT := $(BUILD_DIR)/init.o
STUB_OBJECT := $(BUILD_DIR)/initserv_stub.o
INIT := $(BUILD_DIR)/init
STUB := $(BUILD_DIR)/initserv

.PHONY: all clean native-runtime system-services $(SYSTEM_LIBS_SELFTESTS)

all: $(INIT) $(STUB) $(SYSCALL_SELFTEST) $(SYSTEM_LIBS_SELFTESTS) \
     $(DISPLAYSERV) $(DISPLAYSERV_SELFTEST)

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

system-services:
	$(MAKE) -C $(SYSTEM_SERVICES_DIR) all runtime-test

$(DISPLAYSERV) $(DISPLAYSERV_SELFTEST): system-services

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
