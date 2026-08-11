# Simulation Test: Geofence Breach Failsafe

Validates `SAFETY.md`'s GEOFENCE_BREACH policy using PX4's own onboard
geofence enforcement (`GF_ACTION`), distinct from our backend's client-side
geofence check. The backend's check only validates a mission's waypoints
*before upload* -- it has no bearing on what PX4 itself does if the aircraft
ends up outside a safe area in flight (e.g. from wind drift, a bad turn, or
an operator uploading a mission through a route other than our own backend).
This test asks: does PX4 itself actually enforce a safe area, independent of
us?

## SETUP

- `sudo bash simulation/network/set-geofence-test.sh` -- uploads a real PX4
  onboard geofence (200m inclusion circle around home) and sets
  `GF_ACTION=3` (Return mode; default is `2`, Hold mode).

## ACTION

1. Upload and start a mission whose waypoints fall outside the 200m circle
   (our standard 3-waypoint test mission, ~300-400m out).
2. Separately: start a mission that fits *inside* the fence, then upload a
   tighter fence while the aircraft is already airborne and outside the new
   boundary, to test a genuine in-flight/runtime breach rather than a
   pre-flight rejection.
3. `sudo bash simulation/network/revert-geofence-test.sh` afterward.

## EXPECTED RESULT

- A mission whose waypoints violate the active fence is rejected before it
  can fly (pre-flight gate).
- An aircraft that ends up outside the fence *while already flying*
  triggers `GF_ACTION`'s configured response (Return mode: fly home).

## PASS CRITERIA

- [x] Mission upload/arm/takeoff is refused when the mission violates the
      active onboard fence -- confirmed at multiple points.
- [ ] ~~In-flight breach triggers the configured GF_ACTION response~~ --
      attempted repeatedly, not cleanly demonstrated; see Result.

## Result

**Partial pass.** Executed 2026-08-11.

**Confirmed, and genuinely valuable:** PX4 enforces the geofence at more
than one gate, independent of our own backend's validation entirely:

```
WARN  [navigator] Geofence violation for waypoint 1
WARN  [navigator] mission check failed
WARN  [commander] Switching to Mission is currently not available
```

and, on a later attempt:

```
WARN  [navigator] Geofence violation for waypoint 1
WARN  [navigator] mission check failed
WARN  [navigator] No valid mission available, refusing takeoff
```

This means even if our backend's own geofence check were ever bypassed or
had a bug, PX4 independently refuses to fly a mission that violates its own
configured fence -- real defense in depth, not just documentation.

**Not cleanly demonstrated: the in-flight/runtime breach response
(`GF_ACTION=3`, Return mode, while already airborne).** Attempted twice:
once flying a full mission and re-uploading a tighter fence mid-route, once
triggering RTL first for a longer flight window before re-uploading the
fence. Both attempts were foiled by test-harness timing, not by the system
under test: our simulated flights complete in ~30-40s, faster than the
multi-step manual round-trip (running a script requiring `sudo`, which
cannot be automated through this session's tooling -- see
`SIMULATION.md`'s Phase 4 findings) needed to inject the fence change while
still airborne. This is an honest gap to close later, likely by scripting
the whole sequence (mission start + delayed fence upload) as a single
`sudo`-run script instead of two separately-timed manual steps, rather than
evidence that the runtime behavior doesn't work.

**Also out of scope for this session:** live GPS degradation/loss injection.
PX4's failure-injection command (`MAV_CMD_INJECT_FAILURE`) is only wired up
for the `simulator_mavlink` backend (Gazebo-style setups), not SIH --
confirmed by reading the source, not by trial and error. `SENS_EN_GPSSIM` is
read once at boot to decide whether to start the GPS-sim module at all, not
something that can be toggled mid-flight. Testing GPS failsafes cleanly
would need the Gazebo/mavlink-simulator backend, which was deliberately
avoided in Phase 1 for good reasons (WSL2 GUI/OpenGL complexity) -- revisit
if/when that tradeoff changes.
