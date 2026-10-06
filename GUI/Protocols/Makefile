CC ?= cc
AR ?= ar

ROOT := ../..
CORE := $(ROOT)/System/SystemLibs/libcore
IPC := $(ROOT)/System/SpecialLibs/ipclib
STR := $(ROOT)/System/SystemLibs/libstr
NATIVE := $(ROOT)/System/NativeRuntime
CPPFLAGS := -Iinclude -I$(CORE)/include -I$(IPC)/include -I$(STR)/include \
            -I$(NATIVE)/SyscallWrappers/include -I$(NATIVE)/include
CFLAGS := -std=c11 -ffreestanding -nostdinc -fno-builtin \
          -fno-stack-protector -fno-pic -m64 -Wall -Wextra -Werror
BUILD := build

.PHONY: all compile-check clean
all: $(BUILD)/libpunos_gui_protocol.a
compile-check: $(BUILD)/protocol.o $(BUILD)/abi_compile.o $(BUILD)/protocol_runtime.o

$(BUILD):
	mkdir -p $@
$(BUILD)/protocol.o: src/protocol.c include/gui/protocol.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/abi_compile.o: tests/abi_compile.c include/gui/protocol.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/protocol_runtime.o: tests/protocol_runtime.c include/gui/protocol.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/libpunos_gui_protocol.a: $(BUILD)/protocol.o
	rm -f $@
	$(AR) rcs $@ $^
clean:
	rm -rf $(BUILD)
