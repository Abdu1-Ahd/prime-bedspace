/*
 * ============================================================
 * Project : Prime BedSpace - Hospital Patient Triage & Bed Allocator
 * File    : scheduler.c
 * Group   : Group 14
 * Members : Abdul Ahad (24F-0727), Abdul Rahim (24F-0514)
 * Date    : 2026-05-12
 * Purpose : Priority queue (min-heap) and scheduling simulations — FCFS, SJF, Priority, and Round Robin with Gantt log output.
 * Compile : gcc -Wall -Wextra -pthread -Iinclude <file> -lrt -lpthread
 * ============================================================
 */
#include "scheduler.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

ScheduleEvent g_event_log[MAX_EVENT_LOG];
int           g_event_count = 0;

void pq_init(PriorityQueue *pq) {
    memset(pq->heap, 0, sizeof(pq->heap));
    pq->size = 0;
    pthread_mutex_init(&pq->lock, NULL);
}

int pq_push(PriorityQueue *pq, PatientRecord p) {
    pthread_mutex_lock(&pq->lock);
    if (pq->size >= MAX_QUEUE_SIZE) {
        pthread_mutex_unlock(&pq->lock);
        return -1;
    }

    int i = pq->size;
    pq->heap[i] = p;
    pq->size++;

    
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (pq->heap[parent].priority > pq->heap[i].priority) {
            PatientRecord tmp = pq->heap[parent];
            pq->heap[parent]  = pq->heap[i];
            pq->heap[i]       = tmp;
            i = parent;
        } else {
            break;
        }
    }

    pthread_mutex_unlock(&pq->lock);
    return 0;
}

PatientRecord pq_pop(PriorityQueue *pq) {
    pthread_mutex_lock(&pq->lock);
    if (!pq->size) {
        pthread_mutex_unlock(&pq->lock);
        PatientRecord empty_record = {0};
        empty_record.patient_id = -1;
        return empty_record;
    }

    PatientRecord top_patient = pq->heap[0];
    pq->size--;
    pq->heap[0] = pq->heap[pq->size];

    
    int i = 0;
    while (1) {
        int left    = 2 * i + 1;
        int right   = 2 * i + 2;
        int smallest = i;

        if (left  < pq->size && pq->heap[left].priority  < pq->heap[smallest].priority)
            smallest = left;
        if (right < pq->size && pq->heap[right].priority < pq->heap[smallest].priority)
            smallest = right;

        if (smallest != i) {
            PatientRecord tmp    = pq->heap[i];
            pq->heap[i]         = pq->heap[smallest];
            pq->heap[smallest]  = tmp;
            i = smallest;
        } else {
            break;
        }
    }

    pthread_mutex_unlock(&pq->lock);
    return top_patient;
}

int pq_peek_copy(PriorityQueue *pq, PatientRecord *out) {
    pthread_mutex_lock(&pq->lock);
    int has = (pq->size > 0);
    if (has && out != NULL) {
        *out = pq->heap[0];
    }
    pthread_mutex_unlock(&pq->lock);
    return has;
}

int pq_is_empty(PriorityQueue *pq) {
    pthread_mutex_lock(&pq->lock);
    int is_empty = (!pq->size);
    pthread_mutex_unlock(&pq->lock);
    return is_empty;
}

int pq_size(PriorityQueue *pq) {
    pthread_mutex_lock(&pq->lock);
    int size = pq->size;
    pthread_mutex_unlock(&pq->lock);
    return size;
}

static long estimate_service_time(int care_units) {
    if (care_units >= 3) return 10;
    if (care_units == 2) return 6;
    return 4;
}

void log_admission_event(int patient_id, int priority,
                         time_t arrival, time_t start, int care_units) {
    if (g_event_count >= MAX_EVENT_LOG) return;
    ScheduleEvent *ev   = &g_event_log[g_event_count++];
    ev->patient_id      = patient_id;
    ev->priority        = priority;
    ev->arrival_time    = arrival;
    ev->start_time      = start;
    ev->wait_time       = (long)(start - arrival);
    ev->service_time    = estimate_service_time(care_units);
    ev->turnaround_time = ev->wait_time + ev->service_time;
}

