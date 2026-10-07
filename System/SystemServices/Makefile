# Build service snapshots against this PUNos tree, never their vendored libs.
PUNOS_ROOT := $(abspath ../..)
APPSERV_ARGS := PUNOS_DIR=$(PUNOS_ROOT) REGISTRY=../platform_registry.json METADATA=../applications.json

.PHONY: all appserv runtime-test check clean
all: appserv

appserv:
	$(MAKE) -C appserv $(APPSERV_ARGS) all

runtime-test:
	$(MAKE) -C appserv $(APPSERV_ARGS) build/service_test

check:
	$(MAKE) -C appserv $(APPSERV_ARGS) check

clean:
	$(MAKE) -C appserv clean
