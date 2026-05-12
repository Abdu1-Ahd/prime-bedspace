/*
 * ============================================================
 * Project : Prime BedSpace - Hospital Patient Triage & Bed Allocator
 * File    : bed_allocator.c
 * Group   : Group 14
 * Members : Abdul Ahad (24F-0727), Abdul Rahim (24F-0514)
 * Date    : 2026-05-12
 * Purpose : Memory allocator — implements Best-Fit, First-Fit, and Worst-Fit strategies with coalescing and fragmentation reporting.
 * Compile : gcc -Wall -Wextra -pthread -Iinclude <file> -lrt -lpthread
 * ============================================================
 */
#include "bed_allocator.h"
#include "types.h"
#include "debug_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>

static FILE *g_mem_log = NULL;

static FreeNode *node_new(int idx) {
    FreeNode *n = (FreeNode *)malloc(sizeof(FreeNode));
    if (!n) {
        fprintf(stderr, "[ALLOC] OOM allocating FreeNode\n");
        exit(1);
    }
    n->partition_idx = idx;
    n->next          = NULL;
    return n;
}

static void fl_insert(BedAllocator *a, int idx) {
    FreeNode *n = node_new(idx);

    if (!a->free_list || a->free_list->partition_idx > idx) {
        n->next       = a->free_list;
        a->free_list  = n;
        return;
    }

    FreeNode *cur = a->free_list;
    while (cur->next && cur->next->partition_idx < idx)
        cur = cur->next;
    n->next   = cur->next;
    cur->next = n;
}

void ba_init(BedAllocator *a, BedPartition *ward, int total, AllocStrategy s) {
    a->ward      = ward;
    a->total     = total;
    a->strategy  = s;
    a->free_list = NULL;

    int free_count = 0;

    
    for (int i = 0; i < total; i++) {
        if (ward[i].is_free && ward[i].size > 0) {
            fl_insert(a, i);
            free_count++;
        }
    }

    
    if (!g_mem_log) {
        g_mem_log = fopen("logs/memory_log.txt", "a");
        if (!g_mem_log)
            fprintf(stderr, "[ALLOC] Cannot open logs/memory_log.txt\n");
    }

    printf("[ALLOC] Initialised with strategy: %s | %d partitions | %d free\n",
           ba_strategy_name(s), total, free_count);
}

int ba_alloc(BedAllocator *a, int care_units, const char *bed_type,
             int patient_id_hint) {
    FreeNode *chosen      = NULL;
    FreeNode *chosen_prev = NULL;

    
    int best_delta  =  INT_MAX; 
    int worst_delta = -1;       

    FreeNode *cur  = a->free_list;
    FreeNode *prev = NULL;

    while (cur) {
        int idx = cur->partition_idx;

        
        if (a->ward[idx].size >= care_units &&
            strcmp(a->ward[idx].bed_type, bed_type) == 0) {

            int delta = a->ward[idx].size - care_units;

            switch (a->strategy) {
                case STRATEGY_FIRST:
                    
                    chosen      = cur;
                    chosen_prev = prev;
                    goto found;

                case STRATEGY_BEST:
                    if (delta < best_delta) {
                        best_delta   = delta;
                        chosen       = cur;
                        chosen_prev  = prev;
                    }
                    break;

                case STRATEGY_WORST:
                    if (delta > worst_delta) {
                        worst_delta  = delta;
                        chosen       = cur;
                        chosen_prev  = prev;
                    }
                    break;
            }
        }
        prev = cur;
        cur  = cur->next;
    }

found:
    if (!chosen) return -1; 

    int idx = chosen->partition_idx;

    
    if (chosen_prev) chosen_prev->next = chosen->next;
    else              a->free_list      = chosen->next;
    free(chosen);

    
    a->ward[idx].is_free = 0;

    {
        char data[256];
        snprintf(data, sizeof(data),
                 "{\"patient_id\":%d,\"care_units\":%d,\"idx\":%d,\"bed_size\":%d}",
                 patient_id_hint, care_units, idx, a->ward[idx].size);
        dbg_write_ndjson("pre", "H2", "bed_allocator.c:ba_alloc", "alloc_choice", data);
    }

    
    int pages_needed  = (care_units + PAGE_SIZE - 1) / PAGE_SIZE;
    int internal_frag = (pages_needed * PAGE_SIZE) - care_units;
    printf("[PAGING] Patient %d: %d care units → %d pages, "
           "internal frag = %d units\n",
           patient_id_hint, care_units, pages_needed, internal_frag);

    return idx;
}

void ba_free(BedAllocator *a, int partition_idx) {
    if (partition_idx < 0 || partition_idx >= a->total) return;

    ba_print_ward_map(a, "BEFORE free");

    
    a->ward[partition_idx].is_free    = 1;
    a->ward[partition_idx].patient_id = -1;

    
    fl_insert(a, partition_idx);

    DBG1("pre", "H1", "bed_allocator.c:ba_free", "freed_partition_idx",
         "idx", partition_idx);

    ba_print_ward_map(a, "AFTER free");
    ba_fragmentation_report(a, g_mem_log);
}

void ba_print_ward_map(BedAllocator *a, const char *label) {
    printf("[MAP %s] ", label);
    for (int i = 0; i < a->total; i++) {
        if (a->ward[i].size == 0) continue; 
        const char *type = a->ward[i].bed_type;
        const char *abbr;

        if      (strcmp(type, "ICU")       == 0) abbr = "ICU";
        else if (strcmp(type, "ISOLATION") == 0) abbr = "ISO";
        else                                      abbr = "GEN";

        printf("[%s:%s]", abbr, a->ward[i].is_free ? "FREE" : "OCC ");
    }
    printf("\n");
}

void ba_fragmentation_report(BedAllocator *a, FILE *log) {
    int total_free   = 0;
    int largest_free = 0;

    FreeNode *cur = a->free_list;
    while (cur) {
        int sz = a->ward[cur->partition_idx].size;
        if (sz > 0) {
            total_free += sz;
            if (sz > largest_free) largest_free = sz;
        }
        cur = cur->next;
    }

    double ext_frag = 0.0;
    if (total_free > 0)
        ext_frag = (1.0 - (double)largest_free / total_free) * 100.0;

    printf("[FRAG] Total free: %d units | Largest block: %d units | "
           "External frag: %.1f%%\n",
           total_free, largest_free, ext_frag);

    if (log) {
        time_t now = time(NULL);
        char ts[32];
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&now));
        fprintf(log,
                "[%s] free=%d largest=%d ext_frag=%.1f%%\n",
                ts, total_free, largest_free, ext_frag);
        fflush(log);
    }
}

const char *ba_strategy_name(AllocStrategy s) {
    switch (s) {
        case STRATEGY_BEST:  return "best";
        case STRATEGY_FIRST: return "first";
        case STRATEGY_WORST: return "worst";
        default:             return "unknown";
    }
}
