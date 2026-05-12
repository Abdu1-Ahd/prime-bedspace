CC = gcc
CFLAGS = -Wall -Wextra -pthread -Iinclude
LDFLAGS = -lrt -lpthread
BUILD_DIR = build
SRC_DIR = src

# stubs must define their entry points

ADMISSIONS_SRCS = $(SRC_DIR)/admissions.c $(SRC_DIR)/scheduler.c $(SRC_DIR)/bed_allocator.c $(SRC_DIR)/ipc_utils.c $(SRC_DIR)/terminal_ui.c
PATIENT_SIM_SRCS = $(SRC_DIR)/patient_simulator.c $(SRC_DIR)/ipc_utils.c

SCHED_TEST_SRCS = $(SRC_DIR)/scheduler.c

all: $(BUILD_DIR) $(BUILD_DIR)/admissions $(BUILD_DIR)/patient_simulator
	@echo "[BUILD] Prime BedSpace compiled successfully."

$(BUILD_DIR)/admissions: $(ADMISSIONS_SRCS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/patient_simulator: $(PATIENT_SIM_SRCS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)
	rm -f logs/*.txt
	@echo "[CLEAN] Done."

run: all
	./build/admissions --strategy best

$(BUILD_DIR)/sched_test: $(SCHED_TEST_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -DSCHEDULER_TEST $^ -o $@ $(LDFLAGS)

test: $(BUILD_DIR)/sched_test
	@mkdir -p logs
	@echo "[TEST] Running scheduler self-test..."
	@$(BUILD_DIR)/sched_test && echo "[TEST] Result: PASS" || echo "[TEST] Result: FAIL"

valgrind: all
	valgrind --leak-check=full --track-origins=yes --tool=memcheck \
	         ./build/admissions --strategy best

.PHONY: all clean run test valgrind

