# Simulation

Simulation-first development is mandatory for this project: no physical aircraft purchase or flight until the simulated stack works reliably end-to-end (see `docs/flight-tests/` and `FLIGHT_TEST_PLAN.md` for what "reliably" has to mean before Phase 9).

## Milestone 1 — Standalone SITL + Custom GCS

Everything runs on a single dev PC, no hardware required.

**Setup:** PX4 fixed-wing SITL (current stable release, v1.17.0) + custom backend (MAVSDK) + custom React frontend.

Simulation backend: **SIH** ("Simulation In Hardware"), not jMAVSim. jMAVSim only models multicopter aerodynamics — there is no fixed-wing jMAVSim airframe in this PX4 version. SIH runs the flight dynamics model inside the PX4 binary itself (no external simulator process, no GUI/OpenGL dependency), which is also what makes it reliable under WSL2. Gazebo Classic remains an option later if a 3D visual is wanted for the portfolio demo; SIH is sufficient for control/mission validation. Build target: `make px4_sitl sihsim_airplane` (confirmed for v1.17.0 by reading `ROMFS/px4fmu_common/init.d-posix/airframes/10041_sihsim_airplane`, not assumed).

**Acceptance criteria:**

- [x] PX4 fixed-wing SITL launches and reaches a flyable state
- [x] Backend connects to SITL via MAVSDK and can read vehicle state
- [x] Frontend shows the simulated aircraft's live position on a map
- [x] Operator can place 3 waypoints (HOME → A → B → C → HOME) in the UI
- [x] Backend validates the mission (coordinates, ordering, geofence) before upload
- [x] Mission uploads to PX4 and the aircraft confirms receipt (visible in UI)
- [x] Operator starts the mission; aircraft flies it autonomously in SITL
- [x] Telemetry streams live to the dashboard throughout
- [x] A simulated network outage (backend↔MAVSDK link, not PX4 itself) is triggered manually
- [x] Aircraft continues flying safely / applies a sane failsafe response, independent of the interrupted link

## Milestone 2 — Simulated Ground/Aircraft Network Split

Separate the simulated aircraft network from the ground network as distinct environments (e.g., network namespaces or VMs), connected only through the WireGuard tunnel path described in `NETWORKING.md`. Introduce latency, packet loss, and disconnect/reconnect at that boundary so the software genuinely behaves as if talking over LTE, rather than over localhost.

**Status: network split done, fault injection not yet started.** Two Linux network namespaces (`ground-net`, `aircraft-net`) connected by a real WireGuard tunnel -- see `NETWORKING.md`'s Phase 4 Implementation section for the topology and key findings, `simulation/network/` for the setup scripts. Latency/packet-loss/reconnect injection at that boundary is the next step (Phase 5).

## Milestone 3 — Companion Computer on a Bench

Move the onboard application (mission agent, telemetry agent, network manager) to an actual Linux companion computer. Flight controller can remain simulated or be introduced via hardware-in-the-loop. No propeller-driven flight required.

## Network Fault Injection (Phase 5)

Conditions to simulate, each with SETUP / ACTION / EXPECTED RESULT / PASS CRITERIA defined in `tests/simulation/`. Tooling: `simulation/network/inject-fault.sh` (`tc netem` on the `ground-net`↔`aircraft-net` veth link, see `NETWORKING.md`).

- [x] Latency — tested at 250ms (`tests/simulation/latency_injection.md`); the script supports any value, 50/100/500ms not separately re-run since the mechanism is identical
- [x] Packet loss — tested at 15% (`tests/simulation/packet_loss_injection.md`)
- [x] Connection interruption and full disconnection — `tests/simulation/link_interruption_reconnection.md`
- [x] Reconnection after outage — same test, covers backend auto-resync without a restart
- [x] Backend restart — `tests/simulation/network_outage_during_mission.md`'s restart step
- [ ] Companion-service restart — not distinct from "backend restart" yet, since there's no separate onboard companion process until the C++ mission agent exists (later milestone)
- [ ] Stale command / duplicate command — needs the `command_id`/replay-protection layer, deliberately not built yet (see `NETWORKING.md`)
- [x] Malformed command — covered as application-layer robustness, not network fault injection: Pydantic schema validation rejects malformed mission requests (`ground-station/backend/tests/test_mission.py`)
- [x] Attempted waypoint outside geofence — `ground-station/backend/tests/test_mission.py`
- [ ] Low-battery condition — deferred; requires overriding SITL's `SIM_BAT_MIN_PCT` (currently floors the simulated battery at 50%, a deliberate PX4 testing default -- see Phase 3 findings)
- [ ] GPS degradation — not yet attempted

