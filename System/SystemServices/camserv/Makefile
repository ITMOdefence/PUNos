SERVICE := camserv
DEVICE_LIB_DIR := camlib
DEVICE_LIB_NAME := camera
DEVICE_SYS_LIB := v4l2
EXTRA_OBJECTS := build/logger.o
include ../service.mk

build/logger.o: ../common/logger.c ../common/logger.h ../common/wire.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

build/protocol_test.o: tests/protocol_test.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(ROOT)/System/SystemLibs/testing -c $< -o $@
build/protocol_test: build/protocol_test.o build/protocol.o | dependencies
	$(LINK) $^ $(LINK_LIBS) -o $@
.PHONY: check
check: all build/protocol_test
	build/protocol_test
