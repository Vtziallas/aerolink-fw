# Simulation

Simulation-first development is mandatory for this project: no physical aircraft purchase or flight until the simulated stack works reliably end-to-end (see `docs/flight-tests/` and `FLIGHT_TEST_PLAN.md` for what "reliably" has to mean before Phase 9).

## Milestone 1 — Standalone SITL + Custom GCS

Everything runs on a single dev PC, no hardware required.

**Setup:** PX4 fixed-wing SITL (current stable release, jMAVSim) + custom backend (MAVSDK) + custom React frontend.

**Acceptance criteria:**

- [ ] PX4 fixed-wing SITL launches and reaches a flyable state
- [ ] Backend connects to SITL via MAVSDK and can read vehicle state
- [ ] Frontend shows the simulated aircraft's live position on a map
- [ ] Operator can place 3 waypoints (HOME → A → B → C → HOME) in the UI
- [ ] Backend validates the mission (coordinates, ordering, geofence) before upload
- [ ] Mission uploads to PX4 and the aircraft confirms receipt (visible in UI)
- [ ] Operator starts the mission; aircraft flies it autonomously in SITL
- [ ] Telemetry streams live to the dashboard throughout
- [ ] A simulated network outage (backend↔MAVSDK link, not PX4 itself) is triggered manually
- [ ] Aircraft continues flying safely / applies a sane failsafe response, independent of the interrupted link

## Milestone 2 — Simulated Ground/Aircraft Network Split

Separate the simulated aircraft network from the ground network as distinct environments (e.g., network namespaces or VMs), connected only through the WireGuard tunnel path described in `NETWORKING.md`. Introduce latency, packet loss, and disconnect/reconnect at that boundary so the software genuinely behaves as if talking over LTE, rather than over localhost.

## Milestone 3 — Companion Computer on a Bench

Move the onboard application (mission agent, telemetry agent, network manager) to an actual Linux companion computer. Flight controller can remain simulated or be introduced via hardware-in-the-loop. No propeller-driven flight required.

## Network Fault Injection (Phase 5)

Conditions to simulate, each with SETUP / ACTION / EXPECTED RESULT / PASS CRITERIA defined in `tests/simulation/`:

- Latency: 50ms, 100ms, 250ms, 500ms
- Packet loss (varying rates)
- Connection interruption and full disconnection
- Reconnection after outage
- Backend restart
- Companion-service restart
- Stale command
- Duplicate command
- Malformed command
- Attempted waypoint outside geofence
- Low-battery condition (where simulation permits)
- GPS degradation (where practical)

The result that matters: **the aircraft remains safe when the network is bad.** Not "it worked once" — see `TESTING.md`.

## Status

Not yet started. This document will be filled in with actual tool choices (network namespaces vs. `tc`/`netem` vs. a dedicated fault-injection harness) once Phase 1 is complete.
