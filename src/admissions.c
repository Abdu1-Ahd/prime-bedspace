/**
 * ==============================================================================
 * Project: Prime BedSpace
 * File: admissions.c
 * Group: Zawiar & Subhani
 * Members: Abdul Ahad Zawiar (Abdu1-Ahd), AbdulRahim Subhani (abdulrahim-subh)
 * Date: 2026-05-08
 * Purpose: Main admissions manager using fork/exec and IPC FIFOs.
 * Compile: gcc -Wall -Wextra -pthread src/admissions.c src/scheduler.c src/bed_allocator.c src/ipc_utils.c src/terminal_ui.c -o build/admissions -lrt
 * Usage: ./build/admissions
 * ==============================================================================
 */

#include "types.h"
#include "ipc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>

pid_t child_pids[50];
int child_count = 0;
BedPartition *shm_ward = NULL;
volatile sig_atomic_t running = 1;

void sigchld_handler(int sig) {
    (void)sig;
    pid_t pid;
    int status;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < child_count; i++) {
            if (child_pids[i] == pid) {
                for (int j = i; j < child_count - 1; j++) {
                    child_pids[j] = child_pids[j + 1];
                }
                child_count--;
                break;
            }
        }
    }
}

void sigterm_handler(int sig) {
    (void)sig;
    running = 0;
}

void admit_patient(PatientRecord *p) {
    if (child_count >= 50) {
        fprintf(stderr, "[ADMISSIONS] Queue full.\n");
        return;
    }

    int bed_id = -1;
    for (int i = 0; i < MAX_BEDS; i++) {
        if (shm_ward[i].is_free == 1 && shm_ward[i].size >= p->care_units) {
            bed_id = i;
            break;
        }
    }

    if (bed_id == -1) {
        printf("[ADMISSIONS] No suitable bed available for patient %d. Queuing.\n", p->patient_id);
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork failed");
        return;
    }

    if (pid == 0) {
        char patient_id_str[32];
        char triage_str[32];
        char bed_id_str[32];

        snprintf(patient_id_str, sizeof(patient_id_str), "%d", p->patient_id);
        snprintf(triage_str, sizeof(triage_str), "%d", p->priority);
        snprintf(bed_id_str, sizeof(bed_id_str), "%d", bed_id);

        char *argv[] = {
            "./build/patient_simulator",
            patient_id_str,
            triage_str,
            bed_id_str,
            shm_ward[bed_id].bed_type,
            NULL
        };

        execv("./build/patient_simulator", argv);
        perror("execv failed");
        exit(1);
    } else {
        child_pids[child_count++] = pid;
        shm_ward[bed_id].is_free = 0;
        shm_ward[bed_id].patient_id = p->patient_id;
        printf("[ADMISSIONS] Patient %d admitted to %s bed %d\n", p->patient_id, shm_ward[bed_id].bed_type, bed_id);
    }
}

int main(void) {
    shm_ward = (BedPartition *)init_shared_memory();
    if (shm_ward == NULL) {
        fprintf(stderr, "[ADMISSIONS] Shared memory initialization failed.\n");
        exit(1);
    }

    for (int i = 0; i < MAX_BEDS; i++) {
        shm_ward[i].partition_id = i;
        shm_ward[i].is_free = 1;
        shm_ward[i].patient_id = -1;

        if (i < 4) {
            strcpy(shm_ward[i].bed_type, "ICU");
            shm_ward[i].size = 3;
            shm_ward[i].start_unit = i * 3;
        } else if (i < 8) {
            strcpy(shm_ward[i].bed_type, "ISOLATION");
            shm_ward[i].size = 2;
            shm_ward[i].start_unit = 12 + (i - 4) * 2;
        } else {
            strcpy(shm_ward[i].bed_type, "GENERAL");
            shm_ward[i].size = 1;
            shm_ward[i].start_unit = 20 + (i - 8);
        }
    }
    printf("[ADMISSIONS] Shared memory initialized.\n");

    struct sigaction sa_chld;
    memset(&sa_chld, 0, sizeof(sa_chld));
    sa_chld.sa_handler = sigchld_handler;
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa_chld, NULL);

    struct sigaction sa_term;
    memset(&sa_term, 0, sizeof(sa_term));
    sa_term.sa_handler = sigterm_handler;
    sigaction(SIGTERM, &sa_term, NULL);

    int fd = open_discharge_fifo_read();

    while (running == 1) {
        if (fd != -1) {
            char buf[32];
            ssize_t n;
            while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
                buf[n] = '\0';
                buf[strcspn(buf, "\n\r ")] = '\0';
                int discharged_id = atoi(buf);
                for (int i = 0; i < MAX_BEDS; i++) {
                    if (!shm_ward[i].is_free && shm_ward[i].patient_id == discharged_id) {
                        shm_ward[i].is_free = 1;
                        shm_ward[i].patient_id = -1;
                        printf("[ADMISSIONS] Bed freed for patient %d\n", discharged_id);
                        break;
                    }
                }
            }
        } else {
            fd = open_discharge_fifo_read();
        }

        sleep(1);
    }

    if (fd != -1) {
        close(fd);
    }
    detach_shared_memory(shm_ward);
    printf("[ADMISSIONS] Shutting down.\n");
    return 0;
}
