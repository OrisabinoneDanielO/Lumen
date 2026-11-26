#!/bin/sh
# Batch 12 offline check (Linux): Lumen makes no network connections.
#
# Runs the real app with its audience output for a few seconds, with an
# empty library in a temporary home, under strace, and fails if either
# process (the app or lumen-render) opens or sends to an internet socket
# (AF_INET/AF_INET6). Local sockets (the render link, D-Bus, the display)
# are fine. Then runs it again with no network at all (a network namespace
# of its own) to show it starts and quits normally offline.
#
# usage: offline_check.sh <path to lumen>
# Exits 77 (skipped) where strace or unshare is missing.
set -u
app=$1
command -v strace >/dev/null 2>&1 || { echo "strace is not installed; skipped"; exit 77; }

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
export HOME="$tmp" XDG_DATA_HOME="$tmp/data" XDG_CONFIG_HOME="$tmp/config" XDG_CACHE_HOME="$tmp/cache"
export QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software
server="lumen-offline-$$"

strace -f -qq -e trace=connect,sendto,sendmsg -o "$tmp/trace" \
    "$app" --output --render-server="$server" --render-option=--windowed --quit-after=8000 >"$tmp/log" 2>&1
status=$?
if [ "$status" -ne 0 ]; then
    echo "lumen exited with $status"; cat "$tmp/log"; exit 1
fi
processes=$(cut -d' ' -f1 "$tmp/trace" | sort -u | wc -l)
if ! grep -q "$server" "$tmp/trace"; then
    echo "the render process was never reached"; cat "$tmp/log"; exit 1
fi
if grep -E 'AF_INET6?' "$tmp/trace"; then
    echo "FAIL: network connections above"; exit 1
fi
echo "no network connections ($processes processes traced)"
if pgrep -f "$server" >/dev/null; then
    echo "FAIL: lumen-render is still running after lumen quit"; pkill -f "$server"; exit 1
fi

# With no network at all.
if command -v unshare >/dev/null 2>&1 && unshare -rn true 2>/dev/null; then
    unshare -rn "$app" --output --render-server="$server-2" --render-option=--windowed --quit-after=5000 >"$tmp/log2" 2>&1
    status=$?
    if [ "$status" -ne 0 ]; then
        echo "FAIL: without a network lumen exited with $status"; cat "$tmp/log2"; exit 1
    fi
    echo "runs with no network"
else
    echo "unshare not available; the no-network run was skipped"
fi
exit 0
