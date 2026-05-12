/*
 * ============================================================
 * Project : Prime BedSpace - Hospital Patient Triage & Bed Allocator
 * File    : ipc_utils.c
 * Group   : Group 14
 * Members : Abdul Ahad (24F-0727), Abdul Rahim (24F-0514)
 * Date    : 2026-05-12
 * Purpose : IPC utilities — shared memory init/detach and non-blocking FIFO open helpers.
 * Compile : gcc -Wall -Wextra -pthread -Iinclude <file> -lrt -lpthread
 * ============================================================
 */
#include "types.h"
#include "ipc.h"
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>

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

int open_discharge_fifo_read(void) {
    int fd = open(FIFO_DISCHARGE_PATH, O_RDONLY | O_NONBLOCK);
    if (fd == -1 && errno != ENXIO && errno != ENOENT) {
        perror("[IPC] open discharge FIFO read");
    }
    return fd;
}

int open_discharge_fifo_write(void) {
    int fd = open(FIFO_DISCHARGE_PATH, O_WRONLY | O_NONBLOCK);
    if (fd == -1 && errno != ENXIO && errno != ENOENT) {
        perror("[IPC] open discharge FIFO write");
    }
    return fd;
}

int open_triage_fifo_read(void) {
    int fd = open(FIFO_TRIAGE_PATH, O_RDONLY | O_NONBLOCK);
    if (fd == -1 && errno != ENXIO && errno != ENOENT) {
        perror("[IPC] open triage FIFO read");
    }
    return fd;
}

int open_triage_fifo_read_block(void) {
    int fd = open(FIFO_TRIAGE_PATH, O_RDONLY); 
    if (fd == -1) {
        perror("[IPC] open triage FIFO read (blocking)");
    }
    return fd;
}

int open_triage_fifo_write(void) {
    int fd = open(FIFO_TRIAGE_PATH, O_WRONLY | O_NONBLOCK);
    if (fd == -1 && errno != ENXIO && errno != ENOENT) {
        perror("[IPC] open triage FIFO write");
    }
    return fd;
}
