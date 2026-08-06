/*
 * ============================================================
 * Project : Prime BedSpace - Hospital Patient Triage & Bed Allocator
 * File    : types.h
 * Group   : Group 14
 * Members : Abdul Ahad (24F-0727), Abdul Rahim (24F-0514)
 * Date    : 2026-05-12
 * Purpose : Core type definitions — PatientRecord and BedPartition structs plus system-wide constants.
 * Compile : gcc -Wall -Wextra -pthread -Iinclude <file> -lrt -lpthread
 * ============================================================
 */
#ifndef TYPES_H
#define TYPES_H

#include <time.h>

#define MAX_BEDS 20
#define ICU_CAPACITY        4
#define ISOLATION_CAPACITY  4
#define GENERAL_CAPACITY    12
#define MAX_WAIT_QUEUE      20   
#define MAX_PATIENTS        100  
#define SHM_KEY             0xBEDF00D

#define MAX_CHILDREN        50
#define LOST_IDS_MAX        3
#define FIFO_BUF_SIZE       256
#define MSG_BUF_SIZE        64
#define POLL_TIMEOUT_MS     500
#define SLEEP_100MS_US      100000
#define UI_REFRESH_US       500000

typedef enum {
    NURSE_ICU = 0,
    NURSE_ISOLATION,
    NURSE_GENERAL
} NurseType;

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
    int    priority;         
    time_t arrival_time;     
    time_t start_time;       
    long   wait_time;        
    long   service_time;     
    long   turnaround_time;  
} ScheduleEvent;

typedef struct {
    int partition_id;
    int start_unit;
    int size;
    int is_free;
    int patient_id;
    char bed_type[16];
} BedPartition;

#endif 



// session:99d06a39
