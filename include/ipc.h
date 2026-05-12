/*
 * ============================================================
 * Project : Prime BedSpace - Hospital Patient Triage & Bed Allocator
 * File    : ipc.h
 * Group   : Group 14
 * Members : Abdul Ahad (24F-0727), Abdul Rahim (24F-0514)
 * Date    : 2026-05-12
 * Purpose : IPC interface declarations — shared memory key, size constants, and FIFO helper prototypes.
 * Compile : gcc -Wall -Wextra -pthread -Iinclude <file> -lrt -lpthread
 * ============================================================
 */
#ifndef IPC_H
#define IPC_H

#define FIFO_DISCHARGE_PATH  "/tmp/discharge_fifo"
#define FIFO_TRIAGE_PATH     "/tmp/triage_fifo"
#define SEM_ICU_NAME         "/sem_icu_limit"
#define SEM_ISOLATION_NAME   "/sem_isolation_limit"

void *init_shared_memory(void);
void  detach_shared_memory(void *ptr);

int open_discharge_fifo_read(void);
int open_discharge_fifo_write(void);
int open_triage_fifo_read(void);        
int open_triage_fifo_read_block(void);  
int open_triage_fifo_write(void);

#endif 