The result that matters: **the aircraft remains safe when the network is bad.** Not "it worked once" — see `TESTING.md`. Every test above that's checked off ran the aircraft through a real flight (or attempted one) under the stated condition, not a synthetic/mocked check.

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

## Phase 2 Findings (backend against SITL)

- **The ground-station backend must currently run inside WSL, not natively on Windows**, when developing against WSL-hosted SITL. WSL2 defaults to NAT networking (no `.wslconfig` with `networkingMode=mirrored`), which forwards Windows→WSL over `localhost` but not the reverse: PX4 (inside WSL) sends its outbound MAVLink stream to `localhost:14540`, which stays inside the WSL network namespace and never reaches a Windows-native process. Rather than force mirrored networking or reconfigure PX4's target address, we just run the backend inside WSL too (venv at `~/venvs/aerolink-backend`, source still on the Windows-mounted repo path). Actually bridging separate ground/aircraft networks is Phase 4's job, done properly with WireGuard — not something to improvise here.
- Editable pip installs (`pip install -e .`) fail on `/mnt/c/...` paths with `Operation not permitted` (a 9P filesystem quirk with the temp files `setuptools` creates). Fix: install dependencies directly instead of installing our own package; running `uvicorn app.main:app` from the backend directory makes the `app` package importable without needing it "installed".
- Verified: `GET /api/status` reports `vehicle_connected: true`; `/ws/telemetry` streams live position, attitude, airspeed/groundspeed/heading (via MAVSDK's `fixedwing_metrics`, not derived from NED velocity), battery, flight mode, armed state, and GPS/armable health — all sourced from the same SITL instance validated in Phase 1.

## Phase 2 Findings (frontend)

- The frontend dev server runs natively on Windows (Node 22, Vite) while SITL and the backend run inside WSL. This direction (Windows → WSL over `localhost`) is exactly what WSL2's default NAT networking *does* forward, unlike the reverse direction noted above -- so the WebSocket connection from the browser to `ws://localhost:8000/ws/telemetry` works without any extra configuration.
- Verified visually: map loads with the aircraft marker positioned correctly near PX4 SITL's default home, telemetry panel updates live (altitude, airspeed, heading, battery, flight mode, armed state), link-status badge reflects the WebSocket connection state.

## Phase 3 Findings (mission upload, validation, RTL)

Several real bugs surfaced testing this end-to-end, beyond the mission-plan itself:

- **CORS was never configured on the backend.** The frontend (`:5173`) and backend (`:8000`) are different origins, so the browser sends a preflight `OPTIONS` request before any `POST` -- FastAPI/Starlette return `405` for that by default with no CORS middleware installed. WebSocket connections aren't subject to the same preflight, which is exactly why telemetry worked from day one while mission upload silently failed until `CORSMiddleware` was added.
- **Don't gate `arm()` on the `is_armable` telemetry flag.** It was observed `False` on a *fresh* MAVSDK connection immediately after an autonomous landing, while a direct `arm()` call succeeded right away. The flag is advisory and can lag PX4's actual real-time arming check. Fix: just attempt the arm and surface whatever real rejection PX4 returns, rather than pre-emptively blocking on a client-side signal that isn't authoritative.
- **A single mission item's speed left as NaN (instead of a real number) can make PX4's landing-approach feasibility check spuriously reject an otherwise-valid mission** ("the approach waypoint must be above the landing point", even when the altitudes were plainly fine). Fixed by applying a default cruise speed (`DEFAULT_CRUISE_SPEED_M_S`) to every mission item instead of leaving unset speeds as NaN.
- **Sharp turns between waypoints destabilize the fixed-wing controller even when each leg individually satisfies the minimum-separation check.** A real test mission with an ~89&deg; turn at one interior waypoint (legs of ~200-320m, well above the 150m minimum) caused the aircraft to lose the next waypoint and enter a bad state. Distance alone doesn't capture turn difficulty -- added a separate turn-angle check (`MAX_TURN_ANGLE_DEG`, default 70&deg;) between consecutive *user* waypoints. Deliberately not applied to the final turn into the auto-appended landing point, since an ordinary out-and-back mission naturally needs a sharp turn to line up with home, which isn't evidence of the same problem.
- **`RTL_RETURN_ALT` was 100m in this SITL instance, not PX4's compiled default of 60m** (persisted in the instance's saved parameters, not set by any of our code or the airframe file). RTL climbs to `max(current_altitude, home_altitude + RTL_RETURN_ALT)` before flying home -- a legitimate, correct computation, just slower than the 60m default would be. Not a bug; just worth knowing when RTL "seems to be taking forever."
- Fixed-wing RTL's climb phase uses `NAV_CMD_LOITER_TO_ALT` -- the aircraft deliberately circles in place while climbing to the target altitude before flying home. Seeing the aircraft loiter without translating during this phase is expected, not stuck (distinct from the sharp-turn bug above, which *was* a genuine stuck state).
- Verified full loop end-to-end via the real API/UI: place waypoints -> validate/upload (geofence, altitude, min-separation, turn-angle, and the landing-item/speed fixes above all exercised) -> arm -> fly the route -> trigger RTL -> climb -> fly home -> land -> auto-disarm. PX4's own log is the authoritative confirmation of landing (`Mission finished, landed` / `Landing detected` / `Disarmed by landing`) -- telemetry can appear to "pause" for several seconds around touchdown/rollout without anything being wrong.
- **A default acceptance radius matters.** Leaving `acceptance_radius_m` as NaN resolved to only ~3m on upload -- too tight for a fixed-wing aircraft cornering at cruise speed with fly-through waypoints, and could prevent a waypoint from ever registering "reached." Set explicitly to PX4's own real `NAV_ACC_RAD` default (10m) via `DEFAULT_ACCEPTANCE_RADIUS_M`, rather than an invented value (a first attempt at 90m was tried and abandoned -- see git history in `app/config.py` if the reasoning is ever needed).
- **Mission cruise speed should match the airframe's actual trim airspeed**, not an arbitrary round number. Commanding a cruise speed above `FW_AIRSPD_TRIM` (15 m/s for this airframe) was suspected of making the altitude controller trade altitude for airspeed. `DEFAULT_CRUISE_SPEED_M_S` set to 15 to match.
- **The most severe symptom hit in this phase -- consistently losing the aircraft approaching a specific waypoint, airspeed collapsing to a fraction of trim, altitude never converging, no combination of mission-side fixes helping -- turned out to be a degraded WSL2 VM, not a bug in any of our code.** This development session spanned several real days with the host machine sleeping in between. Restarting the `px4` *process* never fixed it, because the underlying WSL2 *VM* stayed up and degraded across those restarts -- its internal clock/scheduling appears to have been disrupted by suspend/resume, and PX4's SIH physics integration depends on that clock for lockstep timing. Fix: `wsl --shutdown` (from Windows, not from inside WSL) to fully restart the VM, then relaunch SITL fresh. The identical mission that was stuck at 7.8 m/s airspeed and 0/4 progress for 100+ seconds flew flawlessly immediately after. **Takeaway: if PX4 SITL behavior degrades inexplicably after a long-running or multi-day WSL session and process-level restarts don't help, restart the WSL VM itself before chasing application-level causes.**

