#!/usr/bin/env bash
# tools/regression_check.sh — run before every commit
set -euo pipefail

python3 tools/regression_check.py
