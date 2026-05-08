/**
 * ==============================================================================
 * Project: Prime BedSpace
 * File: bed_allocator.c
 * Group: Zawiar & Subhani
 * Members: Abdul Ahad Zawiar (Abdu1-Ahd), AbdulRahim Subhani (abdulrahim-subh)
 * Date: 2026-05-08
 * Purpose: Phase 4 — Memory management simulation.
 *          Implements a free-list allocator over shm_ward[] with three
 *          placement strategies (Best-Fit, First-Fit, Worst-Fit), left+right
 *          coalescing, external fragmentation reporting, and a simple paging
 *          simulation that computes internal fragmentation per admission.
 * ==============================================================================
 */

#include "bed_allocator.h"
#include "types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── Module-level log file ───────────────────────────────────────────── */

static FILE *g_mem_log = NULL;

/* ── Free-list helpers ───────────────────────────────────────────────── */

/* Allocate a new FreeNode on the heap. Exits on OOM. */
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

/* Insert node for idx into free_list, maintaining ascending order by idx. */
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

/* Remove the node for idx from free_list. Returns 1 if found, 0 otherwise. */
static int fl_remove(BedAllocator *a, int idx) {
    FreeNode *cur  = a->free_list;
    FreeNode *prev = NULL;

    while (cur) {
        if (cur->partition_idx == idx) {
            if (prev) prev->next = cur->next;
            else       a->free_list = cur->next;
            free(cur);
            return 1;
        }
        prev = cur;
        cur  = cur->next;
    }
    return 0;
}

/* ── ba_init ─────────────────────────────────────────────────────────── */

void ba_init(BedAllocator *a, BedPartition *ward, int total, AllocStrategy s) {
    a->ward      = ward;
    a->total     = total;
    a->strategy  = s;
    a->free_list = NULL;

    /* Build initial free_list from all free partitions */
    for (int i = 0; i < total; i++) {
        if (ward[i].is_free && ward[i].size > 0)
            fl_insert(a, i);
    }

    /* Open memory log — append mode so multiple runs accumulate */
    if (!g_mem_log) {
        g_mem_log = fopen("logs/memory_log.txt", "a");
        if (!g_mem_log)
            fprintf(stderr, "[ALLOC] Cannot open logs/memory_log.txt\n");
    }

    printf("[ALLOC] Initialised with strategy: %s | %d partitions | %d free\n",
           ba_strategy_name(s), total, total); /* all free at init */
}

/* ── ba_alloc ────────────────────────────────────────────────────────── */

int ba_alloc(BedAllocator *a, int care_units, const char *bed_type,
             int patient_id_hint) {
    FreeNode *chosen     = NULL;
    FreeNode *chosen_prev = NULL;
    int       chosen_delta = 0; /* size - care_units for chosen node */

    /* Initialise delta sentinels per strategy */
    int best_delta  =  1 << 30; /* BEST:  minimise surplus           */
    int worst_delta = -1;       /* WORST: maximise surplus           */

    FreeNode *cur  = a->free_list;
    FreeNode *prev = NULL;

    while (cur) {
        int idx = cur->partition_idx;

        /* Only consider partitions matching the requested bed type */
        if (a->ward[idx].size >= care_units &&
            strcmp(a->ward[idx].bed_type, bed_type) == 0) {

            int delta = a->ward[idx].size - care_units;

            switch (a->strategy) {
                case STRATEGY_FIRST:
                    /* First-Fit: take the very first match */
                    chosen      = cur;
                    chosen_prev = prev;
                    chosen_delta = delta;
                    goto found;

                case STRATEGY_BEST:
                    if (delta < best_delta) {
                        best_delta   = delta;
                        chosen       = cur;
                        chosen_prev  = prev;
                        chosen_delta = delta;
                    }
                    break;

                case STRATEGY_WORST:
                    if (delta > worst_delta) {
                        worst_delta  = delta;
                        chosen       = cur;
                        chosen_prev  = prev;
                        chosen_delta = delta;
                    }
                    break;
            }
        }
        prev = cur;
        cur  = cur->next;
    }

found:
    if (!chosen) return -1; /* no fit */

    int idx = chosen->partition_idx;

    /* Remove from free_list */
    if (chosen_prev) chosen_prev->next = chosen->next;
    else              a->free_list      = chosen->next;
    free(chosen);

    /* Mark partition occupied */
    a->ward[idx].is_free = 0;

    /* ── Split remainder ─────────────────────────────────────────── */
    if (chosen_delta > 0) {
        /* Look for an absorbed/zero-size slot at idx+1 to place the remainder */
        int split_idx = idx + 1;
        if (split_idx < a->total &&
            a->ward[split_idx].size == 0) {
            /* Materialise split remainder */
            a->ward[split_idx].partition_id = split_idx;
            a->ward[split_idx].start_unit   = a->ward[idx].start_unit + care_units;
            a->ward[split_idx].size         = chosen_delta;
            a->ward[split_idx].is_free      = 1;
            a->ward[split_idx].patient_id   = -1;
            strncpy(a->ward[split_idx].bed_type, a->ward[idx].bed_type,
                    sizeof(a->ward[split_idx].bed_type) - 1);
            fl_insert(a, split_idx);

            /* Shrink allocated partition to exact fit */
            a->ward[idx].size = care_units;

            printf("[ALLOC] Split: partition %d (%d units) → alloc=%d, "
                   "remainder at %d (%d units)\n",
                   idx, care_units + chosen_delta,
                   care_units, split_idx, chosen_delta);
        }
        /* If slot not available (live partition there), no split — first-fit
         * behaviour degrades gracefully without corrupting real data.        */
    }

    /* ── Paging simulation (Task 4) ──────────────────────────────── */
    int pages_needed  = (care_units + PAGE_SIZE - 1) / PAGE_SIZE;
    int internal_frag = (pages_needed * PAGE_SIZE) - care_units;
    printf("[PAGING] Patient %d: %d care units → %d pages, "
           "internal frag = %d units\n",
           patient_id_hint, care_units, pages_needed, internal_frag);

    return idx;
}

