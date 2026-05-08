#include "scheduler.h"
#include <stdio.h>
#include <string.h>

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
            PatientRecord temp = pq->heap[parent];
            pq->heap[parent] = pq->heap[i];
            pq->heap[i] = temp;
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
    
    int i = 0;
    while (1) {
        int left = 2 * i + 1;
        int right = 2 * i + 2;
        int smallest = i;
        
        if (left < pq->size && pq->heap[left].priority < pq->heap[smallest].priority) {
            smallest = left;
        }
        if (right < pq->size && pq->heap[right].priority < pq->heap[smallest].priority) {
            smallest = right;
        }
        
        if (smallest != i) {
            PatientRecord temp = pq->heap[i];
            pq->heap[i] = pq->heap[smallest];
            pq->heap[smallest] = temp;
            i = smallest;
        } else {
            break;
        }
    }
    
    pthread_mutex_unlock(&pq->lock);
    return result;
}

int pq_is_empty(PriorityQueue *pq) {
    return pq->size == 0;
}

int pq_size(PriorityQueue *pq) {
    return pq->size;
}

#ifdef SCHEDULER_TEST
void scheduler_self_test(void) {
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
    
    int expected_priorities[] = {1, 1, 3, 4, 5};
    int pass = 1;
    for (int i = 0; i < 5; i++) {
        PatientRecord p = pq_pop(&pq);
        if (p.priority != expected_priorities[i]) {
            pass = 0;
            break;
        }
    }
    
    if (pass) {
        printf("PASS\n");
    } else {
        printf("FAIL\n");
    }
}

int main(void) {
    scheduler_self_test();
    return 0;
}
#endif
