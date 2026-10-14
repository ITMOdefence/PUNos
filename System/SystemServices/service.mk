CC ?= cc
PUNOS_DIR ?= ../../..
ROOT := $(abspath $(PUNOS_DIR))
LIBS := core mem str file process time thread sync eventloop
CPPFLAGS += -Iinclude -Isrc -Ibuild/generated -I$(ROOT)/System/NativeRuntime/include -I$(ROOT)/System/NativeRuntime/SyscallWrappers/include $(foreach lib,$(LIBS),-I$(ROOT)/System/SystemLibs/lib$(lib)/include) -I$(ROOT)/System/SpecialLibs/ipclib/include -I$(ROOT)/System/SpecialLibs/loglib/include
CFLAGS := -std=c11 -ffreestanding -nostdinc -fno-builtin -fno-stack-protector -fno-pic -m64 -Wall -Wextra -Werror -fno-tree-loop-distribute-patterns
ARCHIVES := $(foreach lib,$(LIBS),$(ROOT)/System/SystemLibs/lib$(lib)/build/libpunos_$(lib).a) $(ROOT)/System/SpecialLibs/ipclib/build/libpunos_special_ipc.a $(ROOT)/System/SpecialLibs/loglib/build/libpunos_special_log.a $(ROOT)/System/NativeRuntime/CRT/build/libpunos_crt.a $(ROOT)/System/NativeRuntime/SyscallWrappers/build/libpunos_syscall.a
LINK = $(CC) -nostdlib -no-pie -Wl,-e,_start $(ROOT)/System/NativeRuntime/CRT/build/crt0.o
LINK_LIBS = -Wl,--start-group $(ARCHIVES) -Wl,--end-group
OBJECTS := $(patsubst src/%.c,build/%.o,$(wildcard src/*.c)) $(EXTRA_OBJECTS)
.PHONY: all dependencies clean
all: build/$(SERVICE)
dependencies:
	@set -e; for lib in $(LIBS); do $(MAKE) -C $(ROOT)/System/SystemLibs/lib$$lib all; done
	$(MAKE) -C $(ROOT)/System/SpecialLibs/ipclib all
	$(MAKE) -C $(ROOT)/System/SpecialLibs/loglib all
	$(MAKE) -C $(ROOT)/System/NativeRuntime/CRT all
	$(MAKE) -C $(ROOT)/System/NativeRuntime/SyscallWrappers all
build:
	mkdir -p $@
build/%.o: src/%.c $(wildcard include/*.h src/*.h) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
build/$(SERVICE): $(OBJECTS) | dependencies
	$(LINK) $(OBJECTS) $(LINK_LIBS) -o $@
	@test -z "$$(nm -u $@)"
	@if readelf -l $@ | grep -q INTERP; then exit 1; fi
clean:
	rm -rf build
