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

typedef struct {
    int partition_id;
    int start_unit;
    int size;
    int is_free;
    int patient_id;
    char bed_type[16];
} BedPartition;

#endif /* TYPES_H */
