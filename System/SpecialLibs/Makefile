# Builds and tests every Special Lib. Libraries are listed in dependency order.
LIBS := ipclib loglib inputlib camlib

.PHONY: all check clean

all check clean:
	@set -e; for lib in $(LIBS); do $(MAKE) -C $$lib $@; done
