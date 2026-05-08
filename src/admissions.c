/**
 * ==============================================================================
 * Project: Prime BedSpace
 * File: admissions.c
 * Group: Zawiar & Subhani
 * Members: Abdul Ahad Zawiar (Abdu1-Ahd), AbdulRahim Subhani (abdulrahim-subh)
 * Date: 2026-05-08
 * Purpose: Phase 3 — Multi-threaded hospital admissions manager.
 *          Thread architecture:
 *            - Receptionist Thread: reads triage FIFO → pushes PatientRecord
 *              onto priority queue, signals patient_available condvar.
 *              Blocked by sem_queue (bounded, MAX_WAIT_QUEUE=20) when full.
 *            - Scheduler Thread: waits on patient_available → Best-Fit bed
 *              search under bed_mutex → waits on bed_freed if no bed free →
 *              acquires sem_icu/sem_isolation → do_admit_to_bed().
 *            - Nurse Thread Pool (3 threads, one per bed type):
 *              reads discharge FIFO → frees bed + coalescing → sem_post →
 *              broadcasts bed_freed condvar.
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
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>

/* ── Synchronization primitives ─────────────────────────────────────── */

/* Protects all reads/writes to shm_ward[] */
static pthread_mutex_t bed_mutex   = PTHREAD_MUTEX_INITIALIZER;
/* Broadcast by nurse threads when a bed is freed */
static pthread_cond_t  bed_freed   = PTHREAD_COND_INITIALIZER;

/* Protects g_wait_queue (PriorityQueue) */
static pthread_mutex_t queue_mutex       = PTHREAD_MUTEX_INITIALIZER;
/* Signalled by receptionist when a patient is pushed onto the queue */
static pthread_cond_t  patient_available = PTHREAD_COND_INITIALIZER;

/* Counting semaphores — limit concurrent admissions per ward type */
static sem_t sem_icu;        /* max ICU_CAPACITY = 4       */
static sem_t sem_isolation;  /* max ISOLATION_CAPACITY = 4 */

/* Bounded semaphore — producer (receptionist) blocks when 20 waiting */
static sem_t sem_queue;      /* max MAX_WAIT_QUEUE = 20    */

/* ── Globals ─────────────────────────────────────────────────────────── */

static PriorityQueue g_wait_queue;
static BedPartition *shm_ward   = NULL;
static volatile sig_atomic_t running = 1;

/* Child process registry for SIGCHLD reaping */
static pid_t child_pids[50];
static int   child_count = 0;
static pthread_mutex_t child_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── Signal handlers ─────────────────────────────────────────────────── */

static void sigchld_handler(int sig) {
    (void)sig;
    pid_t pid;
    int   status;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        pthread_mutex_lock(&child_mutex);
        for (int i = 0; i < child_count; i++) {
            if (child_pids[i] == pid) {
                for (int j = i; j < child_count - 1; j++)
                    child_pids[j] = child_pids[j + 1];
                child_count--;
                break;
            }
        }
        pthread_mutex_unlock(&child_mutex);
    }
}

static void sigterm_handler(int sig) {
    (void)sig;
    running = 0;
    /* Wake all waiting threads so they can observe running=0 and exit */
    pthread_cond_broadcast(&patient_available);
    pthread_cond_broadcast(&bed_freed);
}

/* ── Internal helpers ────────────────────────────────────────────────── */

/* Best-Fit: smallest free bed whose size >= care_units.
   MUST be called with bed_mutex held. */
static int find_best_fit_bed(int care_units) {
    int best = -1;
    int best_size = MAX_BEDS * 3 + 1; /* larger than any possible size */

    for (int i = 0; i < MAX_BEDS; i++) {
        if (shm_ward[i].is_free && shm_ward[i].size >= care_units) {
            if (shm_ward[i].size < best_size) {
                best      = i;
                best_size = shm_ward[i].size;
            }
        }
    }
    return best;
}

