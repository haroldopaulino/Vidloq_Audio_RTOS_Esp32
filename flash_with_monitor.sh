#!/usr/bin/env bash
set -euo pipefail
. ~/esp/esp-idf-v5.5.2/export.sh
python3 startup.py --monitor --deep
