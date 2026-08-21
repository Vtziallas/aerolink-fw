# Simulation Test: Companion-Service (Mission Agent) Crash and Restart

Validates the core safety property behind splitting flight control (PX4) from
the companion computer (Mission Agent): a crash or restart of the companion
process must not affect an in-progress flight, and the ground link must
recover on its own once the companion process comes back -- no PX4 failsafe
trigger, no backend restart required. This has been listed as `[ ]` (blocked
on infrastructure) in `SIMULATION.md`'s status section since Phase 5.

## SETUP

- `sudo bash simulation/network/setup-netns-wireguard.sh` run, ending in
  `TUNNEL_OK`.
- `sudo bash simulation/network/launch-px4-only.sh` running in its own
  terminal -- PX4 SITL alone inside `aircraft-net`, with nothing else tied to
  its process lifetime.
- `sudo bash simulation/network/launch-agent-only.sh` running in a second,
  separate terminal -- the `mission_agent` binary alone inside `aircraft-net`,
  connecting to PX4's onboard MAVLink link.
- `sudo bash simulation/network/launch-ground.sh` running in a third
  terminal -- backend inside `ground-net`, connected to the Mission Agent
  across the WireGuard tunnel.
- `socat TCP-LISTEN:8000,fork,reuseaddr TCP:10.201.0.2:8000` running (frontend
  relay, no sudo required).
- Frontend dev server (`npm run dev`) running natively on Windows.

These four processes are deliberately launched as four independent
single-process scripts/terminals rather than the combined
`launch-aircraft.sh`, specifically so that killing the Mission Agent cannot
take PX4 down with it as an artifact of shared process lifetime. Two earlier
attempts at a single combined "launch both, but decoupled" script (background
+ exec patterns) both still killed PX4 when the Mission Agent was killed --
this project's launch scripts have no reliable in-script way to background a
process without tying its life to the parent shell (documented Phase 1
finding, re-confirmed here). One foreground process per script per terminal
is the only pattern proven reliable.

## ACTION

1. Through the frontend UI: place 2 waypoints, upload, and start the mission.
2. Poll the PX4 log for `Takeoff detected` to confirm the vehicle is
   genuinely airborne before injecting the fault.
3. Kill only the Mission Agent process (`kill -9` on the `mission_agent`
   binary's own PID, not its `sudo`/`env` wrapper PIDs).
4. Observe three independent log sources: PX4's own log (does the mission
   keep running?), the backend's log (does it detect the drop?), and
   `ps aux` (does PX4 survive?).
5. Relaunch `launch-agent-only.sh` and observe whether the backend
   reconnects on its own.

## EXPECTED RESULT

- PX4's process survives the Mission Agent's death untouched.
- PX4 logs a warning about losing the companion link but continues flying
  and lands the mission normally -- flight execution must not depend on the
  companion computer once a mission is uploaded and started, matching real
  hardware where PX4 is the flight-critical component and the companion
  computer is not.
- The backend detects the disconnect (`Mission agent link down`) and begins
  retrying with backoff, per the reconnect-with-backoff logic added in the
  Mission Agent integration's final fix wave.
- Once the Mission Agent process is relaunched, the backend's own retry loop
  finds it and reconnects with no backend restart needed.

## PASS CRITERIA

- [x] PX4's process (`ps aux`) is unaffected by killing the Mission Agent.
- [x] PX4's log shows the mission completing (`Landing detected` / `Mission
      finished, landed` / `Disarmed by landing`) after the companion link is
      lost, with no failsafe abort.
- [x] Backend log shows it detected the disconnect
      (`Mission agent link down`).
- [x] Backend log shows automatic reconnection
      (`Mission agent connected` / `Mission agent link up`) after the agent
      is relaunched, without restarting the backend process.

## Result

**PASS.** Executed 2026-08-21 against PX4 SITL (SIH, `sihsim_standard_vtol`)
in `aircraft-net`, the real `mission_agent` C++ binary in a separate terminal
in the same namespace, backend in `ground-net`, all four processes launched
independently per the SETUP above.

```
=== PX4 log, after Mission Agent kill ===
INFO  [commander] Takeoff detected
WARN  [commander] Connection to mission computer lost
INFO  [commander] Landing detected
INFO  [navigator] Mission finished, landed
INFO  [commander] Disarmed by landing

=== ps aux, after Mission Agent kill ===
youruser  13698 ... /home/youruser/src/PX4-Autopilot/build/px4_sitl_default/bin/px4 -d
(still running -- Mission Agent's own PID 13660 is gone)

=== Backend log, disconnect detection ===
WARNING:app.mission_agent_client:Mission agent connection closed
INFO:app.vehicle:Mission agent link down
WARNING:app.mission_agent_client:Mission agent connection to 10.99.0.2:5760 failed: [Errno 111] Connection refused
[... retries with backoff ...]

=== Backend log, automatic reconnection after relaunching the agent ===
WARNING:app.mission_agent_client:Mission agent connection to 10.99.0.2:5760 failed: [Errno 111] Connection refused
INFO:app.mission_agent_client:Mission agent connected at 10.99.0.2:5760
INFO:app.vehicle:Mission agent link up
```

Three independent sources (PX4's own flight-control log, `ps aux` process
state, and the backend's connection-state log) all agree: the companion
computer dying mid-flight neither aborted the mission nor required manual
recovery once it came back.

## Finding during this test (real, not anticipated in the design)

**Missions landed at the last waypoint instead of returning home.** The
Mission Agent's `upload_mission()` (`onboard/mission-agent/src/mavlink_connection.cpp`)
was auto-appending a required terminal `Land` mission item at the *last
waypoint's own coordinates* (a carry-over from the pre-Mission-Agent backend
behavior it was matching). This meant every mission landed wherever the
final waypoint happened to be, not back at the launch point -- caught by
flying a real 2-waypoint mission and observing the vehicle land at waypoint 2
instead of returning home. Fixed by using MAVSDK's `telemetry_->home()`
position for the appended landing item instead of the last waypoint's
coordinates (commit `9185cb2`). Re-verified with a second live flight: the
vehicle correctly returned to and landed at the true home position.

Also worth noting: **PX4's "home" position is the position at arm time, not
a fixed launch pad.** Re-running a mission without restarting PX4 SITL
between runs will use the *previous* landing spot as the new home, since
that's physically where the (simulated) vehicle was sitting when it armed
again. This is correct, expected PX4 behavior, not a bug -- but it means
clean single-flight verification of "does it return to the *original* home"
requires restarting PX4 SITL between test runs, which resets its simulated
position back to the configured origin.
