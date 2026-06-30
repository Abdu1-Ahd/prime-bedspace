/*
 * ============================================================
 * Project : Prime BedSpace - Hospital Patient Triage & Bed Allocator
 * File    : bed_allocator.h
 * Group   : Group 14
 * Members : Abdul Ahad (24F-0727), Abdul Rahim (24F-0514)
 * Date    : 2026-05-12
 * Purpose : Allocator interface — allocation strategy enum and all allocator function declarations.
 * Compile : gcc -Wall -Wextra -pthread -Iinclude <file> -lrt -lpthread
 * ============================================================
 */
#ifndef BED_ALLOCATOR_H
#define BED_ALLOCATOR_H

#include "types.h"
#include <stdio.h>

typedef enum {
    STRATEGY_BEST  = 0,  
    STRATEGY_FIRST,      
    STRATEGY_WORST       
} AllocStrategy;

typedef struct FreeNode {
    int               partition_idx;
    struct FreeNode  *next;
} FreeNode;

typedef struct {
    BedPartition *ward;        
    int           total;       
    FreeNode     *free_list;   
    AllocStrategy strategy;    
} BedAllocator;

#define PAGE_SIZE 2            

void ba_init(BedAllocator *a, BedPartition *ward, int total, AllocStrategy s);

int  ba_alloc(BedAllocator *a, int care_units, const char *bed_type,
              int patient_id_hint);

void ba_free(BedAllocator *a, int partition_idx);

void ba_print_ward_map(BedAllocator *a, const char *label);

void ba_fragmentation_report(BedAllocator *a, FILE *log);

const char *ba_strategy_name(AllocStrategy s);

#endif 


// session:9c10a801
