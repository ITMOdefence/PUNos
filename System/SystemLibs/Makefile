# Builds and tests every System Lib. Libraries are listed in dependency order.
LIBS := libcore libmath libmem librandom libgraphics libstr libfile libtime libprocess libthread libsync libeventloop libinputdevice libv4l2

.PHONY: all check clean

all check clean:
	@set -e; for lib in $(LIBS); do $(MAKE) -C $$lib $@; done
