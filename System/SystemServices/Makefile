# Build service snapshots against this PUNos tree, never their vendored libs.
PUNOS_ROOT := $(abspath ../..)
APPSERV_ARGS := PUNOS_DIR=$(PUNOS_ROOT) REGISTRY=../platform_registry.json METADATA=../applications.json

.PHONY: all appserv runtime-test check clean
all: appserv

appserv:
	$(MAKE) -C appserv $(APPSERV_ARGS) all

runtime-test:
	$(MAKE) -C appserv $(APPSERV_ARGS) build/service_test
	$(MAKE) -C logserv PUNOS_DIR=$(PUNOS_ROOT) runtime-test
	$(MAKE) -C initserv PUNOS_DIR=$(PUNOS_ROOT) runtime-test

check:
	$(MAKE) -C appserv $(APPSERV_ARGS) check

clean:
	$(MAKE) -C appserv clean
	$(MAKE) -C logserv clean
	$(MAKE) -C initserv clean

logserv:
	$(MAKE) -C logserv PUNOS_DIR=$(PUNOS_ROOT) all
initserv:
	$(MAKE) -C initserv PUNOS_DIR=$(PUNOS_ROOT) all
all: logserv initserv
.PHONY: logserv initserv

.PHONY: check-base-services
check-base-services: logserv initserv
	$(MAKE) -C logserv PUNOS_DIR=$(PUNOS_ROOT) check
	$(MAKE) -C initserv PUNOS_DIR=$(PUNOS_ROOT) check
	python3 tests/services_integration.py
check: check-base-services
