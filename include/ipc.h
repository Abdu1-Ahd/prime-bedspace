#ifndef IPC_H
#define IPC_H

/* ── IPC path constants ──────────────────────────────────────────────── */
#define FIFO_DISCHARGE_PATH  "/tmp/discharge_fifo"
#define FIFO_TRIAGE_PATH     "/tmp/triage_fifo"
#define SEM_ICU_NAME         "/sem_icu_limit"
#define SEM_ISOLATION_NAME   "/sem_isolation_limit"

/* ── Shared Memory ───────────────────────────────────────────────────── */
void *init_shared_memory(void);
void  detach_shared_memory(void *ptr);

/* ── FIFO helpers ────────────────────────────────────────────────────── */
int open_discharge_fifo_read(void);
int open_discharge_fifo_write(void);
int open_triage_fifo_read(void);
int open_triage_fifo_write(void);

#endif /* IPC_H */
