# Simulation Test: Mission Agent End-to-End Integration

Validates the real Mission Agent (`onboard/mission-agent/`, see
`docs/architecture/mission-agent-design.md`) working end-to-end, replacing the
backend's former direct MAVSDK connection to PX4. This is the first time the
full architecture in `ARCHITECTURE.md`'s system diagram has actually existed
as running code rather than a design -- Frontend/curl -> Backend -> WireGuard
tunnel -> Mission Agent -> MAVSDK (local link) -> PX4.

## SETUP

- `sudo bash simulation/network/setup-netns-wireguard.sh` run fresh (the
  namespaces from earlier phases' testing didn't survive an intervening WSL
  VM restart, unrelated to this change).
- `sudo bash simulation/network/launch-aircraft.sh` -- launches PX4 SITL
  *and* the `mission_agent` binary together inside `aircraft-net`, the agent
  connecting to PX4's local onboard link (`udp://:14540`).
- `sudo bash simulation/network/launch-ground.sh` -- launches the backend
  inside `ground-net`, now connecting to the Mission Agent's TCP/JSON
  protocol at `10.99.0.2:5760` across the WireGuard tunnel instead of
  speaking MAVLink directly.

## ACTION

1. `curl http://<ground-net-backend>:8000/api/status`.
2. Upload a 2-waypoint mission via `POST /api/missions`, then
   `POST /api/missions/current/start`.
