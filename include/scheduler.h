/*
 * ============================================================
 * Project : Prime BedSpace - Hospital Patient Triage & Bed Allocator
 * File    : scheduler.h
 * Group   : Group 14
 * Members : Abdul Ahad (24F-0727), Abdul Rahim (24F-0514)
 * Date    : 2026-05-12
 * Purpose : Scheduler interface — PriorityQueue struct, SchedEntry struct, and all scheduling function declarations.
 * Compile : gcc -Wall -Wextra -pthread -Iinclude <file> -lrt -lpthread
 * ============================================================
 */
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "types.h"
#include <pthread.h>

#define MAX_QUEUE_SIZE 100

typedef struct {
    PatientRecord heap[MAX_QUEUE_SIZE];
    int size;
    pthread_mutex_t lock;
} PriorityQueue;

void pq_init(PriorityQueue *pq);
int pq_push(PriorityQueue *pq, PatientRecord p);
PatientRecord pq_pop(PriorityQueue *pq);
int pq_peek_copy(PriorityQueue *pq, PatientRecord *out); 
int pq_is_empty(PriorityQueue *pq); 
int pq_size(PriorityQueue *pq);     

extern ScheduleEvent g_event_log[];
extern int           g_event_count;

extern PriorityQueue  g_wait_queue;   
extern pthread_mutex_t g_queue_mutex;   

void log_admission_event(int patient_id, int priority,
                         time_t arrival, time_t start, int care_units);
void run_scheduling_simulation(void);

#endif 
