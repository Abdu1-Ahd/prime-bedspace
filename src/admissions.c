/**
 * ==============================================================================
 * Project: Prime BedSpace
 * File: admissions.c
 * Group: Zawiar & Subhani
 * Members: Abdul Ahad Zawiar (Abdu1-Ahd), AbdulRahim Subhani (abdulrahim-subh)
 * Date: 2026-05-08
 * Purpose: Phase 3+4 — Multi-threaded hospital admissions manager.
 *          Thread architecture:
 *            - Receptionist Thread: reads triage FIFO (blocking) → pushes
 *              PatientRecord onto priority queue, signals patient_available.
 *              Blocked by sem_queue (bounded, MAX_WAIT_QUEUE=20) when full.
 *            - Scheduler Thread: waits on patient_available → ba_alloc() under
 *              bed_mutex → waits on bed_freed if no bed free → fork/exec.
 *            - Nurse Thread Pool (3 threads, one per bed type):
 *              reads discharge FIFO (shared fd + discharge_mutex) → ba_free()
 *              → sem_post → broadcast bed_freed.
 * Phase 4 adds: BedAllocator (best/first/worst), paging simulation,
 *              mmap patient record log, ANSI terminal UI.
 * Compile: make all
 * Usage: ./build/admissions [--strategy best|first|worst]
 * ==============================================================================
 */

#include "types.h"
#include "ipc.h"
#include "scheduler.h"
#include "bed_allocator.h"
#include "terminal_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/mman.h>
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

/* Protects g_wait_queue (PriorityQueue).
 * NOTE: NOT static — terminal_ui.c links to this symbol for queue depth. */
pthread_mutex_t queue_mutex       = PTHREAD_MUTEX_INITIALIZER;
/* Signalled by receptionist when a patient is pushed onto the queue */
static pthread_cond_t  patient_available = PTHREAD_COND_INITIALIZER;

/* Counting semaphores — limit concurrent admissions per ward type */
static sem_t sem_icu;        /* max ICU_CAPACITY = 4       */
static sem_t sem_isolation;  /* max ISOLATION_CAPACITY = 4 */

/* Bounded semaphore — producer (receptionist) blocks when 20 waiting */
static sem_t sem_queue;      /* max MAX_WAIT_QUEUE = 20    */

/* ── Globals ─────────────────────────────────────────────────────────── */

PriorityQueue g_wait_queue;   /* NOT static — linked by terminal_ui.c */
static BedPartition *shm_ward   = NULL;
static volatile sig_atomic_t running = 1;

/* Child process registry for SIGCHLD reaping */
static pid_t child_pids[50];
static int   child_count = 0;
static pthread_mutex_t child_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * Lock ordering (always acquire in this order to prevent deadlock):
 *   1. queue_mutex       — protects g_wait_queue (priority queue)
 *   2. discharge_mutex   — protects shared discharge FIFO reads
 *   3. bed_mutex         — protects shm_ward[] bed bitmap
 * sem_icu / sem_isolation are acquired BEFORE bed_mutex in scheduler thread.
 * sem_queue is posted/waited outside all mutexes.
 * Never hold bed_mutex when calling fork().
 */
static pthread_mutex_t discharge_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Shared discharge fd — opened once in main(), read by all nurse threads
 * under discharge_mutex. If a patient_id does not belong to a nurse's range,
 * it is stored in lost_ids[] for another nurse to reclaim on its next pass. */
static int g_discharge_fd = -1;

/* Round-robin fallback buffer: up to 3 pending IDs not yet claimed by a nurse */
#define LOST_IDS_MAX 3
static int  lost_ids[LOST_IDS_MAX];
static int  lost_ids_count = 0;

/* ── Phase 4 globals ─────────────────────────────────────────────────── */

/* Bed allocator — wraps shm_ward[] with free-list + strategy selection */
static BedAllocator g_allocator;

/* mmap patient record log (MAX_PATIENTS slots, msync'd on shutdown) */
static PatientRecord *mmap_records = NULL;
static pthread_mutex_t mmap_mutex  = PTHREAD_MUTEX_INITIALIZER;

/* Active strategy name (set from --strategy argv, displayed in terminal UI) */
static const char *g_strategy_name = "best";

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

/* Determine bed_type string from care_units.
 * Matches the same logic used during ward initialisation. */
static const char *bed_type_for(int care_units) {
    if (care_units >= 3) return "ICU";
    if (care_units == 2) return "ISOLATION";
    return "GENERAL";
}

