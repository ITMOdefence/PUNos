CC ?= cc
PYTHON ?= python3
PUNOS_DIR ?= ../../..
PUNOS := $(abspath $(PUNOS_DIR))
BUILD_DIR ?= build
REGISTRY ?= ../platform_registry.json
METADATA ?= ../applications.json

SYSTEM_LIBS := core mem str process time thread eventloop
CPPFLAGS += -Iinclude -Isrc -I$(BUILD_DIR)/generated \
            -I$(PUNOS)/System/NativeRuntime/SyscallWrappers/include \
            -I$(PUNOS)/System/NativeRuntime/include \
            $(foreach lib,$(SYSTEM_LIBS),-I$(PUNOS)/System/SystemLibs/lib$(lib)/include) \
            -I$(PUNOS)/System/SpecialLibs/ipclib/include
CFLAGS := -std=c11 -ffreestanding -nostdinc -fno-builtin -fno-stack-protector \
          -fno-pic -m64 -Wall -Wextra -Werror -fno-tree-loop-distribute-patterns
CRT0 := $(PUNOS)/System/NativeRuntime/CRT/build/crt0.o
ARCHIVES := $(foreach lib,$(SYSTEM_LIBS),$(PUNOS)/System/SystemLibs/lib$(lib)/build/libpunos_$(lib).a) \
            $(PUNOS)/System/SpecialLibs/ipclib/build/libpunos_special_ipc.a \
            $(PUNOS)/System/NativeRuntime/CRT/build/libpunos_crt.a \
            $(PUNOS)/System/NativeRuntime/SyscallWrappers/build/libpunos_syscall.a
LINK = $(CC) -nostdlib -no-pie -Wl,-e,_start $(CRT0)
LINK_LIBS = -Wl,--start-group $(ARCHIVES) -Wl,--end-group
SOURCES := catalog service logger main
OBJECTS := $(addprefix $(BUILD_DIR)/,$(addsuffix .o,$(SOURCES))) $(BUILD_DIR)/generated_catalog.o

.PHONY: all dependencies check check-unit check-integration check-generator clean setup FORCE
all: $(BUILD_DIR)/appserv

setup:
	@test -f $(PUNOS)/System/NativeRuntime/CRT/Makefile || { echo "PUNOS_DIR must point to the platform checkout" >&2; exit 1; }

dependencies:
	@test -f $(PUNOS)/System/NativeRuntime/CRT/Makefile || { echo "Run make setup or set PUNOS_DIR" >&2; exit 1; }
	@set -e; for lib in $(SYSTEM_LIBS); do $(MAKE) -C $(PUNOS)/System/SystemLibs/lib$$lib all; done
	$(MAKE) -C $(PUNOS)/System/SpecialLibs/ipclib all
	$(MAKE) -C $(PUNOS)/System/NativeRuntime/CRT all
	$(MAKE) -C $(PUNOS)/System/NativeRuntime/SyscallWrappers all

$(BUILD_DIR):
	mkdir -p $@

# Always validate the selected inputs: changing REGISTRY/METADATA to an older
# file must not silently reuse a catalogue built from another configuration.
FORCE:

$(BUILD_DIR)/generated/.stamp: FORCE tools/generate_catalog.py $(REGISTRY) $(METADATA) | $(BUILD_DIR)
	$(PYTHON) tools/generate_catalog.py --registry $(REGISTRY) --metadata $(METADATA) --output $(BUILD_DIR)/generated
	touch $@

$(BUILD_DIR)/%.o: src/%.c include/appserv/appserv.h src/wire.h src/logger.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/generated_catalog.o: $(BUILD_DIR)/generated/.stamp include/appserv/appserv.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(BUILD_DIR)/generated/catalog.c -o $@

$(BUILD_DIR)/appserv: $(OBJECTS) | dependencies
	$(LINK) $(OBJECTS) $(LINK_LIBS) -o $@
	@test -z "$$(nm -u $@)"
	@if readelf -l $@ | grep -q INTERP; then echo "Unexpected ELF interpreter" >&2; exit 1; fi

$(BUILD_DIR)/service_test.o: tests/service_test.c include/appserv/appserv.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(PUNOS)/System/SystemLibs/testing -c $< -o $@

$(BUILD_DIR)/fakes.o: tests/fakes.c include/appserv/appserv.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/service_test: $(BUILD_DIR)/catalog.o $(BUILD_DIR)/service.o $(BUILD_DIR)/service_test.o $(BUILD_DIR)/fakes.o | dependencies
	$(LINK) $^ -Wl,--wrap=memory_allocate -Wl,--wrap=process_start \
	    -Wl,--wrap=process_terminate -Wl,--wrap=process_poll -Wl,--wrap=process_wait $(LINK_LIBS) -o $@

$(BUILD_DIR)/child.o: tests/child.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/child: $(BUILD_DIR)/child.o | dependencies
	$(LINK) $< $(LINK_LIBS) -o $@

check-generator:
	$(PYTHON) -m unittest discover -s tests -p 'test_generator.py' -v

check-unit: $(BUILD_DIR)/service_test
	$(BUILD_DIR)/service_test

check-integration: $(BUILD_DIR)/child $(BUILD_DIR)/catalog.o $(BUILD_DIR)/service.o
	$(PYTHON) tests/integration.py --punos $(PUNOS) --build $(abspath $(BUILD_DIR))

check: all check-generator check-unit check-integration

clean:
	rm -rf $(BUILD_DIR)
