CC ?= cc
AR ?= ar

ROOT := ../../..
APPFRAMEWORK := ../appframework
CORE := $(ROOT)/System/SystemLibs/libcore
MEM := $(ROOT)/System/SystemLibs/libmem
STR := $(ROOT)/System/SystemLibs/libstr
EVENT := $(ROOT)/System/SystemLibs/libeventloop
TIME := $(ROOT)/System/SystemLibs/libtime
IPC := $(ROOT)/System/SpecialLibs/ipclib
NATIVE := $(ROOT)/System/NativeRuntime
PROTOCOL := $(ROOT)/GUI/Protocols
CRT := $(NATIVE)/CRT
SYSCALL := $(NATIVE)/SyscallWrappers
CPPFLAGS := -Iinclude -I$(APPFRAMEWORK)/include -I$(PROTOCOL)/include -I$(CORE)/include \
            -I$(MEM)/include -I$(STR)/include -I$(EVENT)/include -I$(TIME)/include \
            -I$(IPC)/include -I$(NATIVE)/SyscallWrappers/include \
            -I$(NATIVE)/include
CFLAGS := -std=c11 -ffreestanding -nostdinc -fno-builtin \
          -fno-stack-protector -fno-pic -m64 -Wall -Wextra -Werror \
          -fno-tree-loop-distribute-patterns
BUILD := build
LIBRARY := $(BUILD)/libuilib.a

.PHONY: all check clean runtime-test dependencies
all: $(LIBRARY)
check: $(LIBRARY) $(BUILD)/abi_compile.o $(BUILD)/hello_ui.o $(BUILD)/uilib_runtime
	$(BUILD)/uilib_runtime
	@echo "uilib ABI compile checks passed."
runtime-test: $(BUILD)/uilib_runtime

$(BUILD):
	mkdir -p $@
$(BUILD)/ui.o: src/ui.c include/uilib/ui.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/abi_compile.o: tests/abi_compile.c include/uilib/ui.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/hello_ui.o: examples/hello_ui.c include/uilib/ui.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/runtime.o: tests/runtime.c tests/fake_runtime.h include/uilib/ui.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/fake_runtime.o: tests/fake_runtime.c tests/fake_runtime.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
dependencies:
	$(MAKE) -C $(PROTOCOL) all
	$(MAKE) -C $(CORE) all
	$(MAKE) -C $(MEM) all
	$(MAKE) -C $(STR) all
	$(MAKE) -C $(CRT) all
	$(MAKE) -C $(SYSCALL) all
$(BUILD)/uilib_runtime: $(BUILD)/runtime.o $(BUILD)/fake_runtime.o $(LIBRARY) dependencies
	$(CC) -nostdlib -no-pie -Wl,-e,_start $(CRT)/build/crt0.o \
		$(BUILD)/runtime.o $(BUILD)/fake_runtime.o -Wl,--start-group \
		$(LIBRARY) $(PROTOCOL)/build/libpunos_gui_protocol.a \
		$(MEM)/build/libpunos_mem.a $(STR)/build/libpunos_str.a \
		$(CORE)/build/libpunos_core.a $(CRT)/build/libpunos_crt.a \
		$(SYSCALL)/build/libpunos_syscall.a -Wl,--end-group -o $@
$(LIBRARY): $(BUILD)/ui.o
	rm -f $@
	$(AR) rcs $@ $^
clean:
	rm -rf $(BUILD)
