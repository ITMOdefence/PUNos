SERVICES := displayserv

.PHONY: all check clean runtime-test

all check clean runtime-test:
	@set -e; for service in $(SERVICES); do $(MAKE) -C $$service $@; done