/* Fork and exec patient_simulator. MUST be called with bed_mutex held.
   Releases bed_mutex briefly around fork to avoid deadlock in child. */
static void do_admit_to_bed(PatientRecord *p, int bed_id) {
    pthread_mutex_lock(&child_mutex);
    if (child_count >= 50) {
        fprintf(stderr, "[ADMISSIONS] child_pids table full — cannot fork.\n");
        pthread_mutex_unlock(&child_mutex);
        return;
    }
    pthread_mutex_unlock(&child_mutex);

    time_t admit_time = time(NULL);

    /* Mark bed occupied BEFORE fork (under bed_mutex already held by caller) */
    shm_ward[bed_id].is_free    = 0;
    shm_ward[bed_id].patient_id = p->patient_id;

    /* Release bed_mutex around fork to avoid child inheriting a locked mutex */
    pthread_mutex_unlock(&bed_mutex);

    pid_t pid = fork();

    if (pid < 0) {
        perror("[ADMISSIONS] fork failed");
        /* Re-acquire and revert bed state */
        pthread_mutex_lock(&bed_mutex);
        shm_ward[bed_id].is_free    = 1;
        shm_ward[bed_id].patient_id = -1;
        return;
    }

    if (pid == 0) {
        /* Child: exec patient_simulator */
        char patient_id_str[32], triage_str[32], bed_id_str[32];
        snprintf(patient_id_str, sizeof(patient_id_str), "%d", p->patient_id);
        snprintf(triage_str,     sizeof(triage_str),     "%d", p->priority);
        snprintf(bed_id_str,     sizeof(bed_id_str),     "%d", bed_id);

        char *args[] = {
            "./build/patient_simulator",
            patient_id_str,
            triage_str,
            bed_id_str,
            shm_ward[bed_id].bed_type,
            NULL
        };
        execv("./build/patient_simulator", args);
        perror("[ADMISSIONS] execv failed");
        _exit(1);
    }

    /* Parent */
    pthread_mutex_lock(&child_mutex);
    child_pids[child_count++] = pid;
    pthread_mutex_unlock(&child_mutex);

    printf("[ADMISSIONS] Patient %d admitted to %s bed %d (priority %d)\n",
           p->patient_id, shm_ward[bed_id].bed_type, bed_id, p->priority);

    /* Log admission event for scheduling simulation */
    log_admission_event(p->patient_id, p->priority,
                        p->arrival_time, admit_time, p->care_units);

    /* Re-acquire bed_mutex so caller's lock discipline is intact */
    pthread_mutex_lock(&bed_mutex);
}

/* ── Thread: Receptionist ────────────────────────────────────────────── */

