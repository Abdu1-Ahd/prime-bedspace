/**
 * ==============================================================================
 * Project: Prime BedSpace
 * File: patient_simulator.c
 * Group: <Group XX>
 * Members: <Member 1>, <Member 2>
 * Date: 2026-05-08
 * Purpose: Simulates a patient treatment lifecycle including random sleep and 
 *          FIFO IPC messaging upon discharge.
 * Compile: gcc -Wall -Wextra -pthread src/patient_simulator.c src/ipc_utils.c -o build/patient_simulator -lrt
 * Usage: ./build/patient_simulator <patient_id> <triage_level> <bed_id> <bed_type>
 * ==============================================================================
 */

#include "types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

int main(int argc, char *argv[]) {
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <patient_id> <triage_level> <bed_id> <bed_type>\n", argv[0]);
        return 1;
    }

    int patient_id = atoi(argv[1]);
    int triage_level = atoi(argv[2]);
    int bed_id = atoi(argv[3]);
    char *bed_type = argv[4];

    // Step 1 - Print arrival message
    // ICU = bold red, ISOLATION = bold yellow, GENERAL = bold cyan
    char *color = "\033[0m"; // Default NC
    if (strcmp(bed_type, "ICU") == 0) {
        color = "\033[1;31m"; // Bold Red
    } else if (strcmp(bed_type, "ISOLATION") == 0) {
        color = "\033[1;33m"; // Bold Yellow
    } else if (strcmp(bed_type, "GENERAL") == 0) {
        color = "\033[1;36m"; // Bold Cyan
    }

    printf("%s[PATIENT %d] Arrived at %s bed %d | Priority: %d\033[0m\n", color, patient_id, bed_type, bed_id, triage_level);

    // Step 2 - Sleep random duration
    srand(getpid());
    int sleep_sec = 0;
    if (strcmp(bed_type, "ICU") == 0) {
        sleep_sec = (rand() % 11) + 5;
    } else if (strcmp(bed_type, "ISOLATION") == 0) {
        sleep_sec = (rand() % 8) + 3;
    } else if (strcmp(bed_type, "GENERAL") == 0) {
        sleep_sec = (rand() % 7) + 2;
    } else {
        sleep_sec = 1;
    }

    struct timespec req;
    req.tv_sec = sleep_sec;
    req.tv_nsec = 0;
    nanosleep(&req, NULL);

    // Step 3 - Print treatment complete message
    printf("[PATIENT %d] Treatment complete. Discharging from bed %d.\n", patient_id, bed_id);

    // Step 4 - Write patient_id as a string to named FIFO /tmp/discharge_fifo
    int fd = open("/tmp/discharge_fifo", O_WRONLY | O_NONBLOCK);
    if (fd != -1) {
        char pid_str[32];
        snprintf(pid_str, sizeof(pid_str), "%d", patient_id);
        // write the patient_id as a null-terminated string
        write(fd, pid_str, strlen(pid_str) + 1);
        close(fd);
    } else {
        fprintf(stderr, "[PATIENT %d] Warning: discharge FIFO not available.\n", patient_id);
    }

    // Step 5 - Exit with code 0
    return 0;
}
