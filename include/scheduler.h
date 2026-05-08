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
PatientRecord *pq_peek(PriorityQueue *pq);
int pq_is_empty(PriorityQueue *pq);
int pq_size(PriorityQueue *pq);

/* --- Scheduling simulation -------------------------------------------- */
extern ScheduleEvent g_event_log[];
extern int           g_event_count;

void log_admission_event(int patient_id, int priority,
                         time_t arrival, time_t start, int care_units);
void run_scheduling_simulation(void);

#endif // SCHEDULER_H
