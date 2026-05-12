/*
 * ============================================================
 * Project : Prime BedSpace - Hospital Patient Triage & Bed Allocator
 * File    : terminal_ui.c
 * Group   : Group 14
 * Members : Abdul Ahad (24F-0727), Abdul Rahim (24F-0514)
 * Date    : 2026-05-12
 * Purpose : ANSI terminal dashboard — renders live ward map, patient events, and fragmentation bar.
 * Compile : gcc -Wall -Wextra -pthread -Iinclude <file> -lrt -lpthread
 * ============================================================
 */
#include "terminal_ui.h"
#include "types.h"
#include "bed_allocator.h"
#include "scheduler.h"
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <signal.h>

static volatile sig_atomic_t  ui_running = 0;
static pthread_t              ui_thread;

static BedPartition  *g_ui_ward     = NULL;
static int            g_ui_total    = 0;
static const char    *g_ui_strategy = "best";

static void *render_loop(void *arg) {
    (void)arg;

    while (ui_running) {
        ui_render(g_ui_ward, g_ui_total);
        usleep(UI_REFRESH_US);
    }
    return NULL;
}

void ui_render(BedPartition *ward, int total) {
    
    printf("\033[2J\033[H");

    printf("╔══════════════════════════════════════════════╗\n");
    printf("║      Prime BedSpace — Live Ward Status       ║\n");
    printf("╚══════════════════════════════════════════════╝\n\n");

    
    const char *types[] = { "ICU", "ISOLATION", "GENERAL" };
    const char *labels[] = { "ICU      ", "ISOLATION", "GENERAL  " };

    for (int t = 0; t < 3; t++) {
        printf("  %s: ", labels[t]);
        for (int i = 0; i < total; i++) {
            if (!ward[i].size) continue; 
            if (strcmp(ward[i].bed_type, types[t])) continue;

            if (!ward[i].is_free) {
                
                printf("\033[1;31m[■]\033[0m");
            } else {
                
                printf("\033[0;32m[ ]\033[0m");
            }
        }
        printf("\n");
    }

    
    static int last_depth = 0;
    int trylock_failed = 0;
    if (!pthread_mutex_trylock(&g_queue_mutex)) {
        last_depth = g_wait_queue.size;
        pthread_mutex_unlock(&g_queue_mutex);
    } else {
        trylock_failed = 1;
    }

    if (trylock_failed) {
        printf("\n  Waiting queue depth : %d (stale)\n", last_depth);
    } else {
        printf("\n  Waiting queue depth : %d\n", last_depth);
    }
    printf("  Strategy            : %s\n", g_ui_strategy);
    printf("\n  \033[2m[■] = occupied   [ ] = free\033[0m\n");
    fflush(stdout);
}

static int ui_started = 0;

void ui_start(BedPartition *ward, int total, const char *strategy) {
    if (ui_started) return;

    g_ui_ward     = ward;
    g_ui_total    = total;
    g_ui_strategy = strategy ? strategy : "best";
    ui_running    = 1;

    if (pthread_create(&ui_thread, NULL, render_loop, NULL)) {
        perror("[UI] pthread_create failed");
        ui_running = 0;
        return;
    }

    ui_started = 1;
    printf("[UI] Terminal renderer started (strategy=%s).\n", g_ui_strategy);
}

void ui_stop(void) {
    if (!ui_started) return;
    ui_running = 0;
    pthread_join(ui_thread, NULL);
    ui_started = 0;
    
    
    printf("\033[2J\033[H");
    printf("[UI] Terminal renderer stopped.\n");
    fflush(stdout);
}
