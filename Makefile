CC = gcc
CFLAGS = -Wall -Wextra -pthread
LDFLAGS = -lrt -lpthread
BUILD_DIR = build
SRC_DIR = src

# stubs must define their entry points

ADMISSIONS_SRCS = $(SRC_DIR)/admissions.c $(SRC_DIR)/scheduler.c $(SRC_DIR)/bed_allocator.c $(SRC_DIR)/ipc_utils.c $(SRC_DIR)/terminal_ui.c
PATIENT_SIM_SRCS = $(SRC_DIR)/patient_simulator.c $(SRC_DIR)/ipc_utils.c

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

run:
	bash scripts/start_hospital.sh

test:
	bash scripts/stress_test.sh

valgrind:
	valgrind --leak-check=full --track-origins=yes ./build/admissions

.PHONY: all clean run test valgrind