static void *thread_receptionist(void *arg) {
    (void)arg;
    printf("[RECEPTIONIST] Thread started. Waiting for triage FIFO: %s\n",
           FIFO_TRIAGE_PATH);

    int fd = -1;
    /* Retry loop: FIFO may not exist yet until start_hospital.sh creates it */
    while (fd == -1 && running) {
        fd = open_triage_fifo_read();
        if (fd == -1) sleep(1);
    }

    char buf[256];

    while (running) {
        ssize_t n = read(fd, buf, sizeof(buf) - 1);

        if (n <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(50000); /* 50ms poll */
                continue;
            }
            /* FIFO closed — reopen */
            close(fd);
            fd = -1;
            while (fd == -1 && running) {
                fd = open_triage_fifo_read();
                if (fd == -1) sleep(1);
            }
            continue;
        }

        buf[n] = '\0';

        /* Parse pipe-delimited triage line:
         * patient_id|name|age|severity|priority|care_units|arrival_time */
        PatientRecord p;
        memset(&p, 0, sizeof(p));

        char *tok = strtok(buf, "|");
        if (!tok) continue;
        p.patient_id = atoi(tok);

        tok = strtok(NULL, "|");
        if (!tok) continue;
        strncpy(p.name, tok, sizeof(p.name) - 1);

        tok = strtok(NULL, "|");
        if (!tok) continue;
        p.age = atoi(tok);

        tok = strtok(NULL, "|");
        if (!tok) continue;
        p.severity = atoi(tok);

        tok = strtok(NULL, "|");
        if (!tok) continue;
        p.priority = atoi(tok);

        tok = strtok(NULL, "|");
        if (!tok) continue;
        p.care_units = atoi(tok);

        tok = strtok(NULL, "|");
        p.arrival_time = tok ? (time_t)atol(tok) : time(NULL);

        if (p.patient_id <= 0 || p.priority < 1 || p.priority > 5) continue;

        /* Bounded producer: block if wait queue is at capacity (20 patients) */
        sem_wait(&sem_queue);

        pthread_mutex_lock(&queue_mutex);
        if (pq_push(&g_wait_queue, p) == 0) {
            int depth = pq_size(&g_wait_queue);
            printf("[RECEPTIONIST] Patient %d queued (priority %d, depth %d)\n",
                   p.patient_id, p.priority, depth);
            pthread_cond_signal(&patient_available);
        } else {
            /* Queue struct full (>100) — return the semaphore slot */
            fprintf(stderr,
                    "[RECEPTIONIST] PQ full — patient %d dropped.\n",
                    p.patient_id);
            sem_post(&sem_queue);
        }
        pthread_mutex_unlock(&queue_mutex);
    }

    if (fd != -1) close(fd);
    printf("[RECEPTIONIST] Thread exiting.\n");
    return NULL;
}

/* ── Thread: Scheduler ──────────────────────────────────────────────── */

static void *thread_scheduler(void *arg) {
    (void)arg;
    printf("[SCHEDULER] Thread started.\n");

    while (running) {
        /* Wait for a patient to appear in the queue */
        pthread_mutex_lock(&queue_mutex);
        while (pq_is_empty(&g_wait_queue) && running) {
            pthread_cond_wait(&patient_available, &queue_mutex);
        }

        if (!running) {
            pthread_mutex_unlock(&queue_mutex);
            break;
        }

        PatientRecord p = pq_pop(&g_wait_queue);
        int depth       = pq_size(&g_wait_queue);
        pthread_mutex_unlock(&queue_mutex);

        /* Release one slot in the bounded semaphore (consumer side) */
        sem_post(&sem_queue);

        printf("[SCHEDULER] Dequeued patient %d (priority %d). "
               "Waiting patients: %d\n",
               p.patient_id, p.priority, depth);

        /* Acquire ward-type semaphore BEFORE locking bed_mutex to avoid
           inversion: semaphore controls capacity, mutex protects state. */
        if (p.care_units >= 3) {
            /* ICU patient — wait for an ICU slot */
            printf("[SCHEDULER] Acquiring ICU semaphore for patient %d...\n",
                   p.patient_id);
            sem_wait(&sem_icu);
        } else if (p.care_units == 2) {
            /* ISOLATION patient */
            printf("[SCHEDULER] Acquiring ISOLATION semaphore for patient %d...\n",
                   p.patient_id);
            sem_wait(&sem_isolation);
        }
        /* GENERAL patients have no capacity semaphore (12 beds, rarely saturated) */

        /* Find best-fit bed under bed_mutex */
        pthread_mutex_lock(&bed_mutex);

        int bed_id = find_best_fit_bed(p.care_units);

        /* If no bed is available, wait for nurse to broadcast bed_freed */
        while (bed_id == -1 && running) {
            printf("[SCHEDULER] No bed for patient %d — waiting on bed_freed.\n",
                   p.patient_id);
            pthread_cond_wait(&bed_freed, &bed_mutex);
            bed_id = find_best_fit_bed(p.care_units);
        }

        if (!running) {
            pthread_mutex_unlock(&bed_mutex);
            /* Return semaphore since we never admitted */
            if (p.care_units >= 3)     sem_post(&sem_icu);
            else if (p.care_units == 2) sem_post(&sem_isolation);
            break;
        }

        /* do_admit_to_bed releases and re-acquires bed_mutex around fork */
        do_admit_to_bed(&p, bed_id);
        pthread_mutex_unlock(&bed_mutex);
    }

    printf("[SCHEDULER] Thread exiting.\n");
    return NULL;
}

