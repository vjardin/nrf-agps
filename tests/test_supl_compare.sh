#!/bin/sh
# SUPL structural comparison test
#
# Runs a SUPL session against our local server and (if reachable)
# supl.google.com, then validates structural consistency of the
# LPP assistance data using compare_supl_json.py.
#
# Exit codes:
#   0 = pass (or skip due to network)
#   1 = structural mismatch (test failure)
#   2 = infrastructure error
#
# Copyright (C) 2026 Free Mobile
# SPDX-License-Identifier: AGPL-3.0-or-later

set -e

SCRIPT_DIR=$(dirname "$0")
ROOT_DIR=$(cd "$SCRIPT_DIR/.." && pwd)

DB_PATH="/tmp/test_supl_$$.db"
LOCAL_JSON="/tmp/supl_local_$$.json"
GOOGLE_JSON="/tmp/supl_google_$$.json"
SERVER_PID=""
PORT=7276

cleanup() {
	if [ -n "$SERVER_PID" ]; then
		kill "$SERVER_PID" 2>/dev/null || true
		wait "$SERVER_PID" 2>/dev/null || true
	fi
	rm -f "$DB_PATH" "$LOCAL_JSON" "$GOOGLE_JSON"
}
trap cleanup EXIT

export LD_LIBRARY_PATH="$ROOT_DIR/asn1"

# Step 1: Download BRDC and build SQLite DB
echo "=== Downloading BRDC data ==="
if ! "$ROOT_DIR/rinex_dl/rinex_dl" -s "$DB_PATH" 2>&1; then
	echo "(skip: BRDC download failed, network unavailable?)"
	exit 0
fi

# Step 2: Start local SUPL server
echo "=== Starting local SUPL server ==="
"$ROOT_DIR/supl_server" -d "$DB_PATH" --no-tls -p "$PORT" &
SERVER_PID=$!

# Wait for server to be ready
READY=0
for _ in 1 2 3 4 5; do
	if nc -z 127.0.0.1 "$PORT" 2>/dev/null; then
		READY=1
		break
	fi
	sleep 1
done
if [ "$READY" = "0" ]; then
	echo "FAIL: server did not start within 5 seconds"
	exit 2
fi

# Step 3: SUPL session against local server
echo "=== SUPL client -> local server ==="
if ! "$ROOT_DIR/tests/supl_client" -h 127.0.0.1 -p "$PORT" \
	-o "$LOCAL_JSON" 2>&1; then
	echo "FAIL: local SUPL session failed"
	exit 1
fi

if [ ! -s "$LOCAL_JSON" ]; then
	echo "FAIL: local JSON is empty (server returned no LPP data?)"
	exit 1
fi

# Step 4: SUPL session against Google (may fail due to network)
echo "=== SUPL client -> supl.google.com ==="
GOOGLE_OK=0
if "$ROOT_DIR/tests/supl_client" --tls -h supl.google.com \
	-o "$GOOGLE_JSON" 2>&1; then
	if [ -s "$GOOGLE_JSON" ]; then
		GOOGLE_OK=1
	fi
fi

if [ "$GOOGLE_OK" = "0" ]; then
	echo "(skip Google: connection to supl.google.com failed)"
fi

# Step 5: Stop server
kill "$SERVER_PID" 2>/dev/null || true
wait "$SERVER_PID" 2>/dev/null || true
SERVER_PID=""

# Step 6: Structural comparison
echo ""
echo "=== Structural comparison ==="
if [ "$GOOGLE_OK" = "1" ]; then
	python3 "$SCRIPT_DIR/compare_supl_json.py" "$LOCAL_JSON" "$GOOGLE_JSON"
else
	python3 "$SCRIPT_DIR/compare_supl_json.py" "$LOCAL_JSON"
fi
