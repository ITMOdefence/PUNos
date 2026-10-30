# Сборка msgserv внутри дерева PUNos. Исходники и тесты лежат здесь, заголовки,
# библиотеки и CRT берутся из корня PUNos (PUNOS_DIR), реестр и каталог
# приложений - из System/SystemServices, как у appserv.
CC ?= cc
PYTHON ?= python3
PUNOS_DIR ?= ../../..
PUNOS := $(abspath $(PUNOS_DIR))
BUILD_DIR ?= build
GENERATED := $(BUILD_DIR)/generated

# Реестр и каталог приложений - единственные источники PUNos, копий нет.
REGISTRY ?= ../platform_registry.json
METADATA ?= ../applications.json
CONFIG ?= config/messaging.json
SALT ?= secret.salt

SYSTEM_LIBS := core mem str process time eventloop
CPPFLAGS += -Iinclude -Isrc -I$(GENERATED) \
            -I$(PUNOS)/System/NativeRuntime/SyscallWrappers/include \
            -I$(PUNOS)/System/NativeRuntime/include \
            $(foreach lib,$(SYSTEM_LIBS),-I$(PUNOS)/System/SystemLibs/lib$(lib)/include) \
            -I$(PUNOS)/System/SpecialLibs/ipclib/include
CFLAGS := -std=c11 -ffreestanding -nostdinc -fno-builtin -fno-stack-protector \
          -fno-pic -m64 -Wall -Wextra -Werror -fno-tree-loop-distribute-patterns
TESTING := $(PUNOS)/System/SystemLibs/testing
CRT0 := $(PUNOS)/System/NativeRuntime/CRT/build/crt0.o
ARCHIVES := $(foreach lib,$(SYSTEM_LIBS),$(PUNOS)/System/SystemLibs/lib$(lib)/build/libpunos_$(lib).a) \
            $(PUNOS)/System/SpecialLibs/ipclib/build/libpunos_special_ipc.a \
            $(PUNOS)/System/NativeRuntime/CRT/build/libpunos_crt.a \
            $(PUNOS)/System/NativeRuntime/SyscallWrappers/build/libpunos_syscall.a
LINK = $(CC) -nostdlib -no-pie -Wl,-e,_start $(CRT0)
LINK_LIBS = -Wl,--start-group $(ARCHIVES) -Wl,--end-group

SOURCES := broker logger main
OBJECTS := $(addprefix $(BUILD_DIR)/,$(addsuffix .o,$(SOURCES))) \
           $(BUILD_DIR)/messaging_catalog.o
HEADERS := include/msgserv.h src/wire.h src/logger.h

.PHONY: all setup secret dependencies check check-unit check-protocol check-faults \
        check-generator check-integration runtime-test clean FORCE
all: $(BUILD_DIR)/msgserv

# Дерево PUNos должно лежать рядом (PUNOS_DIR): заголовки, библиотеки и CRT
# берутся только оттуда.
setup:
	@test -f $(PUNOS)/System/NativeRuntime/CRT/Makefile || \
	    { echo "PUNOS_DIR должен указывать на корень PUNos" >&2; exit 1; }

dependencies: | setup
	@set -e; for lib in $(SYSTEM_LIBS); do $(MAKE) -C $(PUNOS)/System/SystemLibs/lib$$lib all; done
	$(MAKE) -C $(PUNOS)/System/SpecialLibs/ipclib all
	$(MAKE) -C $(PUNOS)/System/NativeRuntime/CRT all
	$(MAKE) -C $(PUNOS)/System/NativeRuntime/SyscallWrappers all

$(BUILD_DIR):
	mkdir -p $@

# Соль живёт вне репозитория. Новая соль обнуляет все выданные секреты,
# поэтому она создаётся один раз и не перегенерируется автоматически.
secret:
	@test ! -f $(SALT) || { echo "$(SALT) уже существует: удалите его осознанно" >&2; exit 1; }
	head -c 32 /dev/urandom > $(SALT)
	@echo "соль создана: $(SALT)"

$(SALT):
	@head -c 32 /dev/urandom > $@
	@echo "соль создана автоматически: $@ (для воспроизводимого образа храните её)" >&2

# Входные данные проверяются всегда: подмена REGISTRY/METADATA на старый файл
# не должна молча переиспользовать каталог от другой конфигурации.
FORCE:

$(GENERATED)/.stamp: FORCE tools/generate_messaging.py $(CONFIG) $(REGISTRY) $(METADATA) $(SALT) | $(BUILD_DIR)
	$(PYTHON) tools/generate_messaging.py --config $(CONFIG) --registry $(REGISTRY) \
	    --metadata $(METADATA) --salt-file $(SALT) --output $(GENERATED) \
	    --require-service msgserv
	touch $@

$(BUILD_DIR)/%.o: src/%.c $(HEADERS) $(GENERATED)/.stamp | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/messaging_catalog.o: $(GENERATED)/.stamp include/msgserv.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(GENERATED)/messaging_catalog.c -o $@

$(BUILD_DIR)/msgserv: $(OBJECTS) | dependencies
	$(LINK) $(OBJECTS) $(LINK_LIBS) -o $@
	@test -z "$$(nm -u $@)"
	@if readelf -l $@ | grep -q INTERP; then echo "Неожиданный ELF interpreter" >&2; exit 1; fi

# ---- тесты ----------------------------------------------------------------

TEST_PROGRAMS := $(BUILD_DIR)/broker_test $(BUILD_DIR)/protocol_test $(BUILD_DIR)/fault_test

$(BUILD_DIR)/%_test.o: tests/%_test.c $(HEADERS) tests/support.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(TESTING) -Itests -c $< -o $@

$(BUILD_DIR)/broker_test: $(BUILD_DIR)/broker_test.o $(BUILD_DIR)/broker.o | dependencies
	$(LINK) $^ $(LINK_LIBS) -o $@

# Инъекция отказов живёт в отдельной программе: --wrap действует на всю
# ссылку, и в broker_test он подменял бы вызовы клиентской половины теста.
$(BUILD_DIR)/fault_test: $(BUILD_DIR)/fault_test.o $(BUILD_DIR)/broker.o | dependencies
	$(LINK) $^ -Wl,--wrap=memory_allocate -Wl,--wrap=ipc_connection_send \
	    -Wl,--wrap=ipc_connection_flush -Wl,--wrap=ipc_message_destroy \
	    -Wl,--allow-multiple-definition $(LINK_LIBS) -o $@

$(BUILD_DIR)/protocol_test: $(BUILD_DIR)/protocol_test.o $(BUILD_DIR)/broker.o | dependencies
	$(LINK) $^ $(LINK_LIBS) -o $@

runtime-test: $(TEST_PROGRAMS)

check-unit: $(BUILD_DIR)/broker_test
	$(BUILD_DIR)/broker_test

check-protocol: $(BUILD_DIR)/protocol_test
	$(BUILD_DIR)/protocol_test

check-faults: $(BUILD_DIR)/fault_test
	$(BUILD_DIR)/fault_test

check-generator:
	$(PYTHON) -m unittest discover -s tests -p 'test_generator.py' -v

check-integration: all
	$(PYTHON) tests/integration.py --punos $(PUNOS) --build $(abspath $(BUILD_DIR))

check: all check-generator check-unit check-protocol check-faults check-integration

clean:
	rm -rf $(BUILD_DIR)
