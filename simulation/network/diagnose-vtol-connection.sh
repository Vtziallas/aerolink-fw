#!/bin/bash
# One-shot, read-only diagnostic for the "backend never discovers the
# vehicle" symptom seen after switching PX4 to sihsim_standard_vtol and
# relaunching aircraft/ground. Checks WireGuard tunnel state, namespace
# addressing, firewall rules, and captures a few seconds of UDP/18570
# traffic on both ends to see whether packets are actually flowing.
#
# Must be run with sudo (ip netns exec requires root). Read-only --
# doesn't change any state, safe to run any time.

set -uo pipefail

echo "=== Network namespaces ==="
ip netns list

echo
echo "=== WireGuard state: aircraft-net ==="
ip netns exec aircraft-net wg show

echo
echo "=== WireGuard state: ground-net ==="
ip netns exec ground-net wg show

echo
echo "=== Interface addresses: aircraft-net ==="
ip netns exec aircraft-net ip -4 addr show

echo
echo "=== Interface addresses: ground-net ==="
ip netns exec ground-net ip -4 addr show

echo
echo "=== iptables (filter table): aircraft-net ==="
ip netns exec aircraft-net iptables -L -n -v

echo
echo "=== iptables (filter table): ground-net ==="
ip netns exec ground-net iptables -L -n -v

echo
echo "=== Capturing 6s of UDP/18570 traffic on aircraft-net (PX4 side) ==="
timeout 6 ip netns exec aircraft-net tcpdump -ni any udp port 18570 -c 20 2>&1 || true

echo
echo "=== Capturing 6s of UDP/18570 traffic on ground-net (backend side) ==="
timeout 6 ip netns exec ground-net tcpdump -ni any udp port 18570 -c 20 2>&1 || true

echo
echo "=== Done ==="