## Phase 4 Findings (network namespace + WireGuard split)

Full implementation notes and topology: `NETWORKING.md`'s Phase 4 Implementation section.

- **`sudo` does not work through the harness's `!` chat-prefix mechanism in this environment** -- it needs a real interactive terminal to prompt for a password, which that channel doesn't provide (commands either silently no-op or, once discovered, fail with a generic "Command not found" that has nothing to do with the actual command). Every privileged command in this whole project (`apt-get install`, running the netns/WireGuard setup script, launching PX4/backend inside a namespace) had to be run directly in the native Ubuntu app window instead. Non-privileged read/verify commands work fine through the normal tool/`!` path.
- **PX4's "already running" check is a filesystem lock, not network-namespace-aware.** Since network namespaces don't isolate the filesystem (only mount namespaces do), an old PX4 instance left running in the default namespace will block a new one from starting in `aircraft-net` with `PX4 server already running for instance 0` -- easy to miss since the error goes into the new instance's log file, not anywhere visible immediately. Always confirm old instances are actually killed (`ps aux | grep bin/px4`) before launching into a namespace.
- Long-running namespaced processes (PX4 in `aircraft-net`, the backend in `ground-net`) are launched as plain foreground commands the user runs directly (`sudo bash launch-aircraft.sh`), relying on the harness's own automatic backgrounding for anything that doesn't return quickly -- manual `nohup`/`setsid`/`disown` wrapping inside the script was deliberately avoided, since that was found unreliable in Phase 1 testing (see above).

