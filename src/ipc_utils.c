/**
 * @file ipc_utils.c
 * @brief IPC utilities module
 */
#include "types.h"
#include "ipc.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <string.h>

#define FIFO_PATH "/tmp/discharge_fifo"

int open_discharge_fifo_write(void) {
    // Open for writing. Block if no reader exists unless we use O_NONBLOCK.
    // In this simulation, admissions manager should already have it open for reading.
    int fd = open(FIFO_PATH, O_WRONLY);
    return fd;
}

int open_discharge_fifo_read(void) {
    int fd = open(FIFO_PATH, O_RDONLY | O_NONBLOCK);
    return fd;
}

void *init_shared_memory(void) {
    size_t size = MAX_BEDS * sizeof(BedPartition);
    int shmid = shmget(SHM_KEY, size, IPC_CREAT | 0666);
    if (shmid < 0) {
        perror("shmget failed");
        return NULL;
    }
    
    void *shmaddr = shmat(shmid, NULL, 0);
    if (shmaddr == (void *)-1) {
        perror("shmat failed");
        return NULL;
    }
    return shmaddr;
}

void detach_shared_memory(void *shmaddr) {
    if (shmdt(shmaddr) == -1) {
        perror("shmdt failed");
    }
}
