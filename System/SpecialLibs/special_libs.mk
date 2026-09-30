# Common build rules for one Special Lib. Based on SystemLibs/library.mk
# A library's Makefile sets:
#
#   LIBRARY_NAME   archive name: build/libpunos_special_$(LIBRARY_NAME).a
#   RUNTIME_TEST   base name of tests/<name>.c
#   DEPENDENCIES   System Libs it uses, in dependency order
#
# Example:
#   LIBRARY_NAME := ipc
#   RUNTIME_TEST := ipc_runtime
#   DEPENDENCIES := libcore libmem libstr libeventloop

CC ?= cc
AR ?= ar

# Directory holding this file: System/SpecialLibs. Special Libs live one level
# below it, so every other directory is addressed relative to SPECIAL_LIBS_DIR
# and stays correct no matter which library's Makefile included this file.
SPECIAL_LIBS_DIR := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
SYSTEM_LIBS_DIR := $(SPECIAL_LIBS_DIR)/../SystemLibs
NATIVE_DIR := $(SPECIAL_LIBS_DIR)/../NativeRuntime
CRT_DIR := $(NATIVE_DIR)/CRT
SYSCALL_DIR := $(NATIVE_DIR)/SyscallWrappers
CRT0 := $(CRT_DIR)/build/crt0.o
CRT_LIBRARY := $(CRT_DIR)/build/libpunos_crt.a
SYSCALL_LIBRARY := $(SYSCALL_DIR)/build/libpunos_syscall.a
TESTING_DIR := $(SYSTEM_LIBS_DIR)/testing

# $(call reverse,a b c) -> c b a
reverse = $(if $(1),$(call reverse,$(wordlist 2,$(words $(1)),$(1))) $(firstword $(1)))

DEPENDENCY_ARCHIVES := $(foreach dependency,$(DEPENDENCIES),\
    $(SYSTEM_LIBS_DIR)/$(dependency)/build/libpunos_$(patsubst lib%,%,$(dependency)).a)

CPPFLAGS := -Iinclude $(foreach dependency,$(DEPENDENCIES),-I$(SYSTEM_LIBS_DIR)/$(dependency)/include) \
            -I$(SYSCALL_DIR)/include -I$(NATIVE_DIR)/include -I$(TESTING_DIR)
# -fno-tree-loop-distribute-patterns forbids GCC to turn byte loops into calls
# to memcpy()/memset(), which do not exist in PUNos.
CFLAGS := -std=c11 -ffreestanding -nostdinc -fno-builtin \
          -fno-stack-protector -fno-pic -m64 -Wall -Wextra -Werror \
          -fno-tree-loop-distribute-patterns

BUILD_DIR := build
LIBRARY := $(BUILD_DIR)/libpunos_special_$(LIBRARY_NAME).a
OBJECTS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(wildcard src/*.c))
ABI_TEST_OBJECT := $(BUILD_DIR)/abi_compile.o
RUNTIME_TEST_OBJECT := $(BUILD_DIR)/$(RUNTIME_TEST).o
RUNTIME_TEST_PROGRAM := $(BUILD_DIR)/$(RUNTIME_TEST)
HEADERS := $(wildcard include/special/*.h src/*.h) \
           $(foreach dependency,$(DEPENDENCIES),$(wildcard $(SYSTEM_LIBS_DIR)/$(dependency)/include/sys/*.h))

.PHONY: all check clean runtime-test dependencies

all: $(LIBRARY)

runtime-test: $(RUNTIME_TEST_PROGRAM)

check: $(LIBRARY) $(ABI_TEST_OBJECT) $(RUNTIME_TEST_PROGRAM)
	$(RUNTIME_TEST_PROGRAM)
	@echo "lib$(LIBRARY_NAME) ABI compile checks passed."

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/%.o: src/%.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIBRARY): $(OBJECTS)
	rm -f $@
	$(AR) rcs $@ $^

$(ABI_TEST_OBJECT): tests/abi_compile.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(RUNTIME_TEST_OBJECT): tests/$(RUNTIME_TEST).c $(TESTING_DIR)/check.h $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

dependencies:
	@set -e; for dependency in $(DEPENDENCIES); do $(MAKE) -C $(SYSTEM_LIBS_DIR)/$$dependency all; done
	$(MAKE) -C $(CRT_DIR) all
	$(MAKE) -C $(SYSCALL_DIR) all

$(DEPENDENCY_ARCHIVES) $(CRT0) $(CRT_LIBRARY) $(SYSCALL_LIBRARY): dependencies

$(RUNTIME_TEST_PROGRAM): $(RUNTIME_TEST_OBJECT) $(LIBRARY) $(DEPENDENCY_ARCHIVES) \
                         $(CRT0) $(CRT_LIBRARY) $(SYSCALL_LIBRARY)
	$(CC) -nostdlib -no-pie -Wl,-e,_start \
		$(CRT0) $(RUNTIME_TEST_OBJECT) $(LIBRARY) \
		$(call reverse,$(DEPENDENCY_ARCHIVES)) \
		$(CRT_LIBRARY) $(SYSCALL_LIBRARY) -o $@

clean:
	rm -rf $(BUILD_DIR)
