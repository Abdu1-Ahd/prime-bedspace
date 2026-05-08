#ifndef TYPES_H
#define TYPES_H

#include <time.h>

#define MAX_BEDS 20
#define ICU_CAPACITY 4
#define ISOLATION_CAPACITY 4
#define GENERAL_CAPACITY 12
#define SHM_KEY 0xBEDF00D

typedef struct {
    int patient_id;
    char name[64];
    int age;
    int severity;
    int priority;
    int care_units;
    time_t arrival_time;
} PatientRecord;

#define MAX_EVENT_LOG 200

typedef struct {
    int    patient_id;
    int    priority;         /* triage level 1-5 (1 = most urgent) */
    time_t arrival_time;     /* epoch seconds when patient arrived  */
    time_t start_time;       /* epoch seconds when admitted to bed  */
    long   wait_time;        /* start_time - arrival_time           */
    long   service_time;     /* estimated treatment duration (s)    */
    long   turnaround_time;  /* wait_time + service_time            */
} ScheduleEvent;

typedef struct {
    int partition_id;
    int start_unit;
    int size;
    int is_free;
    int patient_id;
    char bed_type[16];
} BedPartition;

#endif /* TYPES_H */
