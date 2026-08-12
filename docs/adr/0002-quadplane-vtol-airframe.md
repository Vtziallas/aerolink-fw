# ADR 0002: Commit to a QuadPlane VTOL Airframe (Stork VTOL Pack)

## Status

Accepted (2026-08-12)

## Context

`project_prompt.txt` and `ARCHITECTURE.md` originally scoped this project as
a pure fixed-wing aircraft ("The UAV is FIXED-WING, not quadcopter", stated
repeatedly in the original brief). Phases 0-6 were built and validated
entirely against that assumption -- PX4's `sihsim_airplane` SITL model,
runway/hand-launch-style mission profiles, and fixed-wing-specific tuning
(waypoint turn-angle limits, RTL climb altitude behavior, landing approach
geometry).

The user identified a specific physical airframe to build toward: the
[Stork VTOL Pack](https://flightory.com/product/stork-vtol-pack/), a
3D-printed hybrid VTOL -- a "4+1 QuadPlane" configuration: four vertical-lift
motors (quadcopter-style, for takeoff/landing/hover) plus one rear pusher
motor for forward cruise flight, with conventional fixed-wing control
surfaces (ailerons, elevator, rudder) for the cruise phase. Sold as
STL/STEP files for 3D printing ($14.99 + $29.99 base airframe), targeting
1620mm wingspan, 1800-3100g all-up weight, 50-70 km/h cruise.

This is PX4's "Standard VTOL" airframe type (`VT_TYPE=2`), not a pure
fixed-wing aircraft, and not a pure multicopter either -- it's a genuine
hybrid with its own transition logic between hover and cruise flight.

## Decision

Commit to the QuadPlane VTOL configuration as the target airframe. Update
`ARCHITECTURE.md`'s "fixed-wing, not quadcopter" framing to describe a
QuadPlane VTOL instead.

**Simulator backend does not need to change.** PX4's SIH simulator has a
native `sihsim_standard_vtol` airframe (`ROMFS/px4fmu_common/init.d-posix/airframes/10043_sihsim_standard_vtol`)
with exactly this motor layout (4 MC lift motors + 1 forward thrust motor,
`SIH_VEHICLE_TYPE=3`). This was confirmed by reading the actual airframe
file, not assumed -- the concern going into this decision was that VTOL
transition dynamics might force a switch to the Gazebo/mavlink-simulator
backend (which Phase 1 deliberately avoided for WSL2 GUI/OpenGL complexity
reasons). That concern turned out to be unfounded.

## Consequences

- **Reusable as-is:** the whole software architecture (backend, frontend,
  network split, WireGuard tunnel, mission API, telemetry) is airframe-type
  agnostic and needs no changes. `ARCHITECTURE.md`'s non-critical/
  mission-critical/flight-critical separation, and the Phase 4/5 network
  fault-injection results, apply unchanged -- none of that testing depended
  on fixed-wing-specific flight dynamics.
- **Needs rework, not reuse:** fixed-wing-specific tuning from Phase 1-6 does
  not carry over and must be re-validated against `sihsim_standard_vtol`
  before being trusted for VTOL flight:
  - Mission waypoint turn-angle/separation limits (`MAX_TURN_ANGLE_DEG`,
    `MIN_WAYPOINT_SEPARATION_M` in `app/config.py`) were tuned against plain
    fixed-wing cornering behavior.
  - RTL climb-altitude and landing-approach-abort behavior
    (`SIMULATION.md`'s Phase 3 findings) may differ under VTOL transition
    logic.
  - The landing-item requirement itself may change -- a VTOL can land
    vertically at any point inside the geofence rather than needing a
    fixed-wing glide approach, which could simplify some of what Phase 1-3
    worked around.
- **New failure modes, not yet analyzed:** lift-motor failure during hover,
  failed/aborted transition, and hover-phase battery/wind margins are not
  covered by the current `SAFETY.md` failsafe table at all. Tracked as new
  design-only rows until tested.
- **Hardware requirements change:** `HARDWARE.md`'s motor/ESC/propeller
  section assumed a single propulsion system; a QuadPlane needs two (lift +
  cruise), sized and analyzed separately, with their own combined power
  budget.
- **Practical upside:** VTOL removes the hand-launch/runway requirement
  that was a real constraint on `FLIGHT_TEST_PLAN.md`'s VLOS test site
  options -- a real, non-trivial simplification for Phase 9 logistics, in
  exchange for the software/testing rework above.
