LIBRARIES := ipclib displaylib

.PHONY: all check clean

all:
	@set -e; for library in $(LIBRARIES); do $(MAKE) -C $$library all; done

check:
	@set -e; for library in $(LIBRARIES); do $(MAKE) -C $$library check; done

clean:
	@set -e; for library in $(LIBRARIES); do $(MAKE) -C $$library clean; done
