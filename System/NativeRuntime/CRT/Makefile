CC ?= cc
AR ?= ar

SYSCALL_DIR := ../SyscallWrappers
SYSCALL_LIBRARY := $(SYSCALL_DIR)/build/libpunos_syscall.a

CPPFLAGS := -I../include -I$(SYSCALL_DIR)/include
CFLAGS := -std=c11 -ffreestanding -nostdinc -fno-builtin \
          -fno-stack-protector -fno-pic -m64 -Wall -Wextra -Werror
ASFLAGS := -ffreestanding -fno-stack-protector -fno-pic -m64

BUILD_DIR := build
CRT0_OBJECT := $(BUILD_DIR)/crt0.o
CRT_OBJECT := $(BUILD_DIR)/crt.o
CRT_LIBRARY := $(BUILD_DIR)/libpunos_crt.a
TEST_OBJECT := $(BUILD_DIR)/minimal_program.o
TEST_PROGRAM := $(BUILD_DIR)/minimal_program

.PHONY: all check clean syscall-wrappers

all: $(CRT0_OBJECT) $(CRT_LIBRARY)

check: $(TEST_PROGRAM)
	@set +e; $(TEST_PROGRAM); status=$$?; \
	if [ $$status -ne 37 ]; then \
		echo "Minimal PUNos program returned $$status, expected 37." >&2; \
		exit 1; \
	fi
	@echo "CRT runtime test passed."

$(BUILD_DIR):
	mkdir -p $@

$(CRT0_OBJECT): src/x86_64/crt0.S | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(CRT_OBJECT): src/crt.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(CRT_LIBRARY): $(CRT_OBJECT)
	$(AR) rcs $@ $^

$(TEST_OBJECT): tests/minimal_program.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

syscall-wrappers:
	$(MAKE) -C $(SYSCALL_DIR) all

$(SYSCALL_LIBRARY): syscall-wrappers

$(TEST_PROGRAM): $(CRT0_OBJECT) $(CRT_LIBRARY) $(TEST_OBJECT) $(SYSCALL_LIBRARY)
	$(CC) -nostdlib -no-pie -Wl,-e,_start \
		$(CRT0_OBJECT) $(TEST_OBJECT) $(CRT_LIBRARY) $(SYSCALL_LIBRARY) \
		-o $@

clean:
	rm -rf $(BUILD_DIR)
