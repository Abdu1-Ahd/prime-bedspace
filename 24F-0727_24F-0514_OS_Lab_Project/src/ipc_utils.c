/**
 * ==============================================================================
 * Project: Prime BedSpace
 * File: ipc_utils.c
 * Group: Zawiar & Subhani
 * Members: Abdul Ahad Zawiar (Abdu1-Ahd), AbdulRahim Subhani (abdulrahim-subh)
 * Date: 2026-05-08
 * Purpose: POSIX IPC utilities — System V shared memory and named FIFO helpers.
 * ==============================================================================
 */

#include "types.h"
#include "ipc.h"
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>

/* ── Shared Memory ───────────────────────────────────────────────────── */

void *init_shared_memory(void) {
    int shmid = shmget((key_t)SHM_KEY,
                       MAX_BEDS * sizeof(BedPartition),
                       IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("[IPC] shmget failed");
        return NULL;
    }

    void *ptr = shmat(shmid, NULL, 0);
    if (ptr == (void *)-1) {
        perror("[IPC] shmat failed");
        return NULL;
    }
    return ptr;
}

void detach_shared_memory(void *ptr) {
    if (ptr != NULL && ptr != (void *)-1) {
        shmdt(ptr);
    }
}

/* ── FIFO helpers ────────────────────────────────────────────────────── */

/* Open FIFO for blocking read. Returns fd or -1. */
int open_discharge_fifo_read(void) {
    int fd = open(FIFO_DISCHARGE_PATH, O_RDONLY | O_NONBLOCK);
    if (fd == -1 && errno != ENXIO && errno != ENOENT) {
        perror("[IPC] open discharge FIFO read");
    }
    return fd;
}

/* Open FIFO for non-blocking write. Returns fd or -1. */
int open_discharge_fifo_write(void) {
    int fd = open(FIFO_DISCHARGE_PATH, O_WRONLY | O_NONBLOCK);
    if (fd == -1 && errno != ENXIO && errno != ENOENT) {
        perror("[IPC] open discharge FIFO write");
    }
    return fd;
}

/* Open triage FIFO for non-blocking read. Returns fd or -1.
 * Use for retry loops where the FIFO may not exist yet. */
int open_triage_fifo_read(void) {
    int fd = open(FIFO_TRIAGE_PATH, O_RDONLY | O_NONBLOCK);
    if (fd == -1 && errno != ENXIO && errno != ENOENT) {
        perror("[IPC] open triage FIFO read");
    }
    return fd;
}

/* Open triage FIFO for BLOCKING read (no O_NONBLOCK).
 * Used by the receptionist thread after the FIFO is confirmed to exist.
 * read() on this fd will block until data arrives — no busy-spin needed. */
int open_triage_fifo_read_block(void) {
    int fd = open(FIFO_TRIAGE_PATH, O_RDONLY); /* blocking */
    if (fd == -1) {
        perror("[IPC] open triage FIFO read (blocking)");
    }
    return fd;
}

/* Open triage FIFO for non-blocking write. Returns fd or -1. */
int open_triage_fifo_write(void) {
    int fd = open(FIFO_TRIAGE_PATH, O_WRONLY | O_NONBLOCK);
    if (fd == -1 && errno != ENXIO && errno != ENOENT) {
        perror("[IPC] open triage FIFO write");
    }
    return fd;
}
