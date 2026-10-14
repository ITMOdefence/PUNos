# Build service snapshots against this PUNos tree, never their vendored libs.
PUNOS_ROOT := $(abspath ../..)
REGISTRY ?= $(PUNOS_ROOT)/System/SystemServices/platform_registry.json
APPSERV_ARGS := PUNOS_DIR=$(PUNOS_ROOT) REGISTRY=$(REGISTRY) METADATA=../applications.json

.PHONY: all appserv camserv IOserv runtime-test check clean
all: appserv camserv IOserv

appserv:
	$(MAKE) -C appserv $(APPSERV_ARGS) all
camserv:
	$(MAKE) -C camserv PUNOS_DIR=$(PUNOS_ROOT) all
IOserv:
	$(MAKE) -C IOserv PUNOS_DIR=$(PUNOS_ROOT) all

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
	$(MAKE) -C camserv clean
	$(MAKE) -C IOserv clean

logserv:
	$(MAKE) -C logserv PUNOS_DIR=$(PUNOS_ROOT) all
initserv:
	$(MAKE) -C initserv PUNOS_DIR=$(PUNOS_ROOT) REGISTRY=$(REGISTRY) all
all: logserv initserv
.PHONY: logserv initserv

.PHONY: check-base-services
check-base-services: logserv initserv
	$(MAKE) -C logserv PUNOS_DIR=$(PUNOS_ROOT) check
	$(MAKE) -C initserv PUNOS_DIR=$(PUNOS_ROOT) check
	python3 tests/services_integration.py
.PHONY: check-device-services
check-device-services: camserv IOserv
	$(MAKE) -C camserv PUNOS_DIR=$(PUNOS_ROOT) check
	$(MAKE) -C IOserv PUNOS_DIR=$(PUNOS_ROOT) check
	python3 tests/device_integration.py
check: check-base-services check-device-services
