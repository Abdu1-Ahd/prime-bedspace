#!/bin/bash
# ==============================================================================
# Project: Prime BedSpace
# Script: start_hospital.sh
# Group: Zawiar & Subhani
# Members: Abdul Ahad Zawiar (Abdu1-Ahd), AbdulRahim Subhani (abdulrahim-subh)
# Date: 2026-05-08
# Purpose: Initializes the hospital simulation, sets up IPC components,
#          and launches the main admissions manager.
# Usage: ./start_hospital.sh
# ==============================================================================

if [ -f "/tmp/hospital.pid" ]; then
    PID=$(cat /tmp/hospital.pid)
    echo "[ERROR] Hospital already running. PID: $PID" >&2
    exit 1
fi

if [ ! -f "./build/admissions" ]; then
    echo "[ERROR] Build not found. Run: make all" >&2
    exit 1
fi

# Clean up any stale named semaphores from a previous crash
# (Linux stores POSIX named semaphores in /dev/shm as sem.<name>)
[ -e /dev/shm/sem.sem_icu_limit ]       && rm -f /dev/shm/sem.sem_icu_limit       && echo "[INIT] Removed stale /sem_icu_limit"
[ -e /dev/shm/sem.sem_isolation_limit ] && rm -f /dev/shm/sem.sem_isolation_limit  && echo "[INIT] Removed stale /sem_isolation_limit"

# FIFOs are now created by admissions.c itself (mkfifo with EEXIST ignore).
# Pre-create here too so they exist before admissions opens them.
[ -p "/tmp/discharge_fifo" ] || mkfifo /tmp/discharge_fifo && echo "[INIT] discharge_fifo ready"
[ -p "/tmp/triage_fifo" ]    || mkfifo /tmp/triage_fifo    && echo "[INIT] triage_fifo ready"

STRATEGY="${1:-best}"   # default best; override: ./start_hospital.sh first
./build/admissions --strategy "$STRATEGY" &
PID=$!
echo $PID > /tmp/hospital.pid

BLUE_BOLD='\033[1;34m'
GREEN_BOLD='\033[1;32m'
NC='\033[0m'

echo -e "${BLUE_BOLD}PRIME BEDSPACE HOSPITAL SYSTEM${NC}"
echo -e "┌───────────┬─────────┬──────────────┐"
echo -e "│ WARD      │ BEDS    │ CARE UNITS   │"
echo -e "├───────────┼─────────┼──────────────┤"
echo -e "│ ICU       │  4 beds │ 3 units each │"
echo -e "│ ISOLATION │  4 beds │ 2 units each │"
echo -e "│ GENERAL   │ 12 beds │ 1 unit each  │"
echo -e "└───────────┴─────────┴──────────────┘"

echo -e "${GREEN_BOLD}[OK] Admissions manager started. PID: $PID${NC}"
