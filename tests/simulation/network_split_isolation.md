# Simulation Test: Ground/Aircraft Network Isolation (WireGuard Tunnel)

Validates that the simulated aircraft network (`aircraft-net`) is only reachable
through the WireGuard tunnel, matching `NETWORKING.md`'s core requirement: "a
remote network actor must not be able to obtain aircraft control simply by
discovering an IP address or port." Also validates that the full application
stack (mission upload, execution, telemetry) actually works across the tunnel,
not just that the tunnel handshakes.

## SETUP

- `sudo bash simulation/network/setup-netns-wireguard.sh` run, ending in
  `TUNNEL_OK`.
- `sudo bash simulation/network/launch-aircraft.sh` running (PX4 SITL inside
  `aircraft-net`).
- `sudo bash simulation/network/launch-ground.sh` running (backend inside
  `ground-net`, connected via `udpout://10.99.0.2:18570`).
- `socat TCP-LISTEN:8000,fork,reuseaddr TCP:10.201.0.2:8000` running (frontend
  relay, no sudo required).
- Frontend dev server running natively on Windows.

## ACTION

1. `curl http://127.0.0.1:8000/api/status` from Windows or the default WSL
   namespace (i.e. through the full relay + tunnel path, not directly inside
   either namespace).
2. `curl` (or attempt any TCP/UDP connection) directly to
   `10.200.0.2:18570` -- `aircraft-net`'s "internet"-facing address and PX4's
   MAVLink port -- from outside `aircraft-net`, bypassing the tunnel entirely.
3. Attempt a raw UDP send to `10.200.0.2:51820` (the WireGuard port on that
   same interface) to confirm *something* is reachable there.
4. Through the frontend UI: place waypoints, upload, start the mission, watch
   it fly and land, confirmed via live telemetry.

## EXPECTED RESULT

- Step 1 succeeds (`vehicle_connected: true`) -- the tunnel path works.
- Step 2 fails/times out -- `aircraft-net`'s firewall (`iptables -P INPUT
  DROP` plus an explicit accept only for UDP 51820) drops everything on that
  interface except WireGuard traffic. Raw MAVLink is never exposed.
- Step 3 succeeds -- confirms the block in step 2 is a deliberate, narrow
  firewall rule, not e.g. PX4 simply not running or the network being down
  entirely.
- Step 4 completes exactly as it does in the same-machine (Phase 2/3)
  topology -- proving the network split didn't silently degrade
  functionality, only added a real boundary.

## PASS CRITERIA

- [x] `/api/status` reachable through the full relay+tunnel path and reports
      `vehicle_connected: true`.
- [x] Direct connection attempt to PX4's MAVLink port on `aircraft-net`'s
      internet-facing address is blocked.
- [x] WireGuard's own port on that same address is reachable, confirming the
      block is a targeted firewall rule.
- [x] Full mission (upload, validate, fly, land) and live WebSocket telemetry
      work correctly through the tunnel, matching same-machine behavior.

## Result

**PASS.** Executed 2026-08-11 against PX4 SITL v1.17.0 (SIH, fixed-wing) in
`aircraft-net`, backend in `ground-net`, connected via a direct 2-peer
WireGuard tunnel (see `NETWORKING.md`'s Phase 4 Implementation section for
topology and the PX4 MAVLink-instance finding that made this work).

```
=== 1. Backend reachable via localhost (through relay+tunnel) ===
{"vehicle_connected":true,"system_address":"udpout://10.99.0.2:18570"}
=== 2. Raw MAVLink NOT reachable directly on aircraft internet-facing IP ===
GOOD: blocked
=== 3. WireGuard UDP port IS reachable on that same IP ===
GOOD: WireGuard port open
```

A full mission (3 waypoints, ~200-300m legs) was also flown end-to-end through
the tunnel via the real frontend: uploaded, validated, executed (climb,
waypoints, landing), and disarmed, with live WebSocket telemetry visible
throughout -- confirmed via both the UI and PX4's own log (`Mission finished,
landed` / `Disarmed by landing`).