/* ── Thread: Nurse (parametrized by NurseType) ──────────────────────── */

static void *thread_nurse(void *arg) {
    NurseType type = (NurseType)(intptr_t)arg;

    const char *label;
    int range_start, range_end;

    switch (type) {
        case NURSE_ICU:
            label = "NURSE-ICU";
            range_start = 0;  range_end = 3;   /* beds 0-3   */
            break;
        case NURSE_ISOLATION:
            label = "NURSE-ISOLATION";
            range_start = 4;  range_end = 7;   /* beds 4-7   */
            break;
        default: /* NURSE_GENERAL */
            label = "NURSE-GENERAL";
            range_start = 8;  range_end = 19;  /* beds 8-19  */
            break;
    }

    printf("[%s] Thread started. Monitoring beds %d-%d.\n",
           label, range_start, range_end);

    int fd = -1;
    while (fd == -1 && running) {
        fd = open_discharge_fifo_read();
        if (fd == -1) sleep(1);
    }

    char buf[64];

    while (running) {
        ssize_t n = read(fd, buf, sizeof(buf) - 1);

        if (n <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(100000); /* 100ms poll */
                continue;
            }
            close(fd);
            fd = -1;
            while (fd == -1 && running) {
                fd = open_discharge_fifo_read();
                if (fd == -1) sleep(1);
            }
            continue;
        }

        buf[n] = '\0';
        buf[strcspn(buf, "\n\r ")] = '\0';

        int discharged_id = atoi(buf);
        if (discharged_id <= 0) continue;

        pthread_mutex_lock(&bed_mutex);

        int freed_bed = -1;
        for (int i = range_start; i <= range_end; i++) {
            if (!shm_ward[i].is_free &&
                shm_ward[i].patient_id == discharged_id) {
                freed_bed = i;
                break;
            }
        }

        if (freed_bed == -1) {
            /* This discharge belongs to a different nurse type's range */
            pthread_mutex_unlock(&bed_mutex);
            continue;
        }

        shm_ward[freed_bed].is_free    = 1;
        shm_ward[freed_bed].patient_id = -1;

        /* ── Left+Right coalescing ──────────────────────────────────── */
        /* Merge with left neighbour if same type and free */
        int coalesced = 0;
        if (freed_bed > range_start &&
            shm_ward[freed_bed - 1].is_free &&
            strcmp(shm_ward[freed_bed - 1].bed_type,
                   shm_ward[freed_bed].bed_type) == 0) {
            shm_ward[freed_bed - 1].size += shm_ward[freed_bed].size;
            shm_ward[freed_bed].size = 0; /* absorbed */
            coalesced = 1;
        }
        /* Merge with right neighbour if same type and free */
        if (freed_bed < range_end &&
            shm_ward[freed_bed + 1].is_free &&
            strcmp(shm_ward[freed_bed + 1].bed_type,
                   shm_ward[freed_bed].bed_type) == 0) {
            int merge_target = (coalesced) ? freed_bed - 1 : freed_bed;
            shm_ward[merge_target].size += shm_ward[freed_bed + 1].size;
            shm_ward[freed_bed + 1].size = 0; /* absorbed */
            coalesced = 1;
        }

        printf("[%s] Bed %d freed (patient %d discharged). "
               "Coalesced: %s. Broadcasting bed_freed.\n",
               label, freed_bed, discharged_id,
               coalesced ? "yes" : "no");

        /* Release ward-type semaphore slot */
        if (type == NURSE_ICU) {
            sem_post(&sem_icu);
        } else if (type == NURSE_ISOLATION) {
            sem_post(&sem_isolation);
        }

        /* Broadcast so scheduler thread re-checks for available beds */
        pthread_cond_broadcast(&bed_freed);
        pthread_mutex_unlock(&bed_mutex);
    }

    if (fd != -1) close(fd);
    printf("[%s] Thread exiting.\n", label);
    return NULL;
}

