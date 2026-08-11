#!/bin/bash
# Phase 5: network fault injection on the ground-net <-> aircraft-net link
# set up by setup-netns-wireguard.sh. Applies Linux's netem qdisc to both
# ends of the "internet" veth pair (full-duplex, matching real LTE
# degradation affecting both up- and downlink) -- NOT to the WireGuard
# tunnel interface itself, so this degrades the underlying transport the
# same way a bad cellular link would, independent of anything WireGuard does.
#
# Must be run with sudo.
#
# Usage:
#   inject-fault.sh apply <latency_ms> <loss_pct>
#   inject-fault.sh clear
#
# Examples:
#   sudo bash inject-fault.sh apply 250 0     # 250ms latency, no loss
#   sudo bash inject-fault.sh apply 0 20      # no latency, 20% packet loss
#   sudo bash inject-fault.sh apply 100 10    # both at once
#   sudo bash inject-fault.sh clear           # remove all injected impairment

set -euo pipefail

MODE="${1:-}"

apply_netem() {
  local ns="$1" dev="$2" latency_ms="$3" loss_pct="$4"
  ip netns exec "$ns" tc qdisc del dev "$dev" root 2>/dev/null || true

  local args=()
  [ "$latency_ms" != "0" ] && args+=(delay "${latency_ms}ms")
  [ "$loss_pct" != "0" ] && args+=(loss "${loss_pct}%")

  if [ "${#args[@]}" -eq 0 ]; then
    echo "Nothing to apply on $ns/$dev (both latency and loss are 0)"
    return
  fi

  ip netns exec "$ns" tc qdisc add dev "$dev" root netem "${args[@]}"
  echo "Applied to $ns/$dev: ${args[*]}"
}

case "$MODE" in
  apply)
    LATENCY_MS="${2:?latency_ms required (0 for none)}"
    LOSS_PCT="${3:?loss_pct required (0 for none)}"
    apply_netem ground-net veth-gnet-inet "$LATENCY_MS" "$LOSS_PCT"
    apply_netem aircraft-net veth-anet-inet "$LATENCY_MS" "$LOSS_PCT"
    ;;
  clear)
    ip netns exec ground-net tc qdisc del dev veth-gnet-inet root 2>/dev/null || true
    ip netns exec aircraft-net tc qdisc del dev veth-anet-inet root 2>/dev/null || true
    echo "Cleared impairment on both ends"
    ;;
  *)
    echo "Usage: $0 apply <latency_ms> <loss_pct> | $0 clear" >&2
    exit 1
    ;;
esac

echo "--- current state ---"
ip netns exec ground-net tc qdisc show dev veth-gnet-inet
ip netns exec aircraft-net tc qdisc show dev veth-anet-inet
