SUBDIRS = string_utils cli

.PHONY: all run clean

all:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir all || exit 1; \
	done

run: all
	@echo "========================================"
	@echo "Running string_utils tests"
	@echo "========================================"
	@$(MAKE) -C string_utils run
	@echo "========================================"
	@echo "Running cli tests"
	@echo "========================================"
	@$(MAKE) -C cli run
	@echo "========================================"
	@echo "All test suites passed"
	@echo "========================================"

clean:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done
