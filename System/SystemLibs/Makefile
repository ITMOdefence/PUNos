# Builds and tests every System Lib. Libraries are listed in dependency order.
LIBS := libcore libmem libstr libtime libprocess libthread

.PHONY: all check clean

all check clean:
	@set -e; for lib in $(LIBS); do $(MAKE) -C $$lib $@; done
