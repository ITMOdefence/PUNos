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
CPPFLAGS := -Iinclude -I$(APPFRAMEWORK)/include -I$(CORE)/include \
            -I$(MEM)/include -I$(STR)/include -I$(EVENT)/include -I$(TIME)/include \
            -I$(IPC)/include -I$(NATIVE)/SyscallWrappers/include \
            -I$(NATIVE)/include
CFLAGS := -std=c11 -ffreestanding -nostdinc -fno-builtin \
          -fno-stack-protector -fno-pic -m64 -Wall -Wextra -Werror \
          -fno-tree-loop-distribute-patterns
BUILD := build
LIBRARY := $(BUILD)/libuilib.a

.PHONY: all check clean
all: $(LIBRARY)
check: $(LIBRARY) $(BUILD)/abi_compile.o
	@echo "uilib ABI compile checks passed."

$(BUILD):
	mkdir -p $@
$(BUILD)/ui.o: src/ui.c include/uilib/ui.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/abi_compile.o: tests/abi_compile.c include/uilib/ui.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(LIBRARY): $(BUILD)/ui.o
	rm -f $@
	$(AR) rcs $@ $^
clean:
	rm -rf $(BUILD)
