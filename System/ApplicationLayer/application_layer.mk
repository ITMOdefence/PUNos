# Common build rules for one Application Layer library. Based on
# System/SpecialLibs/special_libs.mk. A library's Makefile sets:
#
#   LIBRARY_NAME   archive name: build/lib$(LIBRARY_NAME).a
#   RUNTIME_TESTS  base names of tests/<name>.c; each one is a program with
#                  main() that is also usable as a QEMU self-test
#   DEPENDENCIES   System Libs it links, e.g. libcore libmem
#   SPECIAL_DEPENDENCIES
#                  Special Libs it links, e.g. ipclib
#   HEADER_DEPENDENCIES
#                  System Libs and Special Libs whose headers (and nothing
#                  else) the library or its tests use
#
# Optional:
#   TEST_DEPENDENCIES
#                  System Libs linked into the runtime tests only (the archive
#                  itself does not use them), e.g. libthread
#   TEST_FAKES     base names of tests/<name>_fake.c, linked into every
#                  runtime test before the archives; a fake replaces one
#                  dependency function or one syscall wrapper
#   TEST_LDFLAGS   extra link flags for the runtime tests (for example
#                  --wrap=<symbol>); fakes need --allow-multiple-definition
#                  because libpunos_syscall.a is a single object

CC ?= cc
AR ?= ar

APP_LAYER_DIR := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
SYSTEM_DIR := $(APP_LAYER_DIR)/..
SYSTEM_LIBS_DIR := $(SYSTEM_DIR)/SystemLibs
SPECIAL_LIBS_DIR := $(SYSTEM_DIR)/SpecialLibs
NATIVE_DIR := $(SYSTEM_DIR)/NativeRuntime
CRT_DIR := $(NATIVE_DIR)/CRT
SYSCALL_DIR := $(NATIVE_DIR)/SyscallWrappers
CRT0 := $(CRT_DIR)/build/crt0.o
CRT_LIBRARY := $(CRT_DIR)/build/libpunos_crt.a
SYSCALL_LIBRARY := $(SYSCALL_DIR)/build/libpunos_syscall.a
TESTING_DIR := $(SYSTEM_LIBS_DIR)/testing

ALL_HEADER_LIBS := $(DEPENDENCIES) $(TEST_DEPENDENCIES) $(HEADER_DEPENDENCIES)
SYSTEM_INCLUDES := $(foreach lib,$(filter lib%,$(ALL_HEADER_LIBS)),-I$(SYSTEM_LIBS_DIR)/$(lib)/include)
SPECIAL_INCLUDES := $(foreach lib,$(SPECIAL_DEPENDENCIES) $(filter-out lib%,$(HEADER_DEPENDENCIES)),-I$(SPECIAL_LIBS_DIR)/$(lib)/include)

SYSTEM_ARCHIVES := $(foreach lib,$(DEPENDENCIES),\
    $(SYSTEM_LIBS_DIR)/$(lib)/build/libpunos_$(patsubst lib%,%,$(lib)).a)
SPECIAL_ARCHIVES := $(foreach lib,$(SPECIAL_DEPENDENCIES),\
    $(SPECIAL_LIBS_DIR)/$(lib)/build/libpunos_special_$(patsubst %lib,%,$(lib)).a)
DEPENDENCY_ARCHIVES := $(SPECIAL_ARCHIVES) $(SYSTEM_ARCHIVES)
TEST_ARCHIVES := $(foreach lib,$(TEST_DEPENDENCIES),\
    $(SYSTEM_LIBS_DIR)/$(lib)/build/libpunos_$(patsubst lib%,%,$(lib)).a)

CPPFLAGS := -Iinclude -Isrc $(SYSTEM_INCLUDES) $(SPECIAL_INCLUDES) \
            -I$(SYSCALL_DIR)/include -I$(NATIVE_DIR)/include -I$(TESTING_DIR)
# -fno-tree-loop-distribute-patterns forbids GCC to turn byte loops into calls
# to memcpy()/memset(), which do not exist in PUNos.
CFLAGS := -std=c11 -ffreestanding -nostdinc -fno-builtin \
          -fno-stack-protector -fno-pic -m64 -Wall -Wextra -Werror \
          -fno-tree-loop-distribute-patterns

