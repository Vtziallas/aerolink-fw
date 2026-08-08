# Simulation

Simulation-first development is mandatory for this project: no physical aircraft purchase or flight until the simulated stack works reliably end-to-end (see `docs/flight-tests/` and `FLIGHT_TEST_PLAN.md` for what "reliably" has to mean before Phase 9).

## Milestone 1 — Standalone SITL + Custom GCS

Everything runs on a single dev PC, no hardware required.

**Setup:** PX4 fixed-wing SITL (current stable release, v1.17.0) + custom backend (MAVSDK) + custom React frontend.

Simulation backend: **SIH** ("Simulation In Hardware"), not jMAVSim. jMAVSim only models multicopter aerodynamics — there is no fixed-wing jMAVSim airframe in this PX4 version. SIH runs the flight dynamics model inside the PX4 binary itself (no external simulator process, no GUI/OpenGL dependency), which is also what makes it reliable under WSL2. Gazebo Classic remains an option later if a 3D visual is wanted for the portfolio demo; SIH is sufficient for control/mission validation. Build target: `make px4_sitl sihsim_airplane` (confirmed for v1.17.0 by reading `ROMFS/px4fmu_common/init.d-posix/airframes/10041_sihsim_airplane`, not assumed).

**Acceptance criteria:**

- [x] PX4 fixed-wing SITL launches and reaches a flyable state
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

## Phase 1 Findings (PX4 SITL in WSL2)

- **Run PX4 in daemon mode (`-d`), never through `make px4_sitl <target>` in a non-interactive shell.** `make` launches PX4's interactive `pxh>` console, which expects a real TTY. Without one it spins redrawing the prompt in a tight loop, burning CPU and writing gigabytes of log output within minutes. Correct invocation:
  ```
  cd build/px4_sitl_default/src/modules/simulation/simulator_sih
  PX4_SIM_MODEL=sihsim_airplane PX4_SIMULATOR=sihsim ../../../bin/px4 -d
  ```
  (first build once via `make px4_sitl sihsim_airplane` so the binary exists, then launch it directly with `-d` for actual runs)
- **Fixed-wing missions require an explicit landing item.** Unlike multicopters, PX4's `mission_feasibility_checker` rejects a fixed-wing mission that only ends in "RTL after mission" — it needs a mission item with a landing action/pattern (MAVSDK: `MissionItem.VehicleAction.LAND` on the final item). Without it, the mission silently fails to start and the aircraft never leaves the ground, with no obviously-fatal error unless you go looking in the PX4 console log.
- **MAVLink ports for SITL instance 0:** companion/offboard link `udp://:14540` (MAVSDK default), GCS link on `18570` (PX4 sends there; QGroundControl/our GCS should listen there or PX4's `18570` maps to `14550` on the remote side depending on tool defaults — verify against `px4-rc.mavlink` if a connection doesn't come up).
- Verified end-to-end with MAVSDK: armed, uploaded a 4-item mission (3 waypoints at 30m + landing item), flew it autonomously — climbed to ~31m, executed the route, descended, and landed near the target point.

## Status

Phase 1 baseline (standalone PX4 fixed-wing SITL, arms and flies an uploaded mission) is working. Milestone 1's remaining acceptance criteria (custom backend/frontend, live map, mission upload through our own API, simulated network outage) are not yet started — that's Phase 2/3. Network fault injection tooling (Phase 5) not yet started; will be filled in with actual tool choices once Phase 2/3 are complete.
