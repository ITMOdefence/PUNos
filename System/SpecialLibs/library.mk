CC ?= cc
AR ?= ar

SYSTEM_LIBS_DIR := ../../SystemLibs
NATIVE_DIR := ../../NativeRuntime
CRT_DIR := $(NATIVE_DIR)/CRT
SYSCALL_DIR := $(NATIVE_DIR)/SyscallWrappers
CRT0 := $(CRT_DIR)/build/crt0.o
CRT_LIBRARY := $(CRT_DIR)/build/libpunos_crt.a
SYSCALL_LIBRARY := $(SYSCALL_DIR)/build/libpunos_syscall.a

reverse = $(if $(1),$(call reverse,$(wordlist 2,$(words $(1)),$(1))) $(firstword $(1)))
DEPENDENCY_ARCHIVES := $(foreach dependency,$(DEPENDENCIES),\
    $(SYSTEM_LIBS_DIR)/$(dependency)/build/libpunos_$(patsubst lib%,%,$(dependency)).a)
CPPFLAGS := -Iinclude $(foreach dependency,$(DEPENDENCIES),\
            -I$(SYSTEM_LIBS_DIR)/$(dependency)/include) \
            -I$(SYSCALL_DIR)/include -I$(NATIVE_DIR)/include
CFLAGS := -std=c11 -ffreestanding -nostdinc -fno-builtin \
          -fno-stack-protector -fno-pic -m64 -Wall -Wextra -Werror \
          -fno-tree-loop-distribute-patterns

BUILD_DIR := build
LIBRARY := $(BUILD_DIR)/libpunos_$(LIBRARY_NAME).a
OBJECTS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(wildcard src/*.c))
ABI_TEST_OBJECT := $(BUILD_DIR)/abi_compile.o
RUNTIME_TEST_OBJECT := $(BUILD_DIR)/$(RUNTIME_TEST).o
RUNTIME_TEST_PROGRAM := $(BUILD_DIR)/$(RUNTIME_TEST)
HEADERS := $(wildcard include/special/*.h src/*.h)

.PHONY: all check clean dependencies
all: $(LIBRARY)
check: $(LIBRARY) $(ABI_TEST_OBJECT) $(RUNTIME_TEST_PROGRAM)
	$(RUNTIME_TEST_PROGRAM)

$(BUILD_DIR):
	mkdir -p $@
$(BUILD_DIR)/%.o: src/%.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(LIBRARY): $(OBJECTS)
	rm -f $@
	$(AR) rcs $@ $^
$(ABI_TEST_OBJECT): tests/abi_compile.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(RUNTIME_TEST_OBJECT): tests/$(RUNTIME_TEST).c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

dependencies:
	@set -e; for dependency in $(DEPENDENCIES); do $(MAKE) -C $(SYSTEM_LIBS_DIR)/$$dependency all; done
	$(MAKE) -C $(CRT_DIR) all
	$(MAKE) -C $(SYSCALL_DIR) all
$(DEPENDENCY_ARCHIVES) $(CRT0) $(CRT_LIBRARY) $(SYSCALL_LIBRARY): dependencies
$(RUNTIME_TEST_PROGRAM): $(RUNTIME_TEST_OBJECT) $(LIBRARY) $(DEPENDENCY_ARCHIVES) \
                         $(CRT0) $(CRT_LIBRARY) $(SYSCALL_LIBRARY)
	$(CC) -nostdlib -no-pie -Wl,-e,_start $(CRT0) $(RUNTIME_TEST_OBJECT) \
		$(LIBRARY) $(call reverse,$(DEPENDENCY_ARCHIVES)) \
		$(CRT_LIBRARY) $(SYSCALL_LIBRARY) -o $@
clean:
	rm -rf $(BUILD_DIR)