BUILD_DIR := build
LIBRARY := $(BUILD_DIR)/lib$(LIBRARY_NAME).a
OBJECTS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(wildcard src/*.c))
ABI_TEST_OBJECT := $(BUILD_DIR)/abi_compile.o
TEST_PROGRAMS := $(addprefix $(BUILD_DIR)/,$(RUNTIME_TESTS))
FAKE_OBJECTS := $(addprefix $(BUILD_DIR)/,$(addsuffix .o,$(TEST_FAKES)))
HEADERS := $(wildcard include/appframework/*.h src/*.h tests/*.h) \
           $(wildcard $(TESTING_DIR)/*.h) \
           $(foreach lib,$(filter lib%,$(ALL_HEADER_LIBS)),$(wildcard $(SYSTEM_LIBS_DIR)/$(lib)/include/sys/*.h)) \
           $(foreach lib,$(SPECIAL_DEPENDENCIES) $(filter-out lib%,$(HEADER_DEPENDENCIES)),$(wildcard $(SPECIAL_LIBS_DIR)/$(lib)/include/special/*.h))
ALLOWED_UNDEFINED := $(wildcard allowed_undefined.txt)

.PHONY: all check clean runtime-test dependencies check-symbols

all: $(LIBRARY)

# Test programs packaged into the initramfs as /punos/bin/<name>-selftest.
runtime-test: $(TEST_PROGRAMS)

check: $(LIBRARY) $(ABI_TEST_OBJECT) $(TEST_PROGRAMS) check-symbols
	@set -e; for test in $(TEST_PROGRAMS); do echo "$$test"; $$test; done
	@echo "$(LIBRARY_NAME) ABI compile checks passed."

# The archive may reference only the functions its contract allows. Symbols
# defined by another member of the same archive do not count.
check-symbols: $(LIBRARY)
	@nm --defined-only $(LIBRARY) | awk 'NF == 3 { print $$3 }' | sort -u > $(BUILD_DIR)/defined.txt
	@nm --undefined-only $(LIBRARY) | awk '$$1 == "U" { print $$2 }' | sort -u \
	    | grep -v -x -f $(BUILD_DIR)/defined.txt > $(BUILD_DIR)/external.txt || true
	@if [ -n "$(ALLOWED_UNDEFINED)" ]; then \
	    grep -v -x -f $(ALLOWED_UNDEFINED) $(BUILD_DIR)/external.txt > $(BUILD_DIR)/unexpected.txt || true; \
	    if [ -s $(BUILD_DIR)/unexpected.txt ]; then \
	        echo "$(LIBRARY_NAME): undefined symbols outside the contract:" >&2; \
	        cat $(BUILD_DIR)/unexpected.txt >&2; exit 1; fi; fi

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/%.o: src/%.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIBRARY): $(OBJECTS)
	rm -f $@
	$(AR) rcs $@ $^

$(ABI_TEST_OBJECT): tests/abi_compile.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: tests/%.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

dependencies:
	@set -e; for lib in $(DEPENDENCIES) $(TEST_DEPENDENCIES); do $(MAKE) -C $(SYSTEM_LIBS_DIR)/$$lib all; done
	@set -e; for lib in $(SPECIAL_DEPENDENCIES); do $(MAKE) -C $(SPECIAL_LIBS_DIR)/$$lib all; done
	$(MAKE) -C $(CRT_DIR) all
	$(MAKE) -C $(SYSCALL_DIR) all

$(DEPENDENCY_ARCHIVES) $(TEST_ARCHIVES) $(CRT0) $(CRT_LIBRARY) $(SYSCALL_LIBRARY): dependencies

# One program per runtime test. The archives sit in a group because the
# library, its dependencies and the fakes reference each other.
$(TEST_PROGRAMS): $(BUILD_DIR)/%: $(BUILD_DIR)/%.o $(LIBRARY) $(FAKE_OBJECTS) \
                  $(DEPENDENCY_ARCHIVES) $(TEST_ARCHIVES) $(CRT0) $(CRT_LIBRARY) $(SYSCALL_LIBRARY)
	$(CC) $(TEST_LDFLAGS) -nostdlib -no-pie -Wl,-e,_start $(CRT0) $< $(FAKE_OBJECTS) \
		-Wl,--start-group $(LIBRARY) $(DEPENDENCY_ARCHIVES) $(TEST_ARCHIVES) $(CRT_LIBRARY) $(SYSCALL_LIBRARY) -Wl,--end-group -o $@

clean:
	rm -rf $(BUILD_DIR)
