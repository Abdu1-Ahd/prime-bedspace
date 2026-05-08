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
int pq_is_empty(PriorityQueue *pq);
int pq_size(PriorityQueue *pq);

#endif // SCHEDULER_H
