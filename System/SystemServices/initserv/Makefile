SERVICE := initserv
EXTRA_SYS_LIBS := random
REGISTRY ?= ../platform_registry.json
METADATA ?= ../applications.json
EXTRA_OBJECTS := build/registry.o
include ../service.mk
.PHONY: FORCE
FORCE:
build/generated/.stamp: FORCE tools/generate_registry.py $(REGISTRY) $(METADATA) | build
	python3 tools/generate_registry.py --registry $(REGISTRY) --metadata $(METADATA) --output build/generated
	touch $@
build/registry.o: build/generated/.stamp
	$(CC) $(CPPFLAGS) $(CFLAGS) -c build/generated/registry.c -o $@
.PHONY: check
build/policy_test.o: tests/policy_test.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(ROOT)/System/SystemLibs/testing -c $< -o $@
build/policy_test: build/policy_test.o | dependencies
	$(LINK) $< $(LINK_LIBS) -o $@
.PHONY: runtime-test
runtime-test: build/policy_test
check: all build/policy_test
	python3 -m unittest discover -s tests -p 'test_registry.py' -v
	build/policy_test