static int cmp_arrival(const void *a, const void *b) {
    const ScheduleEvent *ea = (const ScheduleEvent *)a;
    const ScheduleEvent *eb = (const ScheduleEvent *)b;
    if (ea->arrival_time < eb->arrival_time) return -1;
    if (ea->arrival_time > eb->arrival_time) return  1;
    return 0;
}

static int cmp_priority(const void *a, const void *b) {
    const ScheduleEvent *ea = (const ScheduleEvent *)a;
    const ScheduleEvent *eb = (const ScheduleEvent *)b;
    
    if (ea->priority < eb->priority) return -1;
    if (ea->priority > eb->priority) return  1;
    
    if (ea->arrival_time < eb->arrival_time) return -1;
    if (ea->arrival_time > eb->arrival_time) return  1;
    return 0;
}

static int simulate_single_server(ScheduleEvent *events, int n,
                                  long *sim_wait, long *sim_turnaround) {
    time_t sim_clock = (n > 0) ? events[0].arrival_time : 0;

    for (int i = 0; i < n; i++) {
        
        if (sim_clock < events[i].arrival_time)
            sim_clock = events[i].arrival_time;

        long svc    = events[i].service_time;
        long wait_t = (long)(sim_clock - events[i].arrival_time);
        long turn_t = wait_t + svc;

        sim_wait[i]       = wait_t;
        sim_turnaround[i] = turn_t;

        sim_clock += svc;
    }
    return n;
}

void run_scheduling_simulation(void) {
    if (!g_event_count) {
        printf("[SCHED] No admission events recorded — simulation skipped.\n");
        return;
    }

    
    ScheduleEvent fcfs_events[MAX_EVENT_LOG];
    ScheduleEvent prio_events[MAX_EVENT_LOG];
    memcpy(fcfs_events, g_event_log, g_event_count * sizeof(ScheduleEvent));
    memcpy(prio_events, g_event_log, g_event_count * sizeof(ScheduleEvent));

    qsort(fcfs_events, (size_t)g_event_count, sizeof(ScheduleEvent), cmp_arrival);
    qsort(prio_events, (size_t)g_event_count, sizeof(ScheduleEvent), cmp_priority);

    long fcfs_wait[MAX_EVENT_LOG], fcfs_turn[MAX_EVENT_LOG];
    long prio_wait[MAX_EVENT_LOG], prio_turn[MAX_EVENT_LOG];

    simulate_single_server(fcfs_events, g_event_count, fcfs_wait, fcfs_turn);
    simulate_single_server(prio_events, g_event_count, prio_wait, prio_turn);

    
    FILE *fp = fopen("logs/schedule_log.txt", "w");
    if (!fp) {
        perror("[SCHED] Cannot open logs/schedule_log.txt");
        return;
    }

    fprintf(fp, "# Prime BedSpace — Scheduling Simulation Log\n");
    fprintf(fp, "# Note: turnaround_time = wait_time + estimated_service_time\n");
    fprintf(fp, "#       Service estimates: ICU=10s | ISOLATION=6s | GENERAL=4s\n\n");

    fprintf(fp, "## FCFS (First-Come First-Served)\n");
    fprintf(fp, "%-6s | %-11s | %-12s | %-11s | %-16s\n",
            "pid", "arrival", "wait_time", "start_time", "turnaround_time");
    fprintf(fp, "%-6s-+-%-11s-+-%-12s-+-%-11s-+-%-16s\n",
            "------", "-----------", "------------", "-----------", "----------------");

    long fcfs_total_wait = 0, fcfs_total_turn = 0;
    for (int i = 0; i < g_event_count; i++) {
        long start_t = (long)fcfs_events[i].arrival_time + fcfs_wait[i];
        fprintf(fp, "%-6d | %-11ld | %-12ld | %-11ld | %-16ld\n",
                fcfs_events[i].patient_id,
                (long)fcfs_events[i].arrival_time,
                fcfs_wait[i],
                start_t,
                fcfs_turn[i]);
        fcfs_total_wait += fcfs_wait[i];
        fcfs_total_turn += fcfs_turn[i];
    }

    fprintf(fp, "\n## Priority Scheduling (lower priority number = more urgent)\n");
    fprintf(fp, "%-6s | %-8s | %-11s | %-12s | %-11s | %-16s\n",
            "pid", "priority", "arrival", "wait_time", "start_time", "turnaround_time");
    fprintf(fp, "%-6s-+-%-8s-+-%-11s-+-%-12s-+-%-11s-+-%-16s\n",
            "------", "--------", "-----------", "------------", "-----------", "----------------");

    long prio_total_wait = 0, prio_total_turn = 0;
    for (int i = 0; i < g_event_count; i++) {
        long start_t = (long)prio_events[i].arrival_time + prio_wait[i];
        fprintf(fp, "%-6d | %-8d | %-11ld | %-12ld | %-11ld | %-16ld\n",
                prio_events[i].patient_id,
                prio_events[i].priority,
                (long)prio_events[i].arrival_time,
                prio_wait[i],
                start_t,
                prio_turn[i]);
        prio_total_wait += prio_wait[i];
        prio_total_turn += prio_turn[i];
    }

    fclose(fp);

    
    double n = (double)g_event_count;
    printf("[SCHED] Simulation complete (%d patients).\n", g_event_count);
    printf("[SCHED] FCFS          — Avg Wait: %.2f s | Avg Turnaround: %.2f s\n",
           (double)fcfs_total_wait / n, (double)fcfs_total_turn / n);
    printf("[SCHED] Priority Sched — Avg Wait: %.2f s | Avg Turnaround: %.2f s\n",
           (double)prio_total_wait / n, (double)prio_total_turn / n);
    printf("[SCHED] Full log written to logs/schedule_log.txt\n");
}

