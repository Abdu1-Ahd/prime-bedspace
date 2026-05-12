#!/bin/bash
# ============================================================
# Project : Prime BedSpace - Hospital Patient Triage & Bed Allocator
# Script  : stress_test.sh
# Group   : Group 14
# Members : Abdul Ahad (24F-0727), Abdul Rahim (24F-0514)
# Date    : 2026-05-12
# Purpose : Spawns 20 randomized patient arrivals in rapid succession to stress test the admission pipeline.
# Usage   : bash scripts/stress_test.sh
# ============================================================
names=("Ali" "Sara" "Umar" "Hina" "Bilal" "Zara" "Hassan" "Nida" "Kamran" "Sana")

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

YELLOW_BOLD='\033[1;33m'
NC='\033[0m'
echo -e "${YELLOW_BOLD}[STRESS] Sending burst of 5 patients (sem_queue saturation demo)...${NC}" >&2

for i in {21..25}; do
    idx=$((RANDOM % 10))
    name="${names[$idx]}"
    age=$((RANDOM % 73 + 18))
    sev=$((RANDOM % 3 + 1))

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

