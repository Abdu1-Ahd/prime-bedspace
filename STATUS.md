# Prime BedSpace - Project Status

## 1. Completed

| Feature / Module | File | Details |
| :--- | :--- | :--- |
| **Triage Logic** | `scripts/triage.sh` | Implements argument validation, severity mapping (1-10), care unit allocation, and ANSI colorized output. |
| **Stress Tester** | `scripts/stress_test.sh` | Loop that randomly generates 20 patient profiles (names, ages, severities) and pipes them to the FIFO. |
| **Hospital Scripts** | `scripts/start_hospital.sh`, `scripts/stop_hospital.sh` | Initializes FIFO `/tmp/discharge_fifo`, manages `admissions` PID, prints ward structure, and provides shutdown/cleanup. |
| **Patient Simulator** | `src/patient_simulator.c` | Implements `main` with argument parsing, dynamic `nanosleep()` durations based on bed type, and FIFO write for discharge. |
| **Scheduler Priority Queue** | `src/scheduler.c` | Thread-safe Min-Heap implementation: `pq_init`, `pq_push`, `pq_pop`, `pq_is_empty`, `pq_size`, and a built-in `scheduler_self_test`. |
| **Build System** | `Makefile` | Multi-target build (`admissions`, `patient_simulator`), with header resolution (`-Iinclude`), `clean`, `run`, `test`, and `valgrind` commands. |

## 2. Partially Done

| Feature / Module | File | Specific Missing/Incomplete Parts |
| :--- | :--- | :--- |
| **Admissions Manager** | `src/admissions.c` | Core loop, `fork`/`exec` structure, `sigchld` reaping, and FIFO reading (`strcspn`) are implemented. **Incomplete:** `admit_patient()` (Lines 52-104) does not actually queue patients when full—it prints "Queuing" but discards them. |
| **IPC Utilities** | `src/ipc_utils.c` | Contains only a placeholder `ipc_stub()`. The actual implementations for `open_discharge_fifo_write()`, `open_discharge_fifo_read()`, `init_shared_memory()`, and `detach_shared_memory()` are missing. |
| **Header Files** | `include/types.h`, `include/scheduler.h` | Core structs (`PatientRecord`, `BedPartition`, `PriorityQueue`) are defined, but `include/ipc.h` and `include/bed_allocator.h` are empty. |

## 3. Not Started

- **Phase 2C (Completion):** Integration of the `scheduler.c` Min-Heap into `admissions.c` so patients are properly queued and dispatched based on `priority` rather than being dropped.
- **Phase 3 (Dynamic Bed Allocation):** Implementation of `src/bed_allocator.c` and true System V Shared Memory management via `shmget` / `shmat` (instead of mocked memory allocations).
- **Phase 4 (System Integration & Terminal UI):** Implementation of `src/terminal_ui.c` for real-time visualization of bed states, complete IPC named semaphore integration, and final system-wide stress tests tracking statistics.

## 4. Known Issues

- **Build Failure (Undefined References):** The project currently fails to link because `src/admissions.c` and `src/patient_simulator.c` call functions (`open_discharge_fifo_write`, `open_discharge_fifo_read`, `init_shared_memory`, `detach_shared_memory`) that do not exist in `ipc_utils.c`.
- **Missing Semaphores:** `scripts/start_hospital.sh` contains a TODO on line 24: `Create named semaphores using a small C helper once available`.
- **Environment Compatibility:** Development environment is Windows PowerShell, causing `make` failures locally (requires Linux/WSL for testing the POSIX APIs).

## 5. Commit History Summary

```text
b5424a7 Merge pull request #2 from Abdu1-Ahd/chore/update-identity-placeholders
671af8f chore: replace identity placeholders with actual GitHub accounts
b616ec7 fix: add -Iinclude flag to CFLAGS for header resolution (#1)
af358e9 feat(phase-2a): implement admissions core with fork/exec and SIGCHLD handler
be538f9 feat(phase-2a): implement patient_simulator with nanosleep and FIFO discharge
6a6e6dc feat(phase-1): implement stress_test.sh and complete Makefile
0f70d1f feat(phase-1): implement start_hospital.sh and stop_hospital.sh
3df0bf6 feat(phase-1): implement triage.sh with priority mapping and ANSI colors
c42ca0b chore: init workspace scaffold and AGENTS.md
```
