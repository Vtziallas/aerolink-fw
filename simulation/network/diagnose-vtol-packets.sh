#!/bin/bash
# Follow-up to diagnose-vtol-connection.sh: that confirmed the WireGuard
# tunnel and UDP path are healthy (packets flow both ways), but the backend
# still never reports "System discovered". This captures the raw bytes of a
# few packets so we can check whether they're well-formed MAVLink v2 frames
# (and see the actual sysid/compid/msgid), instead of guessing.
#
# Must be run with sudo (ip netns exec requires root). Read-only.

set -uo pipefail

echo "=== Raw hex of first 5 packets PX4 sends (aircraft-net side) ==="
timeout 5 ip netns exec aircraft-net tcpdump -ni wg0 -X udp port 18570 -c 5 2>&1

echo
echo "=== ss: sockets bound to udpout ephemeral port, both namespaces ==="
echo "--- aircraft-net ---"
ip netns exec aircraft-net ss -uapn
echo "--- ground-net ---"
ip netns exec ground-net ss -uapn
