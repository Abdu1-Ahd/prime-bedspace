/*
 * ============================================================
 * Project : Prime BedSpace - Hospital Patient Triage & Bed Allocator
 * File    : admissions.c
 * Group   : Group 14
 * Members : Abdul Ahad (24F-0727), Abdul Rahim (24F-0514)
 * Date    : 2026-05-12
 * Purpose : Central process manager — handles fork/exec, SIGCHLD, shared memory, IPC, scheduling triggers, and bed admission logic.
 * Compile : gcc -Wall -Wextra -pthread -Iinclude <file> -lrt -lpthread
 * ============================================================
 */
#include "types.h"
#include "ipc.h"
#include "scheduler.h"
#include "bed_allocator.h"
#include "terminal_ui.h"
#include "debug_log.h"
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
#include <sys/stat.h>   
#include <poll.h>

static pthread_mutex_t bed_mutex   = PTHREAD_MUTEX_INITIALIZER;

static pthread_cond_t  bed_freed   = PTHREAD_COND_INITIALIZER;

pthread_mutex_t g_queue_mutex       = PTHREAD_MUTEX_INITIALIZER;

static pthread_cond_t  patient_available = PTHREAD_COND_INITIALIZER;

static sem_t sem_icu;        
static sem_t sem_isolation;  

static sem_t sem_queue;      

PriorityQueue g_wait_queue;   
static BedPartition *shm_ward   = NULL;
static volatile sig_atomic_t running = 1;

static pid_t child_pids[50];
static int   child_count = 0;
static pthread_mutex_t child_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t discharge_mutex = PTHREAD_MUTEX_INITIALIZER;

static int g_discharge_fd = -1;

#define LOST_IDS_MAX 3
static int  lost_ids[LOST_IDS_MAX];
static int  lost_ids_count = 0;

static BedAllocator g_allocator;

static PatientRecord *mmap_records = NULL;
static pthread_mutex_t mmap_mutex  = PTHREAD_MUTEX_INITIALIZER;

static const char *g_strategy_name = "best";

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
    
    pthread_cond_broadcast(&patient_available);
    pthread_cond_broadcast(&bed_freed);

    dbg_write_ndjson("pre", "H5", "admissions.c:sigterm_handler", "sigterm_received",
                     "{\"running\":0}");
}

static const char *bed_type_for(int care_units) {
    if (care_units >= 3) return "ICU";
    if (care_units == 2) return "ISOLATION";
    return "GENERAL";
}

