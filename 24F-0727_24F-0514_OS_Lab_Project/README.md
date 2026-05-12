# Prime BedSpace

![CI/CD](https://img.shields.io/badge/CI%2FCD-Active-success?style=flat-square) ![Language](https://img.shields.io/badge/Language-C-blue?style=flat-square) ![License](https://img.shields.io/badge/License-MIT-green?style=flat-square)

Prime BedSpace is a C-based hospital admission and bed allocation simulator. It manages real-time patient triage, concurrent resource scheduling, and inter-process communication using named pipes and shared memory. Designed for healthcare IT administrators and OS engineers to simulate capacity constraints and triage routing.

## Features

| Feature Module | Underlying Mechanism | System Benefit |
| --- | --- | --- |
| Patient Triage | Shell-based logic mapping severities | Rapid routing of critical patients |
| Concurrency Simulator | POSIX `fork()` and `execv()` | Realistic load simulation |
| IPC Discharge | Named FIFOs (`mkfifo`) | Non-blocking inter-process signaling |

## Prerequisites

| Software | Required Version | Installation Source |
| --- | --- | --- |
| GCC | >= 9.0 | package manager |
| Make | >= 4.0 | package manager |
| Git Bash (Windows) | Latest | gitforwindows.org |

## Installation

```bash
git clone https://github.com/Abdu1-Ahd/prime-bedspace.git
cd prime-bedspace
make all
```

## Quickstart

```bash
# Start the hospital admissions manager
bash scripts/start_hospital.sh

# Run a stress test of 20 randomized patients
bash scripts/stress_test.sh

# Stop the hospital and clean up IPC
bash scripts/stop_hospital.sh
```

## Key Design Decisions

| Decision | Choice | Rationale |
| --- | --- | --- |
| Process Model | Multi-process over multi-thread | Ensures strict isolation between patient instances |
| IPC Method | Named FIFOs (Pipes) | Reliable, unidirectional signaling for patient discharge |

## Limitations

- Methodological limitations: Bed allocation logic relies on hardcoded partitions rather than dynamic graph allocation.
- Scope exclusions: Does not simulate staff scheduling or consumable resources.

## Contributing

See [.github/CONTRIBUTING.md](.github/CONTRIBUTING.md)

## License

[MIT](LICENSE)
