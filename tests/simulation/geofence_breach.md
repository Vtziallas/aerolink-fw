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

- Pre-flight-rejection check: `sudo bash simulation/network/set-geofence-test.sh`
  -- uploads a real PX4 onboard geofence (200m inclusion circle around home)
  and sets `GF_ACTION=3` (Return mode; default is `2`, Hold mode) *before*
  a mission that violates it is uploaded.
- In-flight breach check: `sudo bash simulation/network/test-inflight-geofence-breach.sh`
  -- a single self-contained script (fixing the 2026-08-11 timing problem
  below) that uploads a valid mission, arms and starts it, waits a fixed 18s
  while genuinely airborne, *then* uploads the 200m fence and watches the
  response -- all within one continuous process, so there's no manually-timed
  gap between "start flying" and "shrink the fence" for a human to fumble.

## ACTION

1. Pre-flight: upload and start a mission whose waypoints fall outside the
   200m circle (standard 3-waypoint test mission, ~300-400m out) with the
   fence already active.
2. In-flight: run `test-inflight-geofence-breach.sh` end to end.
3. `sudo bash simulation/network/revert-geofence-test.sh` afterward.

## EXPECTED RESULT

- A mission whose waypoints violate the active fence is rejected before it
  can fly (pre-flight gate).
- An aircraft that ends up outside the fence *while already flying*
  triggers `GF_ACTION`'s configured response (Return mode: fly home).

## PASS CRITERIA

- [x] Mission upload/arm/takeoff is refused when the mission violates the
      active onboard fence -- confirmed at multiple points.
- [x] In-flight breach triggers the configured GF_ACTION response (Return
      mode) -- confirmed 2026-08-12, see Result.

## Result

**PASS.** Pre-flight rejection executed 2026-08-11; in-flight breach
executed and confirmed 2026-08-12.

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

**In-flight/runtime breach response (`GF_ACTION=3`, Return mode, while
already airborne) -- confirmed 2026-08-12.** On 2026-08-11 this was attempted
twice with separately-timed manual `sudo` commands (start a mission, then a
human runs a second script to shrink the fence) and failed both times --
not because the system didn't work, but because our simulated flights
complete in ~30-40s, faster than a human can reliably run two `sudo`
commands in sequence. Fixed by writing `test-inflight-geofence-breach.sh`,
which does the entire sequence (upload mission, arm, start, wait 18s while
genuinely airborne, upload the tighter fence, watch the response) inside one
continuous Python/MAVSDK script, eliminating the manual timing gap entirely.

Full PX4 log from the successful run:

```
INFO  [navigator] Executing Mission
INFO  [navigator] Climb to 30.0 meters above home
INFO  [commander] Takeoff detected
WARN  [navigator] Geofence violation for waypoint 1
WARN  [navigator] mission check failed
WARN  [navigator] No valid mission available, loitering
WARN  [health_and_arming_checks] Geofence: approaching or outside geofence
WARN  [failsafe] Failsafe activated: entering Hold for 5 seconds
WARN  [failsafe] Failsafe activated
INFO  [navigator] RTL: start return at 589 m (100 m above destination)
WARN  [health_and_arming_checks] Geofence: approaching or outside geofence
INFO  [navigator] RTL: completed, loitering
```

The aircraft was actually still ~134m from home (inside the 200m circle) at
the exact moment the fence was uploaded -- PX4's `approaching or outside
geofence` check evidently has enough look-ahead (the aircraft's mission/
trajectory would have carried it outside shortly) to trigger before an
actual boundary crossing, which is the more conservative and arguably
correct behavior. Confirmed via live telemetry immediately after: `mode:
RETURN_TO_LAUNCH, armed: true, alt: 97.4m`, position within ~100m of home --
safely recovered, not crashed or lost. The failsafe-triggered RTL ends in an
indefinite loiter near home rather than auto-landing (distinct from a
mission's own RTL-with-landing-item flow); bringing it down the rest of the
way needs an explicit follow-up command, consistent with the "no resume/
land-now control yet" gap already noted in `latency_injection.md`.

**Also out of scope for this session:** live GPS degradation/loss injection.
PX4's failure-injection command (`MAV_CMD_INJECT_FAILURE`) is only wired up
for the `simulator_mavlink` backend (Gazebo-style setups), not SIH --
confirmed by reading the source, not by trial and error. `SENS_EN_GPSSIM` is
read once at boot to decide whether to start the GPS-sim module at all, not
something that can be toggled mid-flight. Testing GPS failsafes cleanly
would need the Gazebo/mavlink-simulator backend, which was deliberately
avoided in Phase 1 for good reasons (WSL2 GUI/OpenGL complexity) -- revisit
if/when that tradeoff changes.