3. Watch the mission fly to completion, cross-checking three independent
   log sources: PX4's own log, the Mission Agent's own log (real MAVSDK
   callbacks, not the backend's view of them), and the backend's log (the
   Python `MissionAgentClient`'s view of the ack chain).

## EXPECTED RESULT

- Step 1 reports `vehicle_connected: true` and `mission_agent_address`
  pointing at the tunnel address -- confirms the backend's `VehicleConnection`
  successfully connected to the Mission Agent, not to PX4 directly (there is
  no longer any MAVSDK connection inside the backend process at all).
- Step 2's requests return `200 OK`, meaning the full `RECEIVED -> VALIDATED
  -> ACCEPTED -> PX4_ACTION_STARTED` ack chain completed without the
  backend's `MissionAgentClient` raising (a `REJECTED` or a connection
  failure would have surfaced as a `RuntimeError` -> `422` from `main.py`).
- Step 3: PX4's log shows a normal full flight lifecycle culminating in
  `Mission finished, landed` / `Disarmed by landing`; the Mission Agent's
  own log shows `mission_impl.cpp`'s real `current`/`total` progress
  advancing 0 -> 3 (2 waypoints + the auto-appended landing item) as its
  MAVSDK subscription actually fires, independent of anything the backend
  reports.

## PASS CRITERIA

- [x] `/api/status` reports `vehicle_connected: true` via the Mission Agent
      connection, with no MAVSDK dependency left in the backend process.
- [x] Mission upload and start both return `200 OK` with no rejection.
- [x] PX4's own log confirms a real completed flight (takeoff, landing,
      disarm).
- [x] The Mission Agent's own log independently confirms real mission
      progress via its own MAVSDK subscription (not just relaying whatever
      the backend claims).

## Result

**PASS.** Executed 2026-08-18 against PX4 SITL v1.17.0 (SIH, `sihsim_standard_vtol`)
in `aircraft-net`, the real `mission_agent` C++ binary alongside it in the
same namespace, backend in `ground-net`, connected across the same 2-peer
WireGuard tunnel used throughout this project -- just now carrying the
Mission Agent's TCP/JSON protocol instead of raw MAVLink.

```
=== /api/status ===
{"vehicle_connected":true,"mission_agent_address":"10.99.0.2:5760"}

=== Backend log ===
INFO:app.vehicle:Connecting to mission agent at 10.99.0.2:5760
INFO:app.vehicle:Mission agent connected
INFO:     10.201.0.1:43906 - "POST /api/missions HTTP/1.1" 200 OK
INFO:     10.201.0.1:43912 - "POST /api/missions/current/start HTTP/1.1" 200 OK

=== Mission Agent log ===
mission_agent: connected to PX4
mission_agent: serving on port 5760
[Debug] current: 0, total: 3 (mission_impl.cpp:1010)
[Debug] current: 1, total: 3 (mission_impl.cpp:1010)
[Debug] current: 2, total: 3 (mission_impl.cpp:1010)
[Debug] current: 3, total: 3 (mission_impl.cpp:1010)

=== PX4 log ===
INFO  [commander] Takeoff detected
INFO  [commander] Landing detected
INFO  [navigator] Mission finished, landed
INFO  [commander] Disarmed by landing
```

Three independent sources (PX4's own flight-control log, the Mission
Agent's own MAVSDK callback log, and the backend's HTTP-level ack-chain
log) all agree the mission genuinely flew and completed -- this isn't one
process's self-report being trusted at face value.

## Findings during this test (real, not anticipated in the design)

- **A `git merge --ff-only` on this Windows/WSL checkout silently corrupted
  three shell scripts with CRLF line endings**, breaking bash's
  shebang/blank-line parsing (`$'\r': command not found`) the first time
  they were actually executed after being edited in this branch. The
  *committed* content was correct LF all along -- only the working-tree copy
  was affected by autocrlf on checkout/merge. Fixed by normalizing the
  working tree and adding `.gitattributes` (`*.sh text eol=lf`,
  `*.py text eol=lf`) so this can't recur on a future checkout/merge on this
  machine. This is a real, project-wide gotcha worth remembering: **any**
  shell script in this repo was silently vulnerable to this before the
  `.gitattributes` fix landed, not just the three scripts this task happened
  to touch.
- **`mission_agent`'s own stdout is block-buffered when redirected to a log
  file**, the same class of issue found earlier in this project with
  Python's `print()` (see `SIMULATION.md`'s VTOL debugging history) -- the
  `mission_agent: connected to PX4` / `serving on port 5760` lines didn't
  appear in the log file until well after the process was actually
  functional (confirmed independently via `/proc/<pid>/net/tcp` showing the
  listening socket before the log caught up). Not a defect worth fixing in
  the binary itself for this project's scope, but worth knowing if this log
  is ever watched live for a "ready" signal -- polling the log file alone
  can lag reality by tens of seconds.
- The network namespaces (`aircraft-net`/`ground-net`) from earlier phases'
  testing did not survive an intervening WSL VM restart and had to be
  recreated via `setup-netns-wireguard.sh` -- consistent with this being
  session-scoped Linux kernel state, not something `git`/the filesystem
  persists across a VM restart. Not a new finding, just re-confirming
  `NETWORKING.md`'s existing framing of the netns setup as needing to be
  redone per WSL session.

## Live re-verification after the final-review fix wave (2026-08-20)

The final whole-branch review (see the design doc's commit history / SDD
ledger) found real gaps the per-task reviews missed and flagged two of the
fixes -- the 8 restored telemetry fields (`absolute_altitude_m`, roll/pitch/yaw,
airspeed/groundspeed/heading, battery voltage) and the backend's new
reconnect-after-drop logic -- as compile/unit-test verified only, with a
specific recommendation to confirm both against live PX4 before treating them
as done. Re-ran the setup and confirmed both directly:

**Telemetry fields:** flew another mission, captured a live frame off
`/ws/telemetry` mid-mission. All 8 previously-null fields came back with real,
non-zero, plausible values -- e.g. `absolute_altitude_m: 489.84`,
`airspeed_m_s: 0.098`, `groundspeed_m_s: 0.052`, `battery_voltage_v: 16.2`.
This specifically closes the flagged residual risk that `fixedwing_metrics()`
might read zero while a QuadPlane VTOL is in multicopter/hover mode -- it
didn't; the data populated correctly.

**Reconnect behavior:** force-killed the backend process mid-session and
confirmed via `ps aux` that the Mission Agent kept running (the SIGPIPE fix
holding up under a real client disconnect, not just the unit test's simulated
one). Relaunched the backend and confirmed it reconnected cleanly
(`/api/status` back to `vehicle_connected: true`) with no manual intervention
beyond the normal launch command.

**Not tested this pass, left as a genuine follow-up:** a silent, sustained
100%-packet-loss outage (both processes alive, no clean disconnect) rather
than a process kill -- per the final review's own analysis, this would fall
back to TCP's own retransmission timeout (`tcp_retries2`, ~15 min) before the
reconnect logic even notices, since neither peer ever sees EOF or an error.
`tests/simulation/link_interruption_reconnection.md`'s original pass criteria
should still hold (TCP itself carries a short outage), but a true long silent
outage isn't covered by anything built in this branch and would need an
application-level heartbeat to detect faster -- worth a future test/fix pass,
not a blocker for this one.
