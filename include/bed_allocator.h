#ifndef BED_ALLOCATOR_H
#define BED_ALLOCATOR_H

#include "types.h"
#include <stdio.h>

/* ── Allocation strategy ─────────────────────────────────────────────── */

typedef enum {
    STRATEGY_BEST  = 0,  /* smallest fitting free block        */
    STRATEGY_FIRST,      /* first fitting free block (L→R)     */
    STRATEGY_WORST       /* largest fitting free block          */
} AllocStrategy;

/* ── Free-list node ──────────────────────────────────────────────────── */

/* Singly-linked list of free BedPartition indices into shm_ward[].
 * Sorted ascending by partition_idx for O(n) coalescing.             */
typedef struct FreeNode {
    int               partition_idx;
    struct FreeNode  *next;
} FreeNode;

/* ── Allocator state ─────────────────────────────────────────────────── */

/* Caller must hold bed_mutex before invoking any ba_* function.       */
typedef struct {
    BedPartition *ward;        /* pointer to shm_ward[]             */
    int           total;       /* total partitions (MAX_BEDS)        */
    FreeNode     *free_list;   /* head of sorted free-list           */
    AllocStrategy strategy;    /* allocation policy                  */
} BedAllocator;

/* ── Paging constant ─────────────────────────────────────────────────── */
#define PAGE_SIZE 2            /* care units per page                */

/* ── Public API ─────────────────────────────────────────────────────── */

/* Initialise allocator. Builds free_list from ward[]. Opens memory_log.txt. */
void ba_init(BedAllocator *a, BedPartition *ward, int total, AllocStrategy s);

/* Allocate a partition satisfying care_units and bed_type.
 * patient_id_hint used for paging log line only (not stored).
 * Returns partition_idx on success, -1 if no fit.
 * Caller must hold bed_mutex.                                         */
int  ba_alloc(BedAllocator *a, int care_units, const char *bed_type,
              int patient_id_hint);

/* Free partition at idx, coalesce left+right same-type neighbours.
 * Prints ward map before and after coalescing.
 * Writes fragmentation report to memory_log.txt.
 * Caller must hold bed_mutex.                                         */
void ba_free(BedAllocator *a, int partition_idx);

/* Print one-line ASCII ward map to stdout.
 * Format: [ICU:OCC][ICU:FREE][ISO:OCC]...
 * label is a short prefix string (e.g. "BEFORE coalesce").           */
void ba_print_ward_map(BedAllocator *a, const char *label);

/* Compute and print external fragmentation metrics.
 * Writes a timestamped line to log (memory_log.txt).                 */
void ba_fragmentation_report(BedAllocator *a, FILE *log);

/* Return human-readable strategy name. */
const char *ba_strategy_name(AllocStrategy s);

#endif /* BED_ALLOCATOR_H */