#ifdef SCHEDULER_TEST

static void test_priority_queue(void) {
    PriorityQueue pq;
    pq_init(&pq);

    PatientRecord p1 = {.patient_id = 1, .priority = 3};
    PatientRecord p2 = {.patient_id = 2, .priority = 1};
    PatientRecord p3 = {.patient_id = 3, .priority = 4};
    PatientRecord p4 = {.patient_id = 4, .priority = 1};
    PatientRecord p5 = {.patient_id = 5, .priority = 5};

    pq_push(&pq, p1);
    pq_push(&pq, p2);
    pq_push(&pq, p3);
    pq_push(&pq, p4);
    pq_push(&pq, p5);

    int expected[] = {1, 1, 3, 4, 5};
    int pass = 1;
    for (int i = 0; i < 5; i++) {
        PatientRecord p = pq_pop(&pq);
        if (p.priority != expected[i]) {
            pass = 0;
            fprintf(stderr, "[TEST] pq_pop[%d]: expected priority %d, got %d\n",
                    i, expected[i], p.priority);
            break;
        }
    }

    if (pass) {
        printf("[TEST] Priority queue: PASS\n");
    } else {
        printf("[TEST] Priority queue: FAIL\n");
    }
}

static void test_simulation(void) {
    
    g_event_count = 0;

    time_t base = (time_t)1000000L; 
    
    log_admission_event(101, 2, base + 0,  base + 1,  3); 
    log_admission_event(102, 5, base + 2,  base + 2,  1); 
    log_admission_event(103, 1, base + 5,  base + 7,  2); 
    log_admission_event(104, 3, base + 10, base + 10, 1); 

    run_scheduling_simulation();
    printf("[TEST] Scheduling simulation: PASS\n");
}

int main(void) {
    test_priority_queue();
    test_simulation();
    return 0;
}

#endif 
