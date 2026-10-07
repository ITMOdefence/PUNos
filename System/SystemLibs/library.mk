# Common build rules for one System Lib. A library's Makefile sets:
#
#   LIBRARY_NAME   archive name: build/libpunos_$(LIBRARY_NAME).a
#   RUNTIME_TEST   base name of tests/<name>.c, the runtime test / QEMU self-test
#   DEPENDENCIES   System Libs it uses, in dependency order (e.g. libcore libmem)
#
# Optional:
#   TEST_FAKES     whitespace-separated base names of tests/<name>_fake.c files
#                  to compile and link into the runtime test. A fake provides
#                  its own definition of the library's dependencies, or of one
#                  syscall wrapper, so paths that need real hardware can be
#                  exercised on a machine that has none. The objects are linked
#                  before the dependency archives.
#   TEST_FAKE_LDFLAGS
#                  extra flags for the runtime test link; see
#                  System/SpecialLibs/special_libs.mk for when a fake that
#                  replaces a syscall wrapper needs
#                  -Wl,--allow-multiple-definition.
#
# and then includes this file. Every .c file in src/ goes into the archive.
# See System/SystemLibs/README.md for the conventions behind these rules.

CC ?= cc
AR ?= ar

NATIVE_DIR := ../../NativeRuntime
CRT_DIR := $(NATIVE_DIR)/CRT
SYSCALL_DIR := $(NATIVE_DIR)/SyscallWrappers
CRT0 := $(CRT_DIR)/build/crt0.o
CRT_LIBRARY := $(CRT_DIR)/build/libpunos_crt.a
SYSCALL_LIBRARY := $(SYSCALL_DIR)/build/libpunos_syscall.a

# $(call reverse,a b c) -> c b a
reverse = $(if $(1),$(call reverse,$(wordlist 2,$(words $(1)),$(1))) $(firstword $(1)))

DEPENDENCY_ARCHIVES := $(foreach dependency,$(DEPENDENCIES),\
    ../$(dependency)/build/libpunos_$(patsubst lib%,%,$(dependency)).a)

CPPFLAGS := -Iinclude $(foreach dependency,$(DEPENDENCIES),-I../$(dependency)/include) \
            -I$(SYSCALL_DIR)/include -I$(NATIVE_DIR)/include
# -fno-tree-loop-distribute-patterns forbids GCC to turn byte loops into calls
# to memcpy()/memset(), which do not exist in PUNos. GCC 15 does not do it for
# the current code even at -O3; the flag keeps it that way.
CFLAGS := -std=c11 -ffreestanding -nostdinc -fno-builtin \
          -fno-stack-protector -fno-pic -m64 -Wall -Wextra -Werror \
          -fno-tree-loop-distribute-patterns

BUILD_DIR := build
LIBRARY := $(BUILD_DIR)/libpunos_$(LIBRARY_NAME).a
OBJECTS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(wildcard src/*.c))
ABI_TEST_OBJECT := $(BUILD_DIR)/abi_compile.o
RUNTIME_TEST_OBJECT := $(BUILD_DIR)/$(RUNTIME_TEST).o
RUNTIME_TEST_PROGRAM := $(BUILD_DIR)/$(RUNTIME_TEST)
HEADERS := $(wildcard include/sys/*.h src/*.h) \
           $(foreach dependency,$(DEPENDENCIES),$(wildcard ../$(dependency)/include/sys/*.h))
FAKE_SOURCES := $(foreach fake,$(TEST_FAKES),tests/$(fake).c)
FAKE_OBJECTS := $(patsubst tests/%.c,$(BUILD_DIR)/%.o,$(FAKE_SOURCES))
# A comma inside $(if ...) would end the argument list, so it comes from a
# variable: $(if) would otherwise see "-Wl" and "--allow-multiple-definition".
comma := ,
FAKE_LDFLAGS := $(if $(FAKE_OBJECTS),-Wl$(comma)--allow-multiple-definition)

.PHONY: all check clean runtime-test dependencies

all: $(LIBRARY)

# Test program packaged into the initramfs as /punos/bin/<library>-selftest.
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

$(RUNTIME_TEST_OBJECT): tests/$(RUNTIME_TEST).c ../testing/check.h $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%_fake.o: tests/%_fake.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

dependencies:
	@set -e; for dependency in $(DEPENDENCIES); do $(MAKE) -C ../$$dependency all; done
	$(MAKE) -C $(CRT_DIR) all
	$(MAKE) -C $(SYSCALL_DIR) all

$(DEPENDENCY_ARCHIVES) $(CRT0) $(CRT_LIBRARY) $(SYSCALL_LIBRARY): dependencies

# Dependants before their dependencies: this library, then DEPENDENCIES reversed.
$(RUNTIME_TEST_PROGRAM): $(RUNTIME_TEST_OBJECT) $(LIBRARY) $(DEPENDENCY_ARCHIVES) \
                         $(FAKE_OBJECTS) $(CRT0) $(CRT_LIBRARY) $(SYSCALL_LIBRARY)
	$(CC) $(TEST_FAKE_LDFLAGS) $(FAKE_LDFLAGS) -nostdlib -no-pie -Wl,-e,_start \
		$(CRT0) $(RUNTIME_TEST_OBJECT) $(LIBRARY) $(FAKE_OBJECTS) \
		$(call reverse,$(DEPENDENCY_ARCHIVES)) \
		$(CRT_LIBRARY) $(SYSCALL_LIBRARY) -o $@

clean:
	rm -rf $(BUILD_DIR)
