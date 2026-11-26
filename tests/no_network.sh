#!/bin/sh
# Runs a command with no network but loopback (a network namespace of its
# own), as a church building with no internet (batch 18). Exits 77
# (skipped) where namespaces are not available.
#
# usage: no_network.sh <command> [arguments]
if ! command -v unshare >/dev/null 2>&1 || ! unshare -rn true 2>/dev/null; then
    echo "network namespaces are not available here; skipped"
    exit 77
fi
exec unshare -rn sh -c 'ip link set lo up 2>/dev/null; exec "$@"' sh "$@"