/* ── main ────────────────────────────────────────────────────────────── */

int main(void) {
    /* ── Shared memory ──────────────────────────────────────────────── */
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
    printf("[ADMISSIONS] Ward initialized: 4 ICU | 4 ISOLATION | 12 GENERAL\n");

    /* ── Semaphores ──────────────────────────────────────────────────── */
    if (sem_init(&sem_icu,       0, ICU_CAPACITY)   != 0 ||
        sem_init(&sem_isolation, 0, ISOLATION_CAPACITY) != 0 ||
        sem_init(&sem_queue,     0, MAX_WAIT_QUEUE)  != 0) {
        perror("[ADMISSIONS] sem_init failed");
        exit(1);
    }

    /* ── Priority queue ──────────────────────────────────────────────── */
    pq_init(&g_wait_queue);

    /* ── Signal handlers ─────────────────────────────────────────────── */
    struct sigaction sa_chld;
    memset(&sa_chld, 0, sizeof(sa_chld));
    sa_chld.sa_handler = sigchld_handler;
    sa_chld.sa_flags   = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa_chld, NULL);

    struct sigaction sa_term;
    memset(&sa_term, 0, sizeof(sa_term));
    sa_term.sa_handler = sigterm_handler;
    sigaction(SIGTERM, &sa_term, NULL);

    /* ── Thread launch ───────────────────────────────────────────────── */
    pthread_t t_receptionist, t_scheduler;
    pthread_t t_nurse_icu, t_nurse_isolation, t_nurse_general;

    pthread_create(&t_receptionist,   NULL, thread_receptionist, NULL);
    pthread_create(&t_scheduler,      NULL, thread_scheduler,    NULL);
    pthread_create(&t_nurse_icu,      NULL, thread_nurse, (void *)(intptr_t)NURSE_ICU);
    pthread_create(&t_nurse_isolation,NULL, thread_nurse, (void *)(intptr_t)NURSE_ISOLATION);
    pthread_create(&t_nurse_general,  NULL, thread_nurse, (void *)(intptr_t)NURSE_GENERAL);

    printf("[ADMISSIONS] 5 threads launched (1 receptionist, 1 scheduler, 3 nurses).\n");

    /* ── Main thread: wait for SIGTERM ──────────────────────────────── */
    while (running) {
        pause(); /* sleep until any signal */
    }

    printf("[ADMISSIONS] Shutdown signal received. Joining threads...\n");

    /* Wake threads that may be blocked in condvar waits */
    pthread_mutex_lock(&queue_mutex);
    pthread_cond_broadcast(&patient_available);
    pthread_mutex_unlock(&queue_mutex);

    pthread_mutex_lock(&bed_mutex);
    pthread_cond_broadcast(&bed_freed);
    pthread_mutex_unlock(&bed_mutex);

    /* Also unblock receptionist/nurse FIFO reads with a short post */
    sem_post(&sem_queue);

    pthread_join(t_receptionist,    NULL);
    pthread_join(t_scheduler,       NULL);
    pthread_join(t_nurse_icu,       NULL);
    pthread_join(t_nurse_isolation, NULL);
    pthread_join(t_nurse_general,   NULL);

    /* ── Cleanup ────────────────────────────────────────────────────── */
    sem_destroy(&sem_icu);
    sem_destroy(&sem_isolation);
    sem_destroy(&sem_queue);

    pthread_mutex_destroy(&bed_mutex);
    pthread_cond_destroy(&bed_freed);
    pthread_mutex_destroy(&queue_mutex);
    pthread_cond_destroy(&patient_available);
    pthread_mutex_destroy(&child_mutex);

    detach_shared_memory(shm_ward);

    /* ── Scheduling simulation report ──────────────────────────────── */
    run_scheduling_simulation();

    printf("[ADMISSIONS] Shutdown complete.\n");
    return 0;
}
