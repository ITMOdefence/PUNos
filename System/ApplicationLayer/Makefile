# Builds and tests every Application Layer library. Libraries are listed in
# dependency order.
LIBS := appframework

.PHONY: all check clean

all check clean:
	@set -e; for lib in $(LIBS); do $(MAKE) -C $$lib $@; done
