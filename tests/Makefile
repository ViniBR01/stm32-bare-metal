SUBDIRS = string_utils cli gpio rcc timer

COVERAGE_INFO = coverage.info
COVERAGE_FILT = coverage-filtered.info
COVERAGE_HTML = coverage-html

.PHONY: all run clean coverage

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
	@echo "Running gpio tests"
	@echo "========================================"
	@$(MAKE) -C gpio run
	@echo "========================================"
	@echo "Running rcc tests"
	@echo "========================================"
	@$(MAKE) -C rcc run
	@echo "========================================"
	@echo "Running timer tests"
	@echo "========================================"
	@$(MAKE) -C timer run
	@echo "========================================"
	@echo "All test suites passed"
	@echo "========================================"

clean:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done
	@rm -f $(COVERAGE_INFO) $(COVERAGE_FILT)
	@rm -rf $(COVERAGE_HTML)

coverage: clean
	@echo "========================================"
	@echo "Building with coverage instrumentation"
	@echo "========================================"
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir run EXTRA_CFLAGS="--coverage" || exit 1; \
	done
	@echo "========================================"
	@echo "Generating coverage report"
	@echo "========================================"
	lcov --capture --directory . --output-file $(COVERAGE_INFO)
	lcov --extract $(COVERAGE_INFO) '*/utils/src/*' '*/drivers/src/*' \
		--output-file $(COVERAGE_FILT)
	genhtml $(COVERAGE_FILT) \
		--output-directory $(COVERAGE_HTML) \
		--title "STM32 Bare Metal Host Tests"
	@echo "========================================"
	@echo "Coverage report: tests/$(COVERAGE_HTML)/"
	@echo "========================================"
