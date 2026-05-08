#!/bin/bash
# ==============================================================================
# Project: Prime BedSpace
# Script: triage.sh
# Group: Hospital Sim Group
# Members: User
# Date: 2026-05-08
# Purpose: Validates patient triage inputs, assigns priority and care units,
#          and outputs formatted data.
# Usage: ./triage.sh <name> <age> <severity>
# ==============================================================================

# Output usage to stderr and exit
usage() {
    echo "Usage: $0 <name> <age> <severity>" >&2
    exit 1
}

# 1. Argument validation
if [ "$#" -ne 3 ]; then
    usage
fi

NAME="$1"
AGE="$2"
SEVERITY="$3"

# Validate name (non-empty)
if [ -z "$NAME" ]; then
    usage
fi

# Validate age (integer 1-120)
if ! [[ "$AGE" =~ ^[0-9]+$ ]] || [ "$AGE" -lt 1 ] || [ "$AGE" -gt 120 ]; then
    usage
fi

# Validate severity (integer 1-10)
if ! [[ "$SEVERITY" =~ ^[0-9]+$ ]] || [ "$SEVERITY" -lt 1 ] || [ "$SEVERITY" -gt 10 ]; then
    usage
fi

# 2. Priority, Care Units, and Labels Mapping
PRIORITY=0
CARE_UNITS=0
LABEL=""
COLOR=""

RED_BOLD='\033[1;31m'
YELLOW_BOLD='\033[1;33m'
GREEN_BOLD='\033[1;32m'
NC='\033[0m' # No Color

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

# 3. Output logic
PATIENT_ID=$$
ARRIVAL_TIME=$(date +%s)

# Print data to stdout
echo "${PATIENT_ID}|${NAME}|${AGE}|${SEVERITY}|${PRIORITY}|${CARE_UNITS}|${ARRIVAL_TIME}"

# Print formatted summary to stderr
echo -e "${COLOR}[TRIAGE] ${NAME} | Age: ${AGE} | Severity: ${SEVERITY}/10 | Priority: ${PRIORITY} (${LABEL}) | Care Units: ${CARE_UNITS}${NC}" >&2
