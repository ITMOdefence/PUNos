LIBS := inputlib ipclib loglib
.PHONY: all check clean
all check clean:
	@set -e; for lib in $(LIBS); do $(MAKE) -C $$lib $@; done
