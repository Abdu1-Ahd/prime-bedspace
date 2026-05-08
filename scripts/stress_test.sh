#!/bin/bash
# ==============================================================================
# Project: Prime BedSpace
# Script: stress_test.sh
# Group: Zawiar & Subhani
# Members: Abdul Ahad Zawiar (Abdu1-Ahd), AbdulRahim Subhani (abdulrahim-subh)
# Date: 2026-05-08
# Purpose: Spawns 25 randomized patients routed through the triage FIFO to the
#          receptionist thread. Includes a burst of 5 rapid patients at the end
#          to saturate sem_queue (bounded=20) and demonstrate blocking behaviour.
# Usage: ./stress_test.sh
# ==============================================================================

names=("Ali" "Sara" "Umar" "Hina" "Bilal" "Zara" "Hassan" "Nida" "Kamran" "Sana")

# Phase 1: 20 patients with 200ms spacing
for i in {1..20}; do
    idx=$((RANDOM % 10))
    name="${names[$idx]}"
    age=$((RANDOM % 73 + 18))
    sev=$((RANDOM % 10 + 1))

    echo "[STRESS] Sending patient $i/25 | Name: $name | Severity: $sev" >&2

    if [ -p "/tmp/triage_fifo" ]; then
        ./scripts/triage.sh "$name" "$age" "$sev" > /tmp/triage_fifo
    else
        ./scripts/triage.sh "$name" "$age" "$sev"
    fi

    sleep 0.2
done

# Phase 2: 5 rapid-fire patients to demonstrate sem_queue saturation/blocking
YELLOW_BOLD='\033[1;33m'
NC='\033[0m'
echo -e "${YELLOW_BOLD}[STRESS] Sending burst of 5 patients (sem_queue saturation demo)...${NC}" >&2

for i in {21..25}; do
    idx=$((RANDOM % 10))
    name="${names[$idx]}"
    age=$((RANDOM % 73 + 18))
    sev=$((RANDOM % 3 + 1))  # high severity to force ICU/ISOLATION semaphore wait

    echo "[STRESS] BURST patient $i/25 | Name: $name | Severity: $sev (HIGH)" >&2

    if [ -p "/tmp/triage_fifo" ]; then
        ./scripts/triage.sh "$name" "$age" "$sev" > /tmp/triage_fifo &
    else
        ./scripts/triage.sh "$name" "$age" "$sev"
    fi
done

wait

GREEN_BOLD='\033[1;32m'
echo -e "${GREEN_BOLD}[STRESS] Done. 25 patients dispatched (20 paced + 5 burst).${NC}"

