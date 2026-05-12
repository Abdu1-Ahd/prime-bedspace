#!/bin/bash
# ============================================================
# Project : Prime BedSpace - Hospital Patient Triage & Bed Allocator
# Script  : triage.sh
# Group   : Group 14
# Members : Abdul Ahad (24F-0727), Abdul Rahim (24F-0514)
# Date    : 2026-05-12
# Purpose : Validates patient input, maps severity to triage priority, and pipes formatted patient record to admissions.
# Usage   : bash scripts/triage.sh <name> <age> <severity 1-10>
# ============================================================
usage() {
    echo "Usage: $0 <name> <age> <severity>" >&2
    exit 1
}

if [ "$
    usage
fi

NAME="$1"
AGE="$2"
SEVERITY="$3"

if [ -z "$NAME" ]; then
    usage
fi

if ! [[ "$AGE" =~ ^[0-9]+$ ]] || [ "$AGE" -lt 1 ] || [ "$AGE" -gt 120 ]; then
    usage
fi

if ! [[ "$SEVERITY" =~ ^[0-9]+$ ]] || [ "$SEVERITY" -lt 1 ] || [ "$SEVERITY" -gt 10 ]; then
    usage
fi

PRIORITY=0
CARE_UNITS=0
LABEL=""
COLOR=""

RED_BOLD='\033[1;31m'
YELLOW_BOLD='\033[1;33m'
GREEN_BOLD='\033[1;32m'
NC='\033[0m'

if [ "$SEVERITY" -le 2 ]; then
    PRIORITY=1
    CARE_UNITS=3
    LABEL="CRITICAL"
    COLOR=$RED_BOLD
elif [ "$SEVERITY" -le 4 ]; then
    PRIORITY=2
    CARE_UNITS=3
    LABEL="HIGH"
    COLOR=$RED_BOLD
elif [ "$SEVERITY" -le 6 ]; then
    PRIORITY=3
    CARE_UNITS=2
    LABEL="MODERATE"
    COLOR=$YELLOW_BOLD
elif [ "$SEVERITY" -le 8 ]; then
    PRIORITY=4
    CARE_UNITS=1
    LABEL="LOW"
    COLOR=$GREEN_BOLD
else
    PRIORITY=5
    CARE_UNITS=1
    LABEL="MINIMAL"
    COLOR=$GREEN_BOLD
fi

PATIENT_ID=$$
ARRIVAL_TIME=$(date +%s)

echo "${PATIENT_ID}|${NAME}|${AGE}|${SEVERITY}|${PRIORITY}|${CARE_UNITS}|${ARRIVAL_TIME}"

echo -e "${COLOR}[TRIAGE] ${NAME} | Age: ${AGE} | Severity: ${SEVERITY}/10 | Priority: ${PRIORITY} (${LABEL}) | Care Units: ${CARE_UNITS}${NC}" >&2