/* ── ba_free ─────────────────────────────────────────────────────────── */

void ba_free(BedAllocator *a, int idx) {
    if (idx < 0 || idx >= a->total) return;

    ba_print_ward_map(a, "BEFORE coalesce");

    a->ward[idx].is_free    = 1;
    a->ward[idx].patient_id = -1;
    fl_insert(a, idx);

    /* ── Left coalesce ───────────────────────────────────────────── */
    if (idx > 0 &&
        a->ward[idx - 1].is_free &&
        a->ward[idx - 1].size > 0 &&
        strcmp(a->ward[idx - 1].bed_type, a->ward[idx].bed_type) == 0) {

        a->ward[idx - 1].size += a->ward[idx].size;
        a->ward[idx].size      = 0; /* mark absorbed */
        fl_remove(a, idx);
        idx = idx - 1; /* continue right-coalesce from merged node */

        printf("[ALLOC] Left-coalesced: partition now at %d (size=%d)\n",
               idx, a->ward[idx].size);
    }

    /* ── Right coalesce ──────────────────────────────────────────── */
    if (idx + 1 < a->total &&
        a->ward[idx + 1].is_free &&
        a->ward[idx + 1].size > 0 &&
        strcmp(a->ward[idx + 1].bed_type, a->ward[idx].bed_type) == 0) {

        a->ward[idx].size += a->ward[idx + 1].size;
        a->ward[idx + 1].size = 0; /* mark absorbed */
        fl_remove(a, idx + 1);

        printf("[ALLOC] Right-coalesced: partition %d absorbed into %d (size=%d)\n",
               idx + 1, idx, a->ward[idx].size);
    }

    ba_print_ward_map(a, "AFTER coalesce");
    ba_fragmentation_report(a, g_mem_log);
}

/* ── ba_print_ward_map ───────────────────────────────────────────────── */

void ba_print_ward_map(BedAllocator *a, const char *label) {
    printf("[MAP %s] ", label);
    for (int i = 0; i < a->total; i++) {
        if (a->ward[i].size == 0) continue; /* absorbed slot — skip */
        const char *type = a->ward[i].bed_type;
        const char *abbr;

        if      (strcmp(type, "ICU")       == 0) abbr = "ICU";
        else if (strcmp(type, "ISOLATION") == 0) abbr = "ISO";
        else                                      abbr = "GEN";

        printf("[%s:%s]", abbr, a->ward[i].is_free ? "FREE" : "OCC ");
    }
    printf("\n");
}

/* ── ba_fragmentation_report ─────────────────────────────────────────── */

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

/* ── ba_strategy_name ────────────────────────────────────────────────── */

const char *ba_strategy_name(AllocStrategy s) {
    switch (s) {
        case STRATEGY_BEST:  return "best";
        case STRATEGY_FIRST: return "first";
        case STRATEGY_WORST: return "worst";
        default:             return "unknown";
    }
}