## Network Outage Findings

Full test definition and result: [`tests/simulation/network_outage_during_mission.md`](tests/simulation/network_outage_during_mission.md).

Summary: killed the backend entirely mid-mission (armed, mode `MISSION`, waypoint 2/3). PX4 logged `Connection to ground station lost` purely informationally and continued the mission to completion -- landed and disarmed autonomously with the backend confirmed dead the whole time (current `NAV_DLL_ACT=0`, data-link-loss failsafe disabled, so this is the expected result given that config, not a coincidence). Backend, once restarted, reconnected and synced to the aircraft's real post-outage state within seconds rather than showing stale data. This is the empirical confirmation of `ARCHITECTURE.md`'s core claim: the ground backend is non-critical, and its disappearance does not threaten flight safety once a mission is airborne.

## Phase 6 Findings (failsafe testing)

Battery: [`tests/simulation/battery_failsafe.md`](tests/simulation/battery_failsafe.md) -- PX4's own `BAT_LOW_THR`/`BAT_CRIT_THR`/`BAT_EMERGEN_THR` thresholds and `COM_LOW_BAT_ACT=3` fired correctly and in order (warning -> return -> emergency land), entirely autonomously. Required removing SITL's battery floor and speeding up the drain rate for a practical test -- see the test doc and `simulation/network/set-battery-test-params.sh`.

Geofence: [`tests/simulation/geofence_breach.md`](tests/simulation/geofence_breach.md) -- confirmed PX4 independently refuses to arm/fly a mission that violates its own onboard geofence (real defense in depth, not just our backend's client-side check). Did **not** manage to cleanly demonstrate the in-flight/runtime breach response (`GF_ACTION=Return` while already airborne) -- our flights complete faster (~30-40s) than the manual multi-step `sudo` round-trip needed to inject a fence change mid-air. An honest gap, not a claim of failure; closing it needs the whole sequence scripted as one `sudo` call instead of two separately-timed manual steps.

GPS degradation/loss: not attempted. PX4's failure-injection command (`MAV_CMD_INJECT_FAILURE`) is confirmed (by reading the source) to only be wired up for the `simulator_mavlink` backend, not SIH. Testing this cleanly needs the Gazebo/mavlink-simulator backend, deliberately avoided since Phase 1 for good reasons (WSL2 GUI/OpenGL complexity) -- revisit if that tradeoff ever changes.

`SAFETY.md`'s event/response table was reconciled with all of the above: verified rows now say so, design-only rows still say so, and the `LTE_LOST` design was corrected to match what's actually configured (simpler than originally specified, not a shortfall -- see that document).

## Status

**Milestone 1 is complete.** SITL, backend, frontend, mission upload/validation/execution, live telemetry, emergency RTL, and the network-outage/failsafe behavior are all working end-to-end and verified against live PX4 SITL.

**Phase 4 (network split) and Phase 5 (fault injection) are complete**, aside from items that need infrastructure not yet built (command auth for stale/duplicate command rejection; a distinct onboard companion process for a companion-service-restart test distinct from a plain backend restart). Backend and PX4 communicate over a real WireGuard tunnel between two isolated Linux network namespaces (`NETWORKING.md`'s Phase 4 Implementation section). The aircraft was flown, or attempted to be recovered, under 250ms latency, 15% packet loss, and a full mid-flight link interruption + reconnection -- all passing, all documented in `tests/simulation/`.

**Phase 6 (failsafe testing) is mostly complete.** Battery failsafe fully verified. Geofence verified at the pre-flight gate, in-flight breach not yet cleanly demonstrated (test-timing gap, not a system failure). GPS degradation/loss blocked on simulator backend choice, tracked honestly as not attempted rather than assumed to work.
