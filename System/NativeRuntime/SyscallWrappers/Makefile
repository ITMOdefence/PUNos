CC ?= cc
AR ?= ar

CPPFLAGS := -Iinclude -I../include
CFLAGS := -std=c11 -ffreestanding -nostdinc -fno-builtin -fno-stack-protector \
          -fno-pic -m64 -Wall -Wextra -Werror
ASFLAGS := -ffreestanding -fno-stack-protector -fno-pic -m64

BUILD_DIR := build
RAW_OBJECT := $(BUILD_DIR)/raw_syscall.o
SPECIAL_OBJECT := $(BUILD_DIR)/special.o
BOOTSTRAP_OBJECT := $(BUILD_DIR)/bootstrap.o
ERRNO_OBJECT := $(BUILD_DIR)/errno.o
WRAPPERS_OBJECT := $(BUILD_DIR)/wrappers.o
LIBRARY_OBJECTS := $(RAW_OBJECT) $(SPECIAL_OBJECT) $(BOOTSTRAP_OBJECT) $(ERRNO_OBJECT) \
                   $(WRAPPERS_OBJECT)
ABI_TEST_OBJECT := $(BUILD_DIR)/abi_compile.o
RUNTIME_TEST_OBJECT := $(BUILD_DIR)/raw_syscall_runtime.o
RUNTIME_TEST := $(BUILD_DIR)/raw_syscall_runtime
PUBLIC_TEST_OBJECT := $(BUILD_DIR)/public_wrappers_runtime.o
PUBLIC_TEST := $(BUILD_DIR)/public_wrappers_runtime
FULL_TEST_OBJECT := $(BUILD_DIR)/full_wrappers_runtime.o
FULL_TEST := $(BUILD_DIR)/full_wrappers_runtime
LIBRARY := $(BUILD_DIR)/libpunos_syscall.a

.PHONY: all check clean full-test

all: $(LIBRARY)

full-test: $(FULL_TEST)

check: $(LIBRARY) $(ABI_TEST_OBJECT) $(RUNTIME_TEST) $(PUBLIC_TEST) $(FULL_TEST)
	$(RUNTIME_TEST)
	$(PUBLIC_TEST)
	$(FULL_TEST)
	@echo "Syscall Wrappers ABI compile checks passed."

$(BUILD_DIR):
	mkdir -p $@

$(RAW_OBJECT): src/x86_64/raw_syscall.S | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(SPECIAL_OBJECT): src/x86_64/special.S | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BOOTSTRAP_OBJECT): src/bootstrap.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(ERRNO_OBJECT): src/errno.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(WRAPPERS_OBJECT): src/wrappers.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(ABI_TEST_OBJECT): tests/abi_compile.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(RUNTIME_TEST_OBJECT): tests/raw_syscall_runtime.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(RUNTIME_TEST): $(RUNTIME_TEST_OBJECT) $(LIBRARY)
	$(CC) -nostdlib -no-pie -Wl,-e,_start $^ -o $@

$(PUBLIC_TEST_OBJECT): tests/public_wrappers_runtime.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(PUBLIC_TEST): $(PUBLIC_TEST_OBJECT) $(LIBRARY)
	$(CC) -nostdlib -no-pie -Wl,-e,_start $^ -o $@

$(FULL_TEST_OBJECT): tests/full_wrappers_runtime.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(FULL_TEST): $(FULL_TEST_OBJECT) $(LIBRARY)
	$(CC) -nostdlib -no-pie -Wl,-e,_start $^ -o $@

$(LIBRARY): $(LIBRARY_OBJECTS)
	$(AR) rcs $@ $^

clean:
	rm -rf $(BUILD_DIR)
