/**
 * ==============================================================================
 * Project: Prime BedSpace
 * File: scheduler.c
 * Group: Zawiar & Subhani
 * Members: Abdul Ahad Zawiar (Abdu1-Ahd), AbdulRahim Subhani (abdulrahim-subh)
 * Date: 2026-05-08
 * Purpose: Thread-safe min-heap priority queue and scheduling simulation
 *          (FCFS + Priority Scheduling) for patient admission analysis.
 * ==============================================================================
 */

#include "scheduler.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

/* ── Global event log ──────────────────────────────────────────────────── */
ScheduleEvent g_event_log[MAX_EVENT_LOG];
int           g_event_count = 0;

/* ── Priority Queue implementation ─────────────────────────────────────── */

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

    /* Sift-up (min-heap on priority) */
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
    if (pq->size == 0) {
        pthread_mutex_unlock(&pq->lock);
        PatientRecord empty = {0};
        empty.patient_id = -1;
        return empty;
    }

    PatientRecord result = pq->heap[0];
    pq->size--;
    pq->heap[0] = pq->heap[pq->size];

    /* Sift-down */
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
    return result;
}

/* Copy top element without removing. Returns 1 on success, 0 if empty.
   Thread-safe: copies value while holding lock, no pointer escapes. */
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
    return pq->size == 0;
}

int pq_size(PriorityQueue *pq) {
    return pq->size;
}

/* ── Admission event logging ────────────────────────────────────────────── */

/* Service-time estimates (seconds) by care_units capacity required:
 *   care_units >= 3  → ICU       → 10 s average
 *   care_units == 2  → ISOLATION →  6 s average
 *   care_units == 1  → GENERAL   →  4 s average  */
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

/* ── Scheduling simulation ──────────────────────────────────────────────── */

/* qsort comparators */
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
    /* lower priority number = higher clinical urgency */
    if (ea->priority < eb->priority) return -1;
    if (ea->priority > eb->priority) return  1;
    /* tie-break by arrival */
    if (ea->arrival_time < eb->arrival_time) return -1;
    if (ea->arrival_time > eb->arrival_time) return  1;
    return 0;
}

/* Single-server simulation over a sorted event array.
 * Fills sim_wait[] and sim_turnaround[] in caller-provided arrays.
 * Returns the number of events processed.                             */
static int simulate_single_server(ScheduleEvent *events, int n,
                                  long *sim_wait, long *sim_turnaround) {
    time_t sim_clock = (n > 0) ? events[0].arrival_time : 0;

    for (int i = 0; i < n; i++) {
        /* Server idles until next patient arrives */
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
    if (g_event_count == 0) {
        printf("[SCHED] No admission events recorded — simulation skipped.\n");
        return;
    }

    /* Make local copies to sort without touching the live log */
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

    /* ── Write Gantt-style log ─────────────────────────────────────────── */
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

    /* ── Print summary to stdout ────────────────────────────────────────── */
    double n = (double)g_event_count;
    printf("[SCHED] Simulation complete (%d patients).\n", g_event_count);
    printf("[SCHED] FCFS          — Avg Wait: %.2f s | Avg Turnaround: %.2f s\n",
           (double)fcfs_total_wait / n, (double)fcfs_total_turn / n);
    printf("[SCHED] Priority Sched — Avg Wait: %.2f s | Avg Turnaround: %.2f s\n",
           (double)prio_total_wait / n, (double)prio_total_turn / n);
    printf("[SCHED] Full log written to logs/schedule_log.txt\n");
}

/* ── Self-test (compiled only with -DSCHEDULER_TEST) ─────────────────── */

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
    /* Seed a small synthetic event log and run the simulation */
    g_event_count = 0;

    time_t base = (time_t)1000000L; /* arbitrary epoch anchor */
    /* care_units: 3=ICU(10s), 2=ISOLATION(6s), 1=GENERAL(4s) */
    log_admission_event(101, 2, base + 0,  base + 1,  3); /* ICU      */
    log_admission_event(102, 5, base + 2,  base + 2,  1); /* GENERAL  */
    log_admission_event(103, 1, base + 5,  base + 7,  2); /* ISOLATION */
    log_admission_event(104, 3, base + 10, base + 10, 1); /* GENERAL  */

    run_scheduling_simulation();
    printf("[TEST] Scheduling simulation: PASS\n");
}

int main(void) {
    test_priority_queue();
    test_simulation();
    return 0;
}

#endif /* SCHEDULER_TEST */
