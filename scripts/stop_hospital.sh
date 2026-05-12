#!/bin/bash
# ============================================================
# Project : Prime BedSpace - Hospital Patient Triage & Bed Allocator
# Script  : stop_hospital.sh
# Group   : Group 14
# Members : Abdul Ahad (24F-0727), Abdul Rahim (24F-0514)
# Date    : 2026-05-12
# Purpose : Sends SIGTERM to admissions, cleans up FIFO and PID file, and prints shutdown summary.
# Usage   : bash scripts/stop_hospital.sh
# ============================================================
if [ ! -f "/tmp/hospital.pid" ]; then
    echo "[ERROR] Hospital not running." >&2
    exit 1
fi

PID=$(cat /tmp/hospital.pid)
kill -TERM "$PID" 2>/dev/null || true

sleep 2

if [ -p "/tmp/discharge_fifo" ]; then
    rm -f /tmp/discharge_fifo
fi

if [ -p "/tmp/triage_fifo" ]; then
    rm -f /tmp/triage_fifo
fi

[ -e /dev/shm/sem.sem_icu_limit ]       && rm -f /dev/shm/sem.sem_icu_limit
[ -e /dev/shm/sem.sem_isolation_limit ] && rm -f /dev/shm/sem.sem_isolation_limit

rm -f /tmp/hospital.pid

YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${YELLOW}[SHUTDOWN] Prime BedSpace stopped.${NC}"
echo -e "${YELLOW}[SHUTDOWN] Shared memory and semaphore cleanup: run ipcrm -a if resources remain.${NC}"
echo -e "${YELLOW}[SHUTDOWN] Check logs/memory_log.txt for session stats.${NC}"