/* Fork and exec patient_simulator for patient p in bed_id.
 *
 * PRECONDITION: bed_mutex must NOT be held by the caller.
 * The caller (thread_scheduler) marks shm_ward[bed_id] as OCCUPIED and
 * unlocks bed_mutex BEFORE calling this function, ensuring fork() is
 * never called with any mutex held (prevents child inheriting locked state).
 *
 * On fork failure: re-acquires bed_mutex, reverts bed to free, broadcasts
 * bed_freed so the scheduler thread can retry with the next patient.
 */
static void do_admit_to_bed(PatientRecord *p, int bed_id) {
    pthread_mutex_lock(&child_mutex);
    if (child_count >= 50) {
        fprintf(stderr, "[ADMISSIONS] child_pids table full — cannot fork.\n");
        pthread_mutex_unlock(&child_mutex);
        /* Revert: re-acquire bed_mutex, reset bed, broadcast */
        pthread_mutex_lock(&bed_mutex);
        shm_ward[bed_id].is_free    = 1;
        shm_ward[bed_id].patient_id = -1;
        pthread_cond_broadcast(&bed_freed);
        pthread_mutex_unlock(&bed_mutex);
        return;
    }
    pthread_mutex_unlock(&child_mutex);

    /* Snapshot bed_type before fork (read-only after marking; safe without lock) */
    char bed_type_snap[16];
    strncpy(bed_type_snap, shm_ward[bed_id].bed_type, sizeof(bed_type_snap) - 1);
    bed_type_snap[sizeof(bed_type_snap) - 1] = '\0';

    time_t admit_time = time(NULL);

    /* ── fork() — NO mutex held ────────────────────────────────────── */
    pid_t pid = fork();

    if (pid < 0) {
        perror("[ADMISSIONS] fork failed");
        /* Revert bed under bed_mutex, wake scheduler */
        pthread_mutex_lock(&bed_mutex);
        shm_ward[bed_id].is_free    = 1;
        shm_ward[bed_id].patient_id = -1;
        pthread_cond_broadcast(&bed_freed);
        pthread_mutex_unlock(&bed_mutex);
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
            bed_type_snap,
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
           p->patient_id, bed_type_snap, bed_id, p->priority);

    log_admission_event(p->patient_id, p->priority,
                        p->arrival_time, admit_time, p->care_units);
}

/* ── Thread: Receptionist ────────────────────────────────────────────── */

