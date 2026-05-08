#!/bin/bash
# ==============================================================================
# Project: Prime BedSpace
# Script: stress_test.sh
# Group: Zawiar & Subhani
# Members: Abdul Ahad Zawiar (Abdu1-Ahd), AbdulRahim Subhani (abdulrahim-subh)
# Date: 2026-05-08
# Purpose: Spawns 20 randomized patients and routes them to the hospital
#          admissions queue (or terminal) for stress testing.
# Usage: ./stress_test.sh
# ==============================================================================

names=("Ali" "Sara" "Umar" "Hina" "Bilal" "Zara" "Hassan" "Nida" "Kamran" "Sana")

for i in {1..20}; do
    idx=$((RANDOM % 10))
    name="${names[$idx]}"
    age=$((RANDOM % 73 + 18))
    sev=$((RANDOM % 10 + 1))

    echo "[STRESS] Sending patient $i/20 | Name: $name | Severity: $sev" >&2

    if [ -p "/tmp/discharge_fifo" ]; then
        ./scripts/triage.sh "$name" "$age" "$sev" > /tmp/discharge_fifo
    else
        ./scripts/triage.sh "$name" "$age" "$sev"
    fi

    sleep 0.2
done

GREEN_BOLD='\033[1;32m'
NC='\033[0m'
echo -e "${GREEN_BOLD}[STRESS] Done. 20 patients dispatched.${NC}"
