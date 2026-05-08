/**
 * ==============================================================================
 * Project: Prime BedSpace
 * File: admissions.c
 * Group: Zawiar & Subhani
 * Members: Abdul Ahad Zawiar (Abdu1-Ahd), AbdulRahim Subhani (abdulrahim-subh)
 * Date: 2026-05-08
 * Purpose: Main admissions manager — fork/exec patient lifecycle, IPC FIFO
 *          discharge handling, priority-queue backed waiting room, and
 *          end-of-session scheduling simulation report.
 * Compile: gcc -Wall -Wextra -pthread src/admissions.c src/scheduler.c
 *          src/bed_allocator.c src/ipc_utils.c src/terminal_ui.c
 *          -o build/admissions -Iinclude -lrt -lpthread
 * Usage: ./build/admissions
 * ==============================================================================
 */

#include "types.h"
#include "ipc.h"
#include "scheduler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>

/* ── Globals ─────────────────────────────────────────────────────────── */
pid_t child_pids[50];
int   child_count = 0;
BedPartition *shm_ward = NULL;
volatile sig_atomic_t running = 1;

static PriorityQueue g_wait_queue; /* patients waiting for a free bed */

/* ── Signal handlers ─────────────────────────────────────────────────── */

void sigchld_handler(int sig) {
    (void)sig;
    pid_t pid;
    int   status;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < child_count; i++) {
            if (child_pids[i] == pid) {
                for (int j = i; j < child_count - 1; j++)
                    child_pids[j] = child_pids[j + 1];
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

/* ── Internal helpers ────────────────────────────────────────────────── */

/* Returns index of first free bed that fits care_units, or -1 */
static int find_free_bed(int care_units) {
    for (int i = 0; i < MAX_BEDS; i++) {
        if (shm_ward[i].is_free == 1 && shm_ward[i].size >= care_units)
            return i;
    }
    return -1;
}

/* Fork and exec patient_simulator for a known patient + bed.
 * Records the admission event for the scheduling simulation. */
static void do_admit_to_bed(PatientRecord *p, int bed_id) {
    if (child_count >= 50) {
        fprintf(stderr, "[ADMISSIONS] child_pids full — cannot fork.\n");
        return;
    }

    time_t admit_time = time(NULL);
    pid_t  pid        = fork();

    if (pid < 0) {
        perror("[ADMISSIONS] fork failed");
        return;
    }

    if (pid == 0) {
        /* Child: exec patient_simulator */
        char patient_id_str[32], triage_str[32], bed_id_str[32];
        snprintf(patient_id_str, sizeof(patient_id_str), "%d", p->patient_id);
        snprintf(triage_str,     sizeof(triage_str),     "%d", p->priority);
        snprintf(bed_id_str,     sizeof(bed_id_str),     "%d", bed_id);

        char *argv[] = {
            "./build/patient_simulator",
            patient_id_str,
            triage_str,
            bed_id_str,
            shm_ward[bed_id].bed_type,
            NULL
        };
        execv("./build/patient_simulator", argv);
        perror("[ADMISSIONS] execv failed");
        exit(1);
    }

    /* Parent: register child and mark bed occupied */
    child_pids[child_count++] = pid;
    shm_ward[bed_id].is_free   = 0;
    shm_ward[bed_id].patient_id = p->patient_id;

    printf("[ADMISSIONS] Patient %d admitted to %s bed %d (priority %d)\n",
           p->patient_id, shm_ward[bed_id].bed_type, bed_id, p->priority);

    /* Record event for scheduling simulation */
    log_admission_event(p->patient_id, p->priority,
                        p->arrival_time, admit_time, p->care_units);
}

/* After a bed frees up, drain waiting patients that can now be admitted */
static void try_admit_from_queue(void) {
    while (!pq_is_empty(&g_wait_queue)) {
        PatientRecord *top = pq_peek(&g_wait_queue);
        if (top == NULL) break;

        int bed_id = find_free_bed(top->care_units);
        if (bed_id == -1) break; /* still no suitable bed */

        PatientRecord p = pq_pop(&g_wait_queue);
        printf("[ADMISSIONS] Draining wait queue: admitting patient %d "
               "(priority %d, queue depth now %d)\n",
               p.patient_id, p.priority, pq_size(&g_wait_queue));
        do_admit_to_bed(&p, bed_id);
    }
}

/* ── Public API ──────────────────────────────────────────────────────── */

void admit_patient(PatientRecord *p) {
    int bed_id = find_free_bed(p->care_units);

    if (bed_id == -1) {
        /* No suitable bed — push onto priority wait queue */
        if (pq_push(&g_wait_queue, *p) == 0) {
            printf("[ADMISSIONS] No bed available — patient %d queued "
                   "(priority %d, queue depth %d).\n",
                   p->patient_id, p->priority, pq_size(&g_wait_queue));
        } else {
            fprintf(stderr,
                    "[ADMISSIONS] Wait queue full — patient %d dropped.\n",
                    p->patient_id);
        }
        return;
    }

    do_admit_to_bed(p, bed_id);
}

/* ── main ────────────────────────────────────────────────────────────── */

int main(void) {
    /* Initialise priority wait queue */
    pq_init(&g_wait_queue);

    /* Initialise shared memory ward */
    shm_ward = (BedPartition *)init_shared_memory();
    if (shm_ward == NULL) {
        fprintf(stderr, "[ADMISSIONS] Shared memory initialization failed.\n");
        exit(1);
    }

    for (int i = 0; i < MAX_BEDS; i++) {
        shm_ward[i].partition_id = i;
        shm_ward[i].is_free      = 1;
        shm_ward[i].patient_id   = -1;

        if (i < 4) {
            strcpy(shm_ward[i].bed_type, "ICU");
            shm_ward[i].size       = 3;
            shm_ward[i].start_unit = i * 3;
        } else if (i < 8) {
            strcpy(shm_ward[i].bed_type, "ISOLATION");
            shm_ward[i].size       = 2;
            shm_ward[i].start_unit = 12 + (i - 4) * 2;
        } else {
            strcpy(shm_ward[i].bed_type, "GENERAL");
            shm_ward[i].size       = 1;
            shm_ward[i].start_unit = 20 + (i - 8);
        }
    }
    printf("[ADMISSIONS] Shared memory initialized. Ward: 4 ICU | 4 ISOLATION | 12 GENERAL\n");

    /* Signal handlers */
    struct sigaction sa_chld;
    memset(&sa_chld, 0, sizeof(sa_chld));
    sa_chld.sa_handler = sigchld_handler;
    sa_chld.sa_flags   = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa_chld, NULL);

    struct sigaction sa_term;
    memset(&sa_term, 0, sizeof(sa_term));
    sa_term.sa_handler = sigterm_handler;
    sigaction(SIGTERM, &sa_term, NULL);

    /* Open discharge FIFO (non-blocking read) */
    int fd = open_discharge_fifo_read();

    /* ── Main event loop ──────────────────────────────────────────────── */
    while (running == 1) {
        if (fd != -1) {
            char    buf[32];
            ssize_t n;

            while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
                buf[n] = '\0';
                buf[strcspn(buf, "\n\r ")] = '\0';

                int discharged_id = atoi(buf);
                if (discharged_id <= 0) continue;

                for (int i = 0; i < MAX_BEDS; i++) {
                    if (!shm_ward[i].is_free &&
                        shm_ward[i].patient_id == discharged_id) {
                        shm_ward[i].is_free    = 1;
                        shm_ward[i].patient_id = -1;
                        printf("[ADMISSIONS] Bed %d freed (patient %d discharged).\n",
                               i, discharged_id);
                        /* Immediately try to admit highest-priority waiting patient */
                        try_admit_from_queue();
                        break;
                    }
                }
            }
        } else {
            /* FIFO not open yet — retry */
            fd = open_discharge_fifo_read();
        }

        sleep(1);
    }

    /* ── Graceful shutdown ────────────────────────────────────────────── */
    if (fd != -1) close(fd);
    detach_shared_memory(shm_ward);

    /* Print scheduling simulation report before exiting */
    run_scheduling_simulation();

    printf("[ADMISSIONS] Shutting down.\n");
    return 0;
}
