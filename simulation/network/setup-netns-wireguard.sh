#!/bin/bash
# Phase 4: simulated ground/aircraft network split.
#
# Creates two Linux network namespaces -- ground-net (where the backend
# runs) and aircraft-net (where PX4 SITL runs) -- connected only by a
# WireGuard tunnel over a direct veth link standing in for "the internet".
# A third veth pair connects ground-net back to the default WSL namespace
# so the Windows-native frontend can still reach the backend.
#
#   [Windows: frontend]
#          |  (WSL localhost forwarding)
#   [default netns] --veth(10.201.0.0/24)-- [ground-net: backend]
#                                                  |
#                                          WireGuard tunnel (10.99.0.0/24)
#                                          over veth "internet" link (10.200.0.0/24)
#                                                  |
#                                          [aircraft-net: PX4 SITL]
#
# aircraft-net's "internet"-facing interface only accepts WireGuard traffic
# (UDP 51820) -- no raw MAVLink reachable from outside the tunnel, matching
# NETWORKING.md's "never expose unauthenticated MAVLink" requirement.
#
# Must be run with sudo. Safe to re-run (tears down prior state first).

set -euo pipefail

KEY_DIR="/tmp/aerolink-wg-keys"

echo "[1/6] Tearing down any previous setup..."
ip netns del ground-net 2>/dev/null || true
ip netns del aircraft-net 2>/dev/null || true
ip link del veth-gnet-host 2>/dev/null || true
rm -rf "$KEY_DIR"
mkdir -p "$KEY_DIR"

echo "[2/6] Creating network namespaces..."
ip netns add ground-net
ip netns add aircraft-net

echo "[3/6] Wiring ground-net <-> aircraft-net (the simulated 'internet' link)..."
ip link add veth-gnet-inet type veth peer name veth-anet-inet
ip link set veth-gnet-inet netns ground-net
ip link set veth-anet-inet netns aircraft-net
ip netns exec ground-net ip addr add 10.200.0.1/24 dev veth-gnet-inet
ip netns exec aircraft-net ip addr add 10.200.0.2/24 dev veth-anet-inet
ip netns exec ground-net ip link set veth-gnet-inet up
ip netns exec aircraft-net ip link set veth-anet-inet up
ip netns exec ground-net ip link set lo up
ip netns exec aircraft-net ip link set lo up

echo "[4/6] Wiring ground-net <-> default namespace (so the frontend can reach the backend)..."
ip link add veth-gnet-host type veth peer name veth-gnet-ns
ip link set veth-gnet-ns netns ground-net
ip addr add 10.201.0.1/24 dev veth-gnet-host
ip netns exec ground-net ip addr add 10.201.0.2/24 dev veth-gnet-ns
ip link set veth-gnet-host up
ip netns exec ground-net ip link set veth-gnet-ns up

echo "[5/6] Setting up WireGuard tunnel..."
umask 077
wg genkey | tee "$KEY_DIR/ground_private" | wg pubkey > "$KEY_DIR/ground_public"
wg genkey | tee "$KEY_DIR/aircraft_private" | wg pubkey > "$KEY_DIR/aircraft_public"

ip netns exec ground-net ip link add wg0 type wireguard
ip netns exec ground-net wg set wg0 \
  private-key "$KEY_DIR/ground_private" \
  listen-port 51820 \
  peer "$(cat "$KEY_DIR/aircraft_public")" \
  endpoint 10.200.0.2:51820 \
  allowed-ips 10.99.0.2/32
ip netns exec ground-net ip addr add 10.99.0.1/24 dev wg0
ip netns exec ground-net ip link set wg0 up

ip netns exec aircraft-net ip link add wg0 type wireguard
ip netns exec aircraft-net wg set wg0 \
  private-key "$KEY_DIR/aircraft_private" \
  listen-port 51820 \
  peer "$(cat "$KEY_DIR/ground_public")" \
  endpoint 10.200.0.1:51820 \
  allowed-ips 10.99.0.1/32
ip netns exec aircraft-net ip addr add 10.99.0.2/24 dev wg0
ip netns exec aircraft-net ip link set wg0 up

echo "[6/6] Locking down aircraft-net's internet-facing interface to WireGuard only..."
ip netns exec aircraft-net iptables -F
ip netns exec aircraft-net iptables -P INPUT DROP
ip netns exec aircraft-net iptables -A INPUT -i lo -j ACCEPT
ip netns exec aircraft-net iptables -A INPUT -i wg0 -j ACCEPT
ip netns exec aircraft-net iptables -A INPUT -i veth-anet-inet -p udp --dport 51820 -j ACCEPT
ip netns exec aircraft-net iptables -A INPUT -i veth-anet-inet -j DROP

echo "Done. Verifying tunnel with a ping..."
ip netns exec ground-net ping -c 2 -W 2 10.99.0.2 && echo "TUNNEL_OK" || echo "TUNNEL_FAILED"
