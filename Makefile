BUILD_DIR ?= build
BUILD_STAMP := $(BUILD_DIR)/.dir
CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -pedantic
RUNTIME_INCLUDES := -Iruntime/include
BOOTLOADER_INCLUDES := -Ibootloader/include
RUNTIME_SRC := $(sort $(wildcard runtime/src/*.c))
PICBOOT_SRC := bootloader/src/picboot.c
PICBOOT_ORACLE_SRC := tools/picboot_oracle_check.c
PICFW_ORACLE_SRC := tools/picfw_oracle_check.c
TEST_ADAPTER_PROTOCOL_SRC := tests/test_adapter_protocol.c
TEST_RUNTIME_SRC := tests/test_runtime.c
PICBOOT_ORACLE_BIN := $(BUILD_DIR)/picboot_oracle_check
PICFW_ORACLE_BIN := $(BUILD_DIR)/picfw_oracle_check
TEST_ADAPTER_PROTOCOL_BIN := $(BUILD_DIR)/test_adapter_protocol
TEST_RUNTIME_BIN := $(BUILD_DIR)/test_runtime
PICBOOT_JSON := $(BUILD_DIR)/picboot_oracle_check.json

# --- Determinism Checks ---
SRC_DIRS := runtime/src runtime/include bootloader/src bootloader/include
PYTHON := python3
# Threshold lowered from 35 to 10 after refactoring FSM dispatchers and
# protocol validation into const dispatch tables and sub-handler functions.
# Peak CC after refactoring: 9 (four functions tied).
MAX_COMPLEXITY := 10
MAX_ISR_CYCLES := 60

.PHONY: build test oracle-check clean \
        check-all check-recursion check-malloc check-loops check-float check-complexity \
        check-lint check-isr check-delays check-wcet install-hooks

## Run all determinism checks (enabled subset)
check-all: check-recursion check-malloc check-loops check-float check-complexity check-lint
	@echo ""
	@echo "============================================"
	@echo "  ALL DETERMINISM CHECKS PASSED"
	@echo "============================================"

## R1: No recursion (direct or mutual)
check-recursion:
	@echo "--- [R1] No recursion ---"
	@$(PYTHON) scripts/check_no_recursion.py $(SRC_DIRS)

## R2/R7: No dynamic allocation, no VLAs
check-malloc:
	@echo "--- [R2/R7] No malloc/VLA ---"
	@$(PYTHON) scripts/check_no_malloc.py $(SRC_DIRS)

## R3: All loops must be bounded
check-loops:
	@echo "--- [R3] Bounded loops ---"
	@$(PYTHON) scripts/check_bounded_loops.py $(SRC_DIRS)

## R6: No floating point
check-float:
	@echo "--- [R6] No floating point ---"
	@$(PYTHON) scripts/check_no_float.py $(SRC_DIRS)

## R8: Cyclomatic complexity < threshold
check-complexity:
	@echo "--- [R8] Cyclomatic complexity ---"
	@$(PYTHON) scripts/check_complexity.py $(SRC_DIRS) --max=$(MAX_COMPLEXITY)

## Lint: cppcheck static analysis
check-lint:
	@echo "--- [LINT] cppcheck ---"
	@cppcheck --enable=warning,style,performance \
		--suppress=missingIncludeSystem \
		--suppress=knownConditionTrueFalse \
		--suppress=objectIndex \
		--suppress=constParameterCallback \
		--error-exitcode=1 \
		--inline-suppr \
		-I runtime/include -I bootloader/include \
		runtime/src/ bootloader/src/

# --- Optional checks (not in check-all) ---
# These are XC8/hardware-specific and skipped until targeting real PIC hardware.
# - check-isr: looks for __interrupt() which our HAL simulation model does not use
# - check-delays: looks for __delay_ms which our code does not use
# - check-wcet: WCET estimation is XC8-specific

## R4: ISR constraints (optional — requires __interrupt() pattern)
check-isr:
	@echo "--- [R4] ISR constraints (optional) ---"
	@$(PYTHON) scripts/check_isr_constraints.py $(SRC_DIRS)

## R5: No blocking delays (optional — requires __delay_ms pattern)
check-delays:
	@echo "--- [R5] No blocking delays (optional) ---"
	@$(PYTHON) scripts/check_no_delay_critical.py $(SRC_DIRS)

## R4: WCET estimation for ISRs (optional — XC8-specific)
check-wcet:
	@echo "--- [R4] ISR WCET estimation (optional) ---"
	@$(PYTHON) scripts/wcet_estimate.py $(SRC_DIRS) --max-isr-cycles=$(MAX_ISR_CYCLES)

## Install git hooks
install-hooks:
	@mkdir -p .git/hooks
	cp hooks/pre-commit .git/hooks/pre-commit
	chmod +x .git/hooks/pre-commit
	@echo "Pre-commit hook installed."

build: $(PICBOOT_ORACLE_BIN) $(PICFW_ORACLE_BIN) $(TEST_ADAPTER_PROTOCOL_BIN) $(TEST_RUNTIME_BIN)

$(BUILD_STAMP):
	mkdir -p "$(BUILD_DIR)"
	touch "$@"

$(PICBOOT_ORACLE_BIN): | $(BUILD_STAMP)
$(PICBOOT_ORACLE_BIN): $(PICBOOT_SRC) $(PICBOOT_ORACLE_SRC)
	$(CC) $(CFLAGS) $(BOOTLOADER_INCLUDES) $(PICBOOT_SRC) $(PICBOOT_ORACLE_SRC) -o "$@"

$(PICFW_ORACLE_BIN): | $(BUILD_STAMP)
$(PICFW_ORACLE_BIN): $(RUNTIME_SRC) $(PICFW_ORACLE_SRC)
	$(CC) $(CFLAGS) $(RUNTIME_INCLUDES) $(RUNTIME_SRC) $(PICFW_ORACLE_SRC) -o "$@"

$(TEST_ADAPTER_PROTOCOL_BIN): | $(BUILD_STAMP)
$(TEST_ADAPTER_PROTOCOL_BIN): $(RUNTIME_SRC) $(TEST_ADAPTER_PROTOCOL_SRC)
	$(CC) $(CFLAGS) $(RUNTIME_INCLUDES) $(RUNTIME_SRC) $(TEST_ADAPTER_PROTOCOL_SRC) -o "$@"

$(TEST_RUNTIME_BIN): | $(BUILD_STAMP)
$(TEST_RUNTIME_BIN): $(RUNTIME_SRC) $(TEST_RUNTIME_SRC)
	$(CC) $(CFLAGS) $(RUNTIME_INCLUDES) $(RUNTIME_SRC) $(TEST_RUNTIME_SRC) -o "$@"

test: build
	"./$(TEST_ADAPTER_PROTOCOL_BIN)"
	"./$(TEST_RUNTIME_BIN)"
	"./$(PICFW_ORACLE_BIN)"
	"./$(PICBOOT_ORACLE_BIN)"

oracle-check: build
	./scripts/adapterproto_oracle_check.sh
	"./$(PICBOOT_ORACLE_BIN)" --json > "$(PICBOOT_JSON)"

clean:
	rm -rf "$(BUILD_DIR)"
