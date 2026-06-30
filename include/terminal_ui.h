#ifndef TERMINAL_UI_H
#define TERMINAL_UI_H

#include "types.h"

/* Render the current ward state to stdout using ANSI escape codes.
 * Safe to call from any thread — reads ward[] read-only and uses
 * pthread_mutex_trylock on queue_mutex for the queue depth line.     */
void ui_render(BedPartition *ward, int total);

/* Launch a detached 1-second render thread.
 * ward     : pointer to shm_ward[] (stays valid for process lifetime)
 * total    : MAX_BEDS
 * strategy : strategy name string for display ("best"/"first"/"worst") */
void ui_start(BedPartition *ward, int total, const char *strategy);

/* Signal the render thread to stop and wait up to ~1.2s for it to exit. */
void ui_stop(void);

#endif /* TERMINAL_UI_H */

// session:b755b09e