static void *thread_receptionist(void *arg) {
    (void)arg;
    printf("[RECEPTIONIST] Thread started. Waiting for triage FIFO: %s\n",
           FIFO_TRIAGE_PATH);

    int fd = -1;
    /* Phase 1: spin with non-blocking open until the FIFO exists */
    while (fd == -1 && running) {
        fd = open_triage_fifo_read();   /* O_NONBLOCK — returns -1 if not ready */
        if (fd == -1) sleep(1);
    }
    close(fd); /* discard the non-blocking fd */
    fd = -1;

    /* Phase 2: reopen with blocking O_RDONLY — read() will block until data */
    while (fd == -1 && running) {
        fd = open_triage_fifo_read_block();
        if (fd == -1) sleep(1);
    }

    char buf[256];

    while (running) {
        /* Blocking read — no busy-spin, no usleep, no EAGAIN */
        ssize_t n = read(fd, buf, sizeof(buf) - 1);

        if (n <= 0) {
            /* n == 0: writer closed the FIFO (EOF). Reopen blocking fd. */
            close(fd);
            fd = -1;
            while (fd == -1 && running) {
                fd = open_triage_fifo_read_block();
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
            /* PQ heap full (>100) — return the semaphore slot */
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

        /* ── ba_alloc() under bed_mutex (replaces inline Best-Fit) ──── */
        pthread_mutex_lock(&bed_mutex);

        const char *req_type = bed_type_for(p.care_units);
        int bed_id = ba_alloc(&g_allocator, p.care_units, req_type, p.patient_id);

        /* If no bed available, wait for a nurse to broadcast bed_freed */
        while (bed_id == -1 && running) {
            printf("[SCHEDULER] No bed for patient %d — waiting on bed_freed.\n",
                   p.patient_id);
            pthread_cond_wait(&bed_freed, &bed_mutex);
            bed_id = ba_alloc(&g_allocator, p.care_units, req_type, p.patient_id);
        }

        if (!running) {
            pthread_mutex_unlock(&bed_mutex);
            if (p.care_units >= 3)      sem_post(&sem_icu);
            else if (p.care_units == 2) sem_post(&sem_isolation);
            break;
        }

        /* ba_alloc() already set is_free=0. Set patient_id then unlock. */
        shm_ward[bed_id].patient_id = p.patient_id;
        pthread_mutex_unlock(&bed_mutex);

        /* ── mmap record write (Task 5) ──────────────────────────────── */
        if (mmap_records) {
            int slot = p.patient_id % MAX_PATIENTS;
            pthread_mutex_lock(&mmap_mutex);
            mmap_records[slot] = p;
            pthread_mutex_unlock(&mmap_mutex);
        }

        do_admit_to_bed(&p, bed_id); /* fork() — no mutex held */
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

    char buf[64];

    while (running) {
        int discharged_id = 0;

        /* ── Step 1: check lost_ids[] buffer first (discharge_mutex held) ─
         * lost_ids[] holds patient IDs read by another nurse that didn't match
         * that nurse's bed range. This nurse checks if any belong to its range.
         * This is a simple round-robin fallback, not a full routing layer.     */
        pthread_mutex_lock(&discharge_mutex);
        for (int i = 0; i < lost_ids_count; i++) {
            /* Peek at shm_ward WITHOUT bed_mutex here — only reading patient_id
             * to see if it's in our range. bed_mutex is acquired below for
             * the actual free operation (lock order: discharge → bed).         */
            for (int b = range_start; b <= range_end; b++) {
                if (!shm_ward[b].is_free &&
                    shm_ward[b].patient_id == lost_ids[i]) {
                    discharged_id = lost_ids[i];
                    /* Remove from lost_ids[] by compacting */
                    for (int j = i; j < lost_ids_count - 1; j++)
                        lost_ids[j] = lost_ids[j + 1];
                    lost_ids_count--;
                    break;
                }
            }
            if (discharged_id > 0) break;
        }

        /* ── Step 2: if no pending lost id, read a new one from the shared fd */
        if (discharged_id == 0 && g_discharge_fd != -1) {
            ssize_t n = read(g_discharge_fd, buf, sizeof(buf) - 1);

            if (n > 0) {
                buf[n] = '\0';
                buf[strcspn(buf, "\n\r ")] = '\0';
                int id = atoi(buf);

                if (id > 0) {
                    /* Check if this id belongs to our bed range */
                    int mine = 0;
                    for (int b = range_start; b <= range_end; b++) {
                        if (!shm_ward[b].is_free &&
                            shm_ward[b].patient_id == id) {
                            mine = 1;
                            break;
                        }
                    }

                    if (mine) {
                        discharged_id = id;
                    } else if (lost_ids_count < LOST_IDS_MAX) {
                        /* Not ours — park in lost_ids[] for another nurse */
                        lost_ids[lost_ids_count++] = id;
                        printf("[%s] Patient %d not in range — parked in "
                               "lost_ids[] (count=%d)\n",
                               label, id, lost_ids_count);
                    } else {
                        fprintf(stderr, "[%s] lost_ids[] full — patient %d "
                                "discharge event dropped.\n", label, id);
                    }
                }
            } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("[NURSE] read discharge FIFO");
            }
        }
        pthread_mutex_unlock(&discharge_mutex);

        if (discharged_id <= 0) {
            usleep(100000); /* 100ms — no event this iteration */
            continue;
        }

        /* ── Step 3: free the bed via ba_free() (lock order: discharge→bed) */
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
            pthread_mutex_unlock(&bed_mutex);
            continue;
        }

        printf("[%s] Discharging patient %d from bed %d.\n",
               label, discharged_id, freed_bed);

        /* ba_free() handles is_free=1, coalescing, ward map, frag report */
        ba_free(&g_allocator, freed_bed);

        /* ── mmap discharge timestamp update (Task 5) ─────────────── */
        if (mmap_records) {
            int slot = discharged_id % MAX_PATIENTS;
            pthread_mutex_lock(&mmap_mutex);
            mmap_records[slot].arrival_time = time(NULL); /* discharge ts proxy */
            pthread_mutex_unlock(&mmap_mutex);
        }

        if (type == NURSE_ICU)            sem_post(&sem_icu);
        else if (type == NURSE_ISOLATION) sem_post(&sem_isolation);

        pthread_cond_broadcast(&bed_freed);
        pthread_mutex_unlock(&bed_mutex);
    }

    printf("[%s] Thread exiting.\n", label);
    return NULL;
}

/* ── main ────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    /* ── Parse --strategy best|first|worst ───────────────────────────── */
    AllocStrategy chosen_strategy = STRATEGY_BEST;
    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--strategy") == 0) {
            if      (strcmp(argv[i + 1], "first") == 0) {
                chosen_strategy = STRATEGY_FIRST;
                g_strategy_name = "first";
            } else if (strcmp(argv[i + 1], "worst") == 0) {
                chosen_strategy = STRATEGY_WORST;
                g_strategy_name = "worst";
            } else if (strcmp(argv[i + 1], "best") == 0) {
                chosen_strategy = STRATEGY_BEST;
                g_strategy_name = "best";
            } else {
                fprintf(stderr, "[ADMISSIONS] Unknown strategy '%s' — using 'best'.\n",
                        argv[i + 1]);
            }
            break;
        }
    }
    printf("[ADMISSIONS] Strategy: %s\n", g_strategy_name);

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

    /* ── BedAllocator init (Phase 4) ─────────────────────────────────── */
    ba_init(&g_allocator, shm_ward, MAX_BEDS, chosen_strategy);

    /* ── mmap patient record log (Phase 4, Task 5) ───────────────────── */
    {
        int pr_fd = open("patient_records.dat", O_RDWR | O_CREAT, 0644);
        if (pr_fd == -1) {
            perror("[ADMISSIONS] open patient_records.dat");
        } else {
            size_t pr_size = MAX_PATIENTS * sizeof(PatientRecord);
            if (ftruncate(pr_fd, (off_t)pr_size) == -1) {
                perror("[ADMISSIONS] ftruncate patient_records.dat");
            } else {
                void *ptr = mmap(NULL, pr_size,
                                 PROT_READ | PROT_WRITE, MAP_SHARED,
                                 pr_fd, 0);
                if (ptr == MAP_FAILED) {
                    perror("[ADMISSIONS] mmap patient_records.dat");
                } else {
                    mmap_records = (PatientRecord *)ptr;
                    printf("[ADMISSIONS] patient_records.dat mmap'd (%zu bytes).\n",
                           pr_size);
                }
            }
            close(pr_fd);
        }
    }

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

    /* ── Open shared discharge FIFO once (Fix 3: single fd, no race) ── */
    /* Retry until patient_simulator has created the FIFO via the script */
    while (g_discharge_fd == -1) {
        g_discharge_fd = open_discharge_fifo_read();
        if (g_discharge_fd == -1) sleep(1);
    }
    printf("[ADMISSIONS] Discharge FIFO open (fd=%d).\n", g_discharge_fd);

    /* ── Thread launch ───────────────────────────────────────────────── */
    pthread_t t_receptionist, t_scheduler;
    pthread_t t_nurse_icu, t_nurse_isolation, t_nurse_general;

    pthread_create(&t_receptionist,   NULL, thread_receptionist, NULL);
    pthread_create(&t_scheduler,      NULL, thread_scheduler,    NULL);
    pthread_create(&t_nurse_icu,      NULL, thread_nurse, (void *)(intptr_t)NURSE_ICU);
    pthread_create(&t_nurse_isolation,NULL, thread_nurse, (void *)(intptr_t)NURSE_ISOLATION);
    pthread_create(&t_nurse_general,  NULL, thread_nurse, (void *)(intptr_t)NURSE_GENERAL);

    printf("[ADMISSIONS] 5 threads launched (1 receptionist, 1 scheduler, 3 nurses).\n");

    /* ── Terminal UI (Phase 4, Task 6) ─────────────────────────── */
    ui_start(shm_ward, MAX_BEDS, g_strategy_name);

    /* ── Main thread: wait for SIGTERM ──────────────────────────────── */
    while (running) {
        pause(); /* sleep until any signal */
    }

    printf("[ADMISSIONS] Shutdown signal received. Joining threads...\n");

    /* Stop terminal UI before joining threads */
    ui_stop();

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
    pthread_mutex_destroy(&discharge_mutex);
    pthread_mutex_destroy(&mmap_mutex);

    if (g_discharge_fd != -1) close(g_discharge_fd);

    /* ── msync + munmap patient_records.dat ──────────────────────── */
    if (mmap_records) {
        size_t pr_size = MAX_PATIENTS * sizeof(PatientRecord);
        msync(mmap_records, pr_size, MS_SYNC);
        munmap(mmap_records, pr_size);
        mmap_records = NULL;
        printf("[ADMISSIONS] patient_records.dat flushed and unmapped.\n");
    }

    detach_shared_memory(shm_ward);

    /* ── Scheduling simulation report ──────────────────────────────── */
    run_scheduling_simulation();

    printf("[ADMISSIONS] Shutdown complete.\n");
    return 0;
}