static void do_admit_to_bed(PatientRecord *p, int bed_id) {
    pthread_mutex_lock(&child_mutex);
    if (child_count >= 50) {
        fprintf(stderr, "[ADMISSIONS] child_pids table full — cannot fork.\n");
        pthread_mutex_unlock(&child_mutex);
        
        pthread_mutex_lock(&bed_mutex);
        shm_ward[bed_id].is_free    = 1;
        shm_ward[bed_id].patient_id = -1;
        pthread_cond_broadcast(&bed_freed);
        pthread_mutex_unlock(&bed_mutex);
        return;
    }
    pthread_mutex_unlock(&child_mutex);

    
    char bed_type_snap[16];
    strncpy(bed_type_snap, shm_ward[bed_id].bed_type, sizeof(bed_type_snap) - 1);
    bed_type_snap[sizeof(bed_type_snap) - 1] = '\0';

    time_t admit_time = time(NULL);

    
    pid_t pid = fork();

    if (pid < 0) {
        perror("[ADMISSIONS] fork failed");
        
        pthread_mutex_lock(&bed_mutex);
        shm_ward[bed_id].is_free    = 1;
        shm_ward[bed_id].patient_id = -1;
        pthread_cond_broadcast(&bed_freed);
        pthread_mutex_unlock(&bed_mutex);
        return;
    }

    if (pid == 0) {
        
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

    
    pthread_mutex_lock(&child_mutex);
    child_pids[child_count++] = pid;
    pthread_mutex_unlock(&child_mutex);

    printf("[ADMISSIONS] Patient %d admitted to %s bed %d (priority %d)\n",
           p->patient_id, bed_type_snap, bed_id, p->priority);

    log_admission_event(p->patient_id, p->priority,
                        p->arrival_time, admit_time, p->care_units);
}

static void *thread_receptionist(void *arg) {
    (void)arg;
    printf("[RECEPTIONIST] Thread started. Waiting for triage FIFO: %s\n",
           FIFO_TRIAGE_PATH);

    dbg_write_ndjson("pre", "H5", "admissions.c:thread_receptionist", "start", "{}");

    int fd = -1;
    
    while (fd == -1 && running) {
        fd = open_triage_fifo_read(); 
        if (fd == -1) sleep(1);
    }

    char buf[256];

    while (running) {
        struct pollfd pfd;
        pfd.fd     = fd;
        pfd.events = POLLIN;

        int pr = poll(&pfd, 1, 500); 
        if (pr == 0) continue;       
        if (pr < 0) {
            if (errno == EINTR) continue;
            perror("[RECEPTIONIST] poll triage FIFO");
            continue;
        }

        if (!(pfd.revents & POLLIN)) continue;

        ssize_t n = read(fd, buf, sizeof(buf) - 1);

        if (n <= 0) {
            if (!running) break;
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;

            
            if (fd != -1) close(fd);
            fd = -1;
            while (fd == -1 && running) {
                fd = open_triage_fifo_read();
                if (fd == -1) sleep(1);
            }
            if (!running) break;
            continue;
        }

        buf[n] = '\0';

        DBG2("pre", "H4", "admissions.c:thread_receptionist:read", "triage_fifo_read",
             "n", (long long)n, "has_nl", (long long)(strchr(buf, '\n') != NULL));

        
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

        
        sem_wait(&sem_queue);

        pthread_mutex_lock(&g_queue_mutex);
        if (pq_push(&g_wait_queue, p) == 0) {
            int depth = pq_size(&g_wait_queue);
            printf("[RECEPTIONIST] Patient %d queued (priority %d, depth %d)\n",
                   p.patient_id, p.priority, depth);
            pthread_cond_signal(&patient_available);

            DBG2("pre", "H4", "admissions.c:thread_receptionist:enqueue", "triage_parsed",
                 "patient_id", p.patient_id, "priority", p.priority);
        } else {
            
            fprintf(stderr,
                    "[RECEPTIONIST] PQ full — patient %d dropped.\n",
                    p.patient_id);
            sem_post(&sem_queue);
        }
        pthread_mutex_unlock(&g_queue_mutex);
    }

    if (fd != -1) close(fd);
    printf("[RECEPTIONIST] Thread exiting.\n");

    dbg_write_ndjson("pre", "H5", "admissions.c:thread_receptionist", "exit", "{}");
    return NULL;
}

static void *thread_scheduler(void *arg) {
    (void)arg;
    printf("[SCHEDULER] Thread started.\n");

    while (running) {
        
        pthread_mutex_lock(&g_queue_mutex);
        while (pq_is_empty(&g_wait_queue) && running) {
            pthread_cond_wait(&patient_available, &g_queue_mutex);
        }

        if (!running) {
            pthread_mutex_unlock(&g_queue_mutex);
            break;
        }

        PatientRecord p = pq_pop(&g_wait_queue);
        int depth       = pq_size(&g_wait_queue);
        pthread_mutex_unlock(&g_queue_mutex);

        
        sem_post(&sem_queue);

        printf("[SCHEDULER] Dequeued patient %d (priority %d). "
               "Waiting patients: %d\n",
               p.patient_id, p.priority, depth);

        
        if (p.care_units >= 3) {
            
            printf("[SCHEDULER] Acquiring ICU semaphore for patient %d...\n",
                   p.patient_id);
            sem_wait(&sem_icu);
        } else if (p.care_units == 2) {
            
            printf("[SCHEDULER] Acquiring ISOLATION semaphore for patient %d...\n",
                   p.patient_id);
            sem_wait(&sem_isolation);
        }
        

        
        pthread_mutex_lock(&bed_mutex);

        const char *req_type = bed_type_for(p.care_units);
        int bed_id = ba_alloc(&g_allocator, p.care_units, req_type, p.patient_id);

        
        while (bed_id == -1 && running) {
            printf("[SCHEDULER] No bed for patient %d — waiting on bed_freed.\n",
                   p.patient_id);

            DBG2("pre", "H5", "admissions.c:thread_scheduler:wait_bed", "no_bed_wait",
                 "patient_id", p.patient_id, "care_units", p.care_units);

            pthread_cond_wait(&bed_freed, &bed_mutex);
            bed_id = ba_alloc(&g_allocator, p.care_units, req_type, p.patient_id);
        }

        if (!running) {
            pthread_mutex_unlock(&bed_mutex);
            if (p.care_units >= 3)      sem_post(&sem_icu);
            else if (p.care_units == 2) sem_post(&sem_isolation);
            break;
        }

        
        shm_ward[bed_id].patient_id = p.patient_id;
        pthread_mutex_unlock(&bed_mutex);

        DBG2("pre", "H2", "admissions.c:thread_scheduler:alloc", "allocated_bed",
             "patient_id", p.patient_id, "bed_id", bed_id);

        
        if (mmap_records) {
            int slot = p.patient_id % MAX_PATIENTS;
            pthread_mutex_lock(&mmap_mutex);
            mmap_records[slot] = p;
            pthread_mutex_unlock(&mmap_mutex);
        }

        do_admit_to_bed(&p, bed_id); 
    }

    printf("[SCHEDULER] Thread exiting.\n");
    return NULL;
}

static void *thread_nurse(void *arg) {
    NurseType type = (NurseType)(intptr_t)arg;

    const char *label;
    int range_start, range_end;

    switch (type) {
        case NURSE_ICU:
            label = "NURSE-ICU";
            range_start = 0;  range_end = 3;   
            break;
        case NURSE_ISOLATION:
            label = "NURSE-ISOLATION";
            range_start = 4;  range_end = 7;   
            break;
        default: 
            label = "NURSE-GENERAL";
            range_start = 8;  range_end = 19;  
            break;
    }

    printf("[%s] Thread started. Monitoring beds %d-%d.\n",
           label, range_start, range_end);

    char buf[64];

    while (running) {
        int discharged_id = 0;

        
        pthread_mutex_lock(&discharge_mutex);
        for (int i = 0; i < lost_ids_count; i++) {
            
            for (int b = range_start; b <= range_end; b++) {
                if (!shm_ward[b].is_free &&
                    shm_ward[b].patient_id == lost_ids[i]) {
                    discharged_id = lost_ids[i];
                    
                    for (int j = i; j < lost_ids_count - 1; j++)
                        lost_ids[j] = lost_ids[j + 1];
                    lost_ids_count--;
                    break;
                }
            }
            if (discharged_id > 0) break;
        }

        
        if (discharged_id == 0 && g_discharge_fd != -1) {
            ssize_t n = read(g_discharge_fd, buf, sizeof(buf) - 1);

            if (n > 0) {
                buf[n] = '\0';
                buf[strcspn(buf, "\n\r ")] = '\0';
                int id = atoi(buf);

                if (id > 0) {
                    
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
                        
                        lost_ids[lost_ids_count++] = id;
                        printf("[%s] Patient %d not in range — parked in "
                               "lost_ids[] (count=%d)\n",
                               label, id, lost_ids_count);

                        DBG2("pre", "H3", "admissions.c:thread_nurse:park", "park_discharge_id",
                             "patient_id", id, "lost_count", lost_ids_count);
                    } else {
                        fprintf(stderr, "[%s] lost_ids[] full — patient %d "
                                "discharge event dropped.\n", label, id);

                        DBG1("pre", "H3", "admissions.c:thread_nurse:drop", "drop_discharge_id",
                             "patient_id", id);
                    }
                }
            } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("[NURSE] read discharge FIFO");
            }
        }
        pthread_mutex_unlock(&discharge_mutex);

        if (discharged_id <= 0) {
            usleep(100000); 
            continue;
        }

        
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

        DBG2("pre", "H3", "admissions.c:thread_nurse:free", "free_bed",
             "patient_id", discharged_id, "bed_id", freed_bed);

        
        ba_free(&g_allocator, freed_bed);

        
        if (mmap_records) {
            int slot = discharged_id % MAX_PATIENTS;
            pthread_mutex_lock(&mmap_mutex);
            mmap_records[slot].arrival_time = time(NULL); 
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

int main(int argc, char *argv[]) {
    
    AllocStrategy chosen_strategy = STRATEGY_BEST;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--strategy") == 0) {
            if (i + 1 < argc) {
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
            } else {
                fprintf(stderr, "[ADMISSIONS] Missing value for --strategy — using 'best'.\n");
            }
            break;
        }
    }
    printf("[ADMISSIONS] Strategy: %s\n", g_strategy_name);

    
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

    
    ba_init(&g_allocator, shm_ward, MAX_BEDS, chosen_strategy);

    
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

    
    if (sem_init(&sem_icu,       0, ICU_CAPACITY)   != 0 ||
        sem_init(&sem_isolation, 0, ISOLATION_CAPACITY) != 0 ||
        sem_init(&sem_queue,     0, MAX_WAIT_QUEUE)  != 0) {
        perror("[ADMISSIONS] sem_init failed");
        exit(1);
    }

    
    pq_init(&g_wait_queue);

    
    struct sigaction sa_chld;
    memset(&sa_chld, 0, sizeof(sa_chld));
    sa_chld.sa_handler = sigchld_handler;
    sa_chld.sa_flags   = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa_chld, NULL);

    struct sigaction sa_term;
    memset(&sa_term, 0, sizeof(sa_term));
    sa_term.sa_handler = sigterm_handler;
    sigaction(SIGTERM, &sa_term, NULL);

    
    
    if (mkfifo(FIFO_TRIAGE_PATH, 0666) == -1 && errno != EEXIST) {
        perror("[ADMISSIONS] mkfifo triage");
        exit(1);
    }
    if (mkfifo(FIFO_DISCHARGE_PATH, 0666) == -1 && errno != EEXIST) {
        perror("[ADMISSIONS] mkfifo discharge");
        exit(1);
    }
    printf("[ADMISSIONS] FIFOs ready.\n");

    
    ui_start(shm_ward, MAX_BEDS, g_strategy_name);

    
    
    g_discharge_fd = open(FIFO_DISCHARGE_PATH, O_RDWR | O_NONBLOCK);
    if (g_discharge_fd == -1) {
        perror("[ADMISSIONS] open discharge FIFO");
        exit(1);
    }
    printf("[ADMISSIONS] Discharge FIFO open (fd=%d).\n", g_discharge_fd);

    
    pthread_t t_receptionist, t_scheduler;
    pthread_t t_nurse_icu, t_nurse_isolation, t_nurse_general;

    pthread_create(&t_receptionist,   NULL, thread_receptionist, NULL);
    pthread_create(&t_scheduler,      NULL, thread_scheduler,    NULL);
    pthread_create(&t_nurse_icu,      NULL, thread_nurse, (void *)(intptr_t)NURSE_ICU);
    pthread_create(&t_nurse_isolation,NULL, thread_nurse, (void *)(intptr_t)NURSE_ISOLATION);
    pthread_create(&t_nurse_general,  NULL, thread_nurse, (void *)(intptr_t)NURSE_GENERAL);

    printf("[ADMISSIONS] 5 threads launched (1 receptionist, 1 scheduler, 3 nurses).\n");

    
    while (running) {
        pause(); 
    }

    printf("[ADMISSIONS] Shutdown signal received. Joining threads...\n");

    
    ui_stop();

    
    pthread_mutex_lock(&g_queue_mutex);
    pthread_cond_broadcast(&patient_available);
    pthread_mutex_unlock(&g_queue_mutex);

    pthread_mutex_lock(&bed_mutex);
    pthread_cond_broadcast(&bed_freed);
    pthread_mutex_unlock(&bed_mutex);

    
    sem_post(&sem_queue);

    dbg_write_ndjson("pre", "H5", "admissions.c:main", "join_begin", "{}");

    dbg_write_ndjson("pre", "H5", "admissions.c:main", "join_receptionist_begin", "{}");
    pthread_join(t_receptionist,    NULL);
    dbg_write_ndjson("pre", "H5", "admissions.c:main", "join_receptionist_done", "{}");

    dbg_write_ndjson("pre", "H5", "admissions.c:main", "join_scheduler_begin", "{}");
    pthread_join(t_scheduler,       NULL);
    dbg_write_ndjson("pre", "H5", "admissions.c:main", "join_scheduler_done", "{}");

    dbg_write_ndjson("pre", "H5", "admissions.c:main", "join_nurse_icu_begin", "{}");
    pthread_join(t_nurse_icu,       NULL);
    dbg_write_ndjson("pre", "H5", "admissions.c:main", "join_nurse_icu_done", "{}");

    dbg_write_ndjson("pre", "H5", "admissions.c:main", "join_nurse_isolation_begin", "{}");
    pthread_join(t_nurse_isolation, NULL);
    dbg_write_ndjson("pre", "H5", "admissions.c:main", "join_nurse_isolation_done", "{}");

    dbg_write_ndjson("pre", "H5", "admissions.c:main", "join_nurse_general_begin", "{}");
    pthread_join(t_nurse_general,   NULL);
    dbg_write_ndjson("pre", "H5", "admissions.c:main", "join_nurse_general_done", "{}");

    
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

    
    if (mmap_records) {
        size_t pr_size = MAX_PATIENTS * sizeof(PatientRecord);
        msync(mmap_records, pr_size, MS_SYNC);
        munmap(mmap_records, pr_size);
        mmap_records = NULL;
        printf("[ADMISSIONS] patient_records.dat flushed and unmapped.\n");
    }

    detach_shared_memory(shm_ward);

    
    run_scheduling_simulation();

    printf("[ADMISSIONS] Shutdown complete.\n");
    return 0;
}
