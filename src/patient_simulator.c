/*
 * ============================================================
 * Project : Prime BedSpace - Hospital Patient Triage & Bed Allocator
 * File    : patient_simulator.c
 * Group   : Group 14
 * Members : Abdul Ahad (24F-0727), Abdul Rahim (24F-0514)
 * Date    : 2026-05-12
 * Purpose : Simulates patient treatment lifecycle — prints arrival/discharge messages, sleeps per bed type, and notifies admissions via FIFO.
 * Compile : gcc -Wall -Wextra -pthread -Iinclude <file> -lrt -lpthread
 * ============================================================
 */
#include "types.h"
#include "ipc.h"
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

    char *color = "\033[0m"; 
    if (!strcmp(bed_type, "ICU")) {
        color = "\033[1;31m"; 
    } else if (!strcmp(bed_type, "ISOLATION")) {
        color = "\033[1;33m"; 
    } else if (!strcmp(bed_type, "GENERAL")) {
        color = "\033[1;36m"; 
    }

    printf("%s[PATIENT %d] Arrived at %s bed %d | Priority: %d\033[0m\n", color, patient_id, bed_type, bed_id, triage_level);

    srand(getpid());
    int sleep_sec = 0;
    if (!strcmp(bed_type, "ICU")) {
        sleep_sec = (rand() % 11) + 5;
    } else if (!strcmp(bed_type, "ISOLATION")) {
        sleep_sec = (rand() % 8) + 3;
    } else if (!strcmp(bed_type, "GENERAL")) {
        sleep_sec = (rand() % 7) + 2;
    } else {
        sleep_sec = 1;
    }

    struct timespec req;
    req.tv_sec = sleep_sec;
    req.tv_nsec = 0;
    nanosleep(&req, NULL);

    printf("[PATIENT %d] Treatment complete. Discharging from bed %d.\n", patient_id, bed_id);

    int fd = open_discharge_fifo_write();
    if (fd != -1) {
        char pid_str[32];
        snprintf(pid_str, sizeof(pid_str), "%d", patient_id);
        write(fd, pid_str, strlen(pid_str) + 1);
        close(fd);
    } else {
        fprintf(stderr, "[PATIENT %d] Warning: discharge FIFO not available.\n", patient_id);
    }

    return 0;
}

// session:1ff848f2b
