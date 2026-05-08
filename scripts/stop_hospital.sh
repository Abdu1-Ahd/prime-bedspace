#!/bin/bash
# ==============================================================================
# Project: Prime BedSpace
# Script: stop_hospital.sh
# Group: <Group XX>
# Members: <Member 1>, <Member 2>
# Date: 2026-05-08
# Purpose: Safely terminates the hospital simulation and cleans up 
#          IPC resources (FIFOs, PID files).
# Usage: ./stop_hospital.sh
# ==============================================================================

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

rm -f /tmp/hospital.pid

YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${YELLOW}[SHUTDOWN] Prime BedSpace stopped.${NC}"
echo -e "${YELLOW}[SHUTDOWN] Shared memory and semaphore cleanup: run ipcrm -a if resources remain.${NC}"
echo -e "${YELLOW}[SHUTDOWN] Check logs/memory_log.txt for session stats.${NC}"
