# Simulation Test: Full Link Interruption and Reconnection

The hardest of the Phase 5 network tests: fully severs the ground/aircraft
link (100% packet loss, not just degraded) mid-flight -- distinct from
`network_outage_during_mission.md`, which killed the *backend process*
entirely. Here both processes stay alive; only the network path between them
is cut, closer to what a real LTE dead zone looks like. Also tests that the
system recovers correctly once the link returns, not just that it survives
while it's down.

## SETUP

- Network split + WireGuard tunnel up, PX4 and backend both running
  (`tests/simulation/network_split_isolation.md`).

## ACTION

1. Upload and start a 3-waypoint mission.
2. Within a few seconds of starting (confirmed mid-flight via PX4's log),
   `sudo bash simulation/network/inject-fault.sh apply 0 100` -- full link
   interruption.
3. While the link is down: attempt an RTL command through the normal API,
   confirm what happens.
4. Let the mission continue running with the link still down.
5. `sudo bash simulation/network/inject-fault.sh clear` to restore the link.
6. Confirm the backend automatically resyncs without needing a restart, then
   send a real RTL command and confirm it now succeeds.

## EXPECTED RESULT

- The aircraft continues its mission autonomously with the link fully down,
  exactly as in the backend-kill test -- confirming the same architectural
  property holds for "signal lost" as for "companion computer gone".
- Commands sent while the link is down genuinely fail (timeout), not just
  slowly succeed -- proving the aircraft is actually unreachable, not just
  degraded.
- Once the link is restored, the backend's existing connection recovers on
  its own (no restart needed) and commands work again.

## PASS CRITERIA

- [x] Mission continues after the link is cut mid-flight (confirmed via
      PX4's log, not telemetry, since telemetry is unavailable during the
      outage by design).
- [x] A command sent while the link is down genuinely times out rather than
      eventually succeeding.
- [x] Once the link is restored, the backend automatically resyncs (no
      process restart) and a subsequent command succeeds.
- [x] The aircraft ends the test landed and disarmed, not crashed or lost.

## Result

**PASS.** Executed 2026-08-11.

PX4's log across the full sequence:

```
INFO  [navigator] Climb to 30.0 meters above home
INFO  [commander] Takeoff detected
INFO  [commander] Connection to ground station lost      <- link cut, mid-flight
INFO  [navigator] Holding at 30 m above landing waypoint. <- unrelated flight-dynamics
                                                              landing-abort, see below
INFO  [commander] GCS connection regained                <- link restored
INFO  [navigator] Executing Mission
INFO  [navigator] RTL Mission land: climb to 592 m
...
INFO  [commander] Landing detected
INFO  [navigator] Mission finished, landed
INFO  [commander] Disarmed by landing
```

A command (`/api/emergency/rtl`) sent while the link was down returned a real
MAVSDK-level timeout (`TIMEOUT: 'Timeout'; origin: return_to_launch()`), not
a slow success -- confirmed the aircraft was genuinely unreachable. Once the
link was restored, `/api/status` responded correctly without any backend
restart, and a subsequent RTL call succeeded immediately.

**Notable, separately-tracked finding:** during this test (and twice more
during other Phase 5 tests today), PX4 autonomously aborted its landing
approach and looped through hold/retry a few times before eventually landing
successfully. Traced to source: the trigger is
`Navigator::abort_landing()` in `navigator_main.cpp`, which reads
`landing_status.abort_status` from the fixed-wing position controller's own
glide-slope/alignment assessment -- confirmed to have no dependency on
network or GCS state whatsoever. This is a real flight-dynamics/landing-
approach-geometry characteristic worth investigating separately (our
auto-appended landing point's fixed approach geometry may be marginal for
this controller's abort thresholds), but it is **not** a network-fault-
injection issue, and it did not prevent a safe outcome in any of the three
occurrences -- the aircraft held safely and eventually landed each time.
