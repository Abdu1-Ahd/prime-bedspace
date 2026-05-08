#ifndef IPC_H
#define IPC_H

#include "types.h"
#include <sys/types.h>

int open_discharge_fifo_write(void);
int open_discharge_fifo_read(void);
void *init_shared_memory(void);
void detach_shared_memory(void *shmaddr);

#endif /* IPC_H */
