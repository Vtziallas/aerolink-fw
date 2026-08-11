# Simulation Test: Backend/Network Outage During an Active Mission

Validates the core architectural claim of this project (see `ARCHITECTURE.md` §4,
`SAFETY.md`): the aircraft must not depend on the ground link once a mission is
airborne. This test simulates total loss of the backend (mission-critical, but not
flight-critical) while PX4 (flight-critical) is executing an uploaded mission.

## SETUP

- PX4 SITL (fixed-wing, SIH) running fresh.
- Ground-station backend running and connected (`GET /api/status` reports
  `vehicle_connected: true`).
- A valid 3-waypoint mission uploaded and started via the normal API
  (`POST /api/missions`, `POST /api/missions/current/start`).
- Confirm via telemetry that the mission is actively executing (armed, flight
  mode `MISSION`, `mission_current` > 0).

## ACTION

1. Once the aircraft is confirmed mid-mission, kill the backend process entirely
   (`pkill -f 'uvicorn app.main:app'`) -- this is a harder failure than a network
   blip: the MAVSDK connection, the WebSocket telemetry stream, and the mission-agent
   process are all gone simultaneously, more like the companion computer disappearing
   than just the LTE link dropping.
2. Wait for the mission's expected flight duration (climb + all waypoint legs +
   landing) with the backend still dead, observing PX4's own log directly
   (`/tmp/px4_sitl.log`) since telemetry is unavailable during this window.
3. Restart the backend and confirm it reconnects.

## EXPECTED RESULT

- PX4 continues executing the uploaded mission with no mode change, no RTL, no
  interruption -- current PX4 parameters have `NAV_DLL_ACT=0` (data-link-loss
  failsafe disabled), so an active mission is not affected by loss of the
  companion/GCS link. This matches `SAFETY.md`'s stated design: LTE/companion
  loss should affect telemetry and remote mission updates, not flight safety.
- The aircraft completes the mission (all waypoints, lands, disarms) entirely
  without the backend present, confirmed via PX4's own log
  (`Mission finished, landed` / `Landing detected` / `Disarmed by landing`).
- On backend restart, it reconnects to the still-running PX4 instance and
  telemetry resumes, reflecting the aircraft's actual current state (e.g.
  landed and disarmed) -- not stale pre-outage data.

## PASS CRITERIA

- [x] PX4 log shows no failsafe/mode-change event attributable to the backend
      being killed.
- [x] Mission reaches `mission_current == mission_total` and lands successfully
      while the backend is confirmed dead the entire time.
- [x] Backend, once restarted, reports `vehicle_connected: true` and streams
      telemetry matching the aircraft's actual (landed/disarmed) state within a
      few seconds -- no manual intervention beyond restarting the process.

## Result

**PASS.** Executed 2026-08-11 against PX4 SITL v1.17.0 (SIH, fixed-wing).

Backend was killed (`pkill -9`) while the aircraft was mid-mission (waypoint
2 of 3, ~27m altitude, armed, mode `MISSION`). PX4's own log, the only
available evidence during the outage, showed:

```
INFO  [commander] Connection to ground station lost
INFO  [commander] Landing detected
INFO  [navigator] Mission finished, landed
INFO  [commander] Disarmed by landing
```

The connection-lost event was logged purely informationally -- no RTL, no
mode switch, no interruption. The mission ran to completion and landed
autonomously with the backend confirmed dead the entire time (current PX4
config has `NAV_DLL_ACT=0`, data-link-loss failsafe disabled, so this is
expected given that setting, not a fluke). On backend restart, `/api/status`
reported `vehicle_connected: true` within seconds and telemetry immediately
reflected the aircraft's real state (`armed: false`, altitude ~0m) rather
than stale pre-outage data.

This empirically confirms the architectural claim in `ARCHITECTURE.md` §4 /
`SAFETY.md`: the backend disappearing does not affect flight safety once a
mission is airborne.
