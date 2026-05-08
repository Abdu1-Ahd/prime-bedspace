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

# TODO: Create named semaphores using a small C helper once available

if [ ! -p "/tmp/discharge_fifo" ]; then
    mkfifo /tmp/discharge_fifo
fi

./build/admissions &
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
