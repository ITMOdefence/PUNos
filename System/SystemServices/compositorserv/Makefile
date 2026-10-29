CC ?= cc
AR ?= ar

ROOT := ../../..
NATIVE := ../../NativeRuntime
SYSTEM_LIBS := ../../SystemLibs
SPECIAL_LIBS := ../../SpecialLibs
GUI_PROTOCOLS := $(ROOT)/GUI/Protocols
DISPLAY_SERV := ../displayserv

CRT := $(NATIVE)/CRT
SYSCALL := $(NATIVE)/SyscallWrappers
CRT0 := $(CRT)/build/crt0.o
CRT_LIBRARY := $(CRT)/build/libpunos_crt.a
SYSCALL_LIBRARY := $(SYSCALL)/build/libpunos_syscall.a

SYSTEM_DEPENDENCIES := libeventloop libgraphics libprocess libstr libtime libthread libmem librandom libcore
SYSTEM_ARCHIVES := $(foreach dependency,$(SYSTEM_DEPENDENCIES),\
    $(SYSTEM_LIBS)/$(dependency)/build/libpunos_$(patsubst lib%,%,$(dependency)).a)
IPC_LIBRARY := $(SPECIAL_LIBS)/ipclib/build/libpunos_special_ipc.a
DISPLAY_LIBRARY := $(SPECIAL_LIBS)/displaylib/build/libpunos_special_display.a
PROTOCOL_LIBRARY := $(GUI_PROTOCOLS)/build/libpunos_gui_protocol.a
DISPLAY_SERV_LIBRARY := $(DISPLAY_SERV)/build/libpunos_displayserv.a

CPPFLAGS := -Iinclude -I$(GUI_PROTOCOLS)/include \
             -I$(SPECIAL_LIBS)/ipclib/include \
             -I$(SPECIAL_LIBS)/displaylib/include \
             -I$(DISPLAY_SERV)/include \
             $(foreach dependency,$(SYSTEM_DEPENDENCIES),\
                 -I$(SYSTEM_LIBS)/$(dependency)/include) \
             -I$(SYSCALL)/include -I$(NATIVE)/include
CFLAGS := -std=c11 -ffreestanding -nostdinc -fno-builtin \
          -fno-stack-protector -fno-pic -m64 -Wall -Wextra -Werror \
          -fno-tree-loop-distribute-patterns

BUILD := build
LIBRARY := $(BUILD)/libpunos_compositorserv.a
OBJECT := $(BUILD)/compositorserv.o
RENDERER_OBJECT := $(BUILD)/renderer.o
MAIN_OBJECT := $(BUILD)/main.o
PROGRAM := $(BUILD)/compositorserv
RUNTIME_OBJECT := $(BUILD)/compositorserv_runtime.o
RUNTIME_PROGRAM := $(BUILD)/compositorserv_runtime
INTEGRATION_OBJECT := $(BUILD)/compositorserv_integration.o
INTEGRATION_PROGRAM := $(BUILD)/compositorserv_integration

.PHONY: all check clean runtime-test integration-test dependencies

all: $(LIBRARY) $(PROGRAM)
runtime-test: $(RUNTIME_PROGRAM)
integration-test: $(INTEGRATION_PROGRAM)
	$(INTEGRATION_PROGRAM)
check: $(RUNTIME_PROGRAM) $(INTEGRATION_PROGRAM)
	$(RUNTIME_PROGRAM)
	$(INTEGRATION_PROGRAM)

$(BUILD):
	mkdir -p $@

$(OBJECT): src/compositorserv.c include/compositorserv.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(RENDERER_OBJECT): src/renderer.c src/renderer.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(MAIN_OBJECT): src/main.c include/compositorserv.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(RUNTIME_OBJECT): tests/compositorserv_runtime.c include/compositorserv.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(INTEGRATION_OBJECT): tests/compositorserv_integration.c include/compositorserv.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIBRARY): $(OBJECT) $(RENDERER_OBJECT)
	rm -f $@
	$(AR) rcs $@ $^

dependencies:
	$(MAKE) -C $(CRT) all
	$(MAKE) -C $(SYSCALL) all
	$(MAKE) -C $(SYSTEM_LIBS) all
	$(MAKE) -C $(SPECIAL_LIBS) all
	$(MAKE) -C $(GUI_PROTOCOLS) all
	$(MAKE) -C $(DISPLAY_SERV) build/libpunos_displayserv.a

$(CRT0) $(CRT_LIBRARY) $(SYSCALL_LIBRARY) $(SYSTEM_ARCHIVES) \
$(IPC_LIBRARY) $(DISPLAY_LIBRARY) $(PROTOCOL_LIBRARY) $(DISPLAY_SERV_LIBRARY): dependencies

$(PROGRAM): $(MAIN_OBJECT) $(LIBRARY) $(PROTOCOL_LIBRARY) \
            $(IPC_LIBRARY) $(SYSTEM_ARCHIVES) \
            $(CRT0) $(CRT_LIBRARY) $(SYSCALL_LIBRARY)
	$(CC) -nostdlib -no-pie -Wl,-e,_start \
		$(CRT0) $(MAIN_OBJECT) $(LIBRARY) $(PROTOCOL_LIBRARY) \
		$(IPC_LIBRARY) $(SYSTEM_ARCHIVES) \
		$(CRT_LIBRARY) $(SYSCALL_LIBRARY) -o $@

$(RUNTIME_PROGRAM): $(RUNTIME_OBJECT) $(LIBRARY) $(PROTOCOL_LIBRARY) \
                    $(IPC_LIBRARY) $(SYSTEM_ARCHIVES) \
                    $(CRT0) $(CRT_LIBRARY) $(SYSCALL_LIBRARY)
	$(CC) -nostdlib -no-pie -Wl,-e,_start \
		$(CRT0) $(RUNTIME_OBJECT) $(LIBRARY) $(PROTOCOL_LIBRARY) \
		$(IPC_LIBRARY) $(SYSTEM_ARCHIVES) \
		$(CRT_LIBRARY) $(SYSCALL_LIBRARY) -o $@

$(INTEGRATION_PROGRAM): $(INTEGRATION_OBJECT) $(LIBRARY) $(DISPLAY_SERV_LIBRARY) \
                        $(PROTOCOL_LIBRARY) $(DISPLAY_LIBRARY) $(IPC_LIBRARY) \
                        $(SYSTEM_ARCHIVES) $(CRT0) $(CRT_LIBRARY) \
                        $(SYSCALL_LIBRARY)
	$(CC) -nostdlib -no-pie -Wl,-e,_start \
		$(CRT0) $(INTEGRATION_OBJECT) $(LIBRARY) $(DISPLAY_SERV_LIBRARY) \
		$(PROTOCOL_LIBRARY) $(DISPLAY_LIBRARY) $(IPC_LIBRARY) \
		$(SYSTEM_ARCHIVES) $(CRT_LIBRARY) $(SYSCALL_LIBRARY) -o $@

clean:
	rm -rf $(BUILD)
