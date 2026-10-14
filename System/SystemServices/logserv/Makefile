SERVICE := logserv
include ../service.mk

build/faults.o: tests/faults.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
build/logserv-faults: $(OBJECTS) build/faults.o | dependencies
	$(LINK) $^ -Wl,--wrap=log_store_open -Wl,--wrap=log_store_append -Wl,--wrap=log_store_flush -Wl,--wrap=log_store_close -Wl,--wrap=thread_join $(LINK_LIBS) -o $@

.PHONY: check runtime-test
build/queue_test.o: tests/queue_test.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(ROOT)/System/SystemLibs/testing -c $< -o $@
build/queue_test: build/queue_test.o build/queue.o | dependencies
	$(LINK) $^ -Wl,--wrap=memory_allocate $(LINK_LIBS) -o $@
runtime-test: build/queue_test
check: all build/queue_test
	build/queue_test
