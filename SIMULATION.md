# Simulation

Simulation-first development is mandatory for this project: no physical aircraft purchase or flight until the simulated stack works reliably end-to-end (see `docs/flight-tests/` and `FLIGHT_TEST_PLAN.md` for what "reliably" has to mean before Phase 9).

## Milestone 1 — Standalone SITL + Custom GCS

Everything runs on a single dev PC, no hardware required.

**Setup:** PX4 fixed-wing SITL (current stable release, v1.17.0) + custom backend (MAVSDK) + custom React frontend.

Simulation backend: **SIH** ("Simulation In Hardware"), not jMAVSim. jMAVSim only models multicopter aerodynamics — there is no fixed-wing jMAVSim airframe in this PX4 version. SIH runs the flight dynamics model inside the PX4 binary itself (no external simulator process, no GUI/OpenGL dependency), which is also what makes it reliable under WSL2. Gazebo Classic remains an option later if a 3D visual is wanted for the portfolio demo; SIH is sufficient for control/mission validation. Build target: `make px4_sitl sihsim_airplane` (confirmed for v1.17.0 by reading `ROMFS/px4fmu_common/init.d-posix/airframes/10041_sihsim_airplane`, not assumed).

> **VTOL note:** everything in Milestones 1-2 and the Phase 1-6 findings below was built and validated against `sihsim_airplane` (pure fixed-wing), before the [QuadPlane VTOL decision](docs/adr/0002-quadplane-vtol-airframe.md). It remains accurate as a record of what was actually done and stays reusable for the software/network/failsafe-pattern layers (see that ADR's Consequences section), but none of the flight-dynamics-specific tuning below (turn-angle limits, RTL climb/landing-approach behavior, acceptance radius, trim-speed matching) should be assumed to still hold under `sihsim_standard_vtol` (confirmed to exist at `ROMFS/px4fmu_common/init.d-posix/airframes/10043_sihsim_standard_vtol`, matching the Stork's 4-lift+1-pusher layout) until re-run and re-verified against it. Treat this whole section as "what we learned about PX4 fixed-wing SITL," not "what's currently true of this aircraft."

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
- **A one-time UDP partner-learning race between PX4 and MAVSDK can permanently wedge a connection until PX4 is restarted.** Hit this after switching to `sihsim_standard_vtol` and relaunching the backend: `mavsdk_server` sat at "Waiting to discover system..." indefinitely even though the WireGuard tunnel was confirmed healthy (`wg show` handshake current, `tcpdump` showed PX4 actively streaming telemetry through the tunnel at full rate). Root cause, confirmed by comparing a packet capture against `ss -uapn`: PX4 was streaming to UDP port 52462, but the live `mavsdk_server` process's actual socket was bound to a different port (49010) -- `mavsdk_server` had evidently sent its first discovery packet from one ephemeral socket, then internally retried onto a new one before PX4's reply came back, and PX4's GCS-style MAVLink instance only ever learns its peer address **once**, from whichever source sent the first packet (`NETWORKING.md`'s Phase 4 finding) -- it never re-learns from a later, different source, even one on the same host sending the same protocol. High traffic volume alone does not prove MAVSDK-level discovery succeeded -- PX4 will happily keep streaming full-rate telemetry to a stale learned partner forever, so a live tcpdump capture can look completely healthy while the actual client is listening on the wrong port. Fix: kill both PX4 and the backend (this clears PX4's learned-partner state) and relaunch both fresh. Diagnostic scripts for this are kept in `simulation/network/diagnose-vtol-connection.sh` and `diagnose-vtol-packets.sh` in case it recurs -- not VTOL-specific despite the filename, this can happen on any restart.

## Network Outage Findings

Full test definition and result: [`tests/simulation/network_outage_during_mission.md`](tests/simulation/network_outage_during_mission.md).

Summary: killed the backend entirely mid-mission (armed, mode `MISSION`, waypoint 2/3). PX4 logged `Connection to ground station lost` purely informationally and continued the mission to completion -- landed and disarmed autonomously with the backend confirmed dead the whole time (current `NAV_DLL_ACT=0`, data-link-loss failsafe disabled, so this is the expected result given that config, not a coincidence). Backend, once restarted, reconnected and synced to the aircraft's real post-outage state within seconds rather than showing stale data. This is the empirical confirmation of `ARCHITECTURE.md`'s core claim: the ground backend is non-critical, and its disappearance does not threaten flight safety once a mission is airborne.

## Phase 6 Findings (failsafe testing)

Battery: [`tests/simulation/battery_failsafe.md`](tests/simulation/battery_failsafe.md) -- PX4's own `BAT_LOW_THR`/`BAT_CRIT_THR`/`BAT_EMERGEN_THR` thresholds and `COM_LOW_BAT_ACT=3` fired correctly and in order (warning -> return -> emergency land), entirely autonomously. Required removing SITL's battery floor and speeding up the drain rate for a practical test -- see the test doc and `simulation/network/set-battery-test-params.sh`.

Geofence: [`tests/simulation/geofence_breach.md`](tests/simulation/geofence_breach.md) -- confirmed PX4 independently refuses to arm/fly a mission that violates its own onboard geofence (real defense in depth, not just our backend's client-side check), **and** (2026-08-12) confirmed the in-flight/runtime breach response: `GF_ACTION=3` (Return mode) fired correctly while genuinely airborne, climbing to RTL altitude and returning safely toward home. The 2026-08-11 attempt failed on manual test-harness timing (two separately-timed `sudo` steps couldn't keep up with a ~30-40s flight); fixed by scripting the whole sequence (upload, arm, start, wait 18s, upload the tighter fence, observe) as one continuous script (`simulation/network/test-inflight-geofence-breach.sh`) instead of relying on human timing between two commands.

GPS degradation/loss: not attempted. PX4's failure-injection command (`MAV_CMD_INJECT_FAILURE`) is confirmed (by reading the source) to only be wired up for the `simulator_mavlink` backend, not SIH. Testing this cleanly needs the Gazebo/mavlink-simulator backend, deliberately avoided since Phase 1 for good reasons (WSL2 GUI/OpenGL complexity) -- revisit if that tradeoff ever changes.

`SAFETY.md`'s event/response table was reconciled with all of the above: verified rows now say so, design-only rows still say so, and the `LTE_LOST` design was corrected to match what's actually configured (simpler than originally specified, not a shortfall -- see that document).

## VTOL Re-Validation Needed (before Phase 7 relies on any of the above)

The [QuadPlane VTOL decision](docs/adr/0002-quadplane-vtol-airframe.md) landed after Phase 6 was already complete against `sihsim_airplane`. Before that Phase 1-6 work is trusted for the actual target aircraft, it needs to be re-run against `sihsim_standard_vtol` (`PX4_SIM_MODEL=sihsim_standard_vtol`, same daemon-mode launch pattern as the Phase 1 findings above) and checked for the following, none of which is assumed to transfer automatically:

- **Mission tuning parameters** (`MAX_TURN_ANGLE_DEG`, `MIN_WAYPOINT_SEPARATION_M`, `DEFAULT_ACCEPTANCE_RADIUS_M`, `DEFAULT_CRUISE_SPEED_M_S` in `app/config.py`) were all empirically tuned against fixed-wing cornering/trim behavior (Phase 3 findings above) -- a VTOL's cruise-phase flight dynamics may be similar (same wing, same control surfaces) but this has not been verified, and hover/transition phases have no equivalent tuning at all yet.
- **RTL behavior** (climb-to-`RTL_RETURN_ALT` via `NAV_CMD_LOITER_TO_ALT`, landing-approach geometry) may differ under VTOL -- a Standard VTOL can transition back to hover and land vertically rather than needing a fixed-wing glide/landing-item approach, which could change or simplify the landing-item requirement noted in Phase 1 findings, but this is unverified.
- **The landing-item requirement itself** (Phase 1 finding: fixed-wing missions reject without an explicit landing item) needs to be re-checked against `sihsim_standard_vtol` -- may behave differently given vertical-landing capability.
- **GPS degradation/loss testing** is still blocked on simulator backend (`MAV_CMD_INJECT_FAILURE` only wired up for `simulator_mavlink`, not SIH) regardless of airframe type -- this constraint doesn't change with the VTOL decision.
- New hover/transition-specific scenarios that simply didn't exist before (`LIFT_MOTOR_FAILURE`, `TRANSITION_FAILURE` -- see `SAFETY.md`'s Event → Response table) have no simulation test at all yet, design-only.

None of Phase 1-6's checkmarks above should be read as "done for the VTOL" -- they're an accurate record of what was verified for the airframe that existed at the time.

## First VTOL Flight (2026-08-12)

First mission flown against `sihsim_standard_vtol`, using the same mission-generation code (3 waypoints + auto-appended landing item) unchanged from the fixed-wing era. Confirmed both by the PX4 console log (armed -> executing mission -> climb -> takeoff detected -> landing detected -> mission finished, landed -> disarmed, no warnings) and by direct visual observation in the GCS UI: **vertical hover takeoff, a mid-route transition to forward flight, and a vertical landing** -- genuine QuadPlane behavior, not fixed-wing-style runway takeoff/glide-landing. Notably, this worked with zero mission-generation code changes -- PX4's own VTOL transition logic decided when to switch hover/cruise automatically, without needing an explicit transition mission item.

This is a real, positive result, but a narrow one -- one uneventful mission, default geometry, no wind/failure conditions. It does not on its own clear the still-open items above (turn-angle/separation tuning under real cruise dynamics, RTL/landing-approach behavior under an aborted or off-nominal transition, hover-phase battery margins, `LIFT_MOTOR_FAILURE`/`TRANSITION_FAILURE`). Treat this as "the airframe swap didn't break the basic mission loop," not as "VTOL flight dynamics are validated."

Separately, this session also surfaced a PX4/MAVSDK networking gotcha unrelated to the VTOL switch itself -- see the new bullet under Phase 4 Findings above (one-time UDP partner-learning race that can wedge a connection until both PX4 and the backend are restarted together).

## Mission-Tuning Stress Test (2026-08-13)

Deliberately flew missions violating `app/config.py`'s `MAX_TURN_ANGLE_DEG` (70) and `MIN_WAYPOINT_SEPARATION_M` (150) -- both derived from Phase 3 fixed-wing findings -- to see whether the same failure modes reproduce on `sihsim_standard_vtol`. Two approaches were tried:

- **Direct MAVSDK bypass** (uploading missions to PX4 without going through the backend's own validation, matching `test-inflight-geofence-breach.sh`'s pattern) worked for isolated single-flight tests, but was unreliable for a rapid multi-scenario sweep: a second independent `mavsdk_server` process cannot discover PX4 at all while the backend's own connection is already the established partner (PX4's GCS-style MAVLink instance only tracks one learned peer, and `mavsdk_server` doesn't open its own gRPC port until *after* UDP discovery succeeds -- so a second client just hangs indefinitely, silently, with no error). Do not run a second MAVSDK client against the same PX4 instance while the backend is connected.
- **What actually worked**: relaunching the backend with `MAX_TURN_ANGLE_DEG`/`MIN_WAYPOINT_SEPARATION_M` relaxed via their existing env-var overrides (`simulation/network/launch-ground-stress-test.sh`), then driving missions through the real `/api/missions` + `/api/missions/current/start` endpoints via plain `curl` to the backend's `ground-net` IP directly (`10.201.0.2:8000` -- no `sudo`/`ip netns exec` needed, since that address is reachable from the default WSL namespace without entering the namespace). This exercises the actual code path a real operator would use, just with relaxed limits, rather than bypassing the backend entirely.

**Results** (all against `sihsim_standard_vtol`, 30m altitude, 3 flights each starting from a clean disarmed-on-ground state):

| Scenario | Geometry | Speed | Result |
|---|---|---|---|
| S2 | 60m/60m legs (well under 150m floor), 45deg turn | 15 m/s (trim) | Completed cleanly via own landing item |
| S3 | 60m/60m legs, 100deg turn | 15 m/s | Completed cleanly, confirmed with full telemetry trace (smooth climb, stable ~28-30m altitude hold, clean descent) |
| S4 | 300m/300m legs, 150deg near-reversal turn | 15 m/s | Completed cleanly in ~50s, no warnings |
| S5 | 250m/250m legs, 40deg turn | 25 m/s (vs. 15 m/s `FW_AIRSPD_TRIM`) | Completed, but took ~200s -- roughly 4x longer than S4's comparable-distance ~50s |
| S1 | 250m/280m legs, 89deg turn (the exact angle that broke fixed-wing in Phase 3) | 15 m/s | Completed via own landing item, no errors -- but real-time data is unreliable here specifically (see caveat below) |

**Headline finding:** geometry that was rejected outright by the app's own limits, and that specifically destabilized the fixed-wing controller in Phase 3 (sharp turns, tight waypoint spacing), flew cleanly on this VTOL with zero failures across S2-S4. The likely explanation is the airframe's very different mass/inertia in cruise phase (`SIH_MASS` 0.2kg vs. whatever the fixed-wing model used) rather than anything about the turn-angle logic itself -- PX4's navigation controller has much less momentum to fight through a sharp turn. **This is a narrow result, not a green light to loosen the production config**: only 5 scenarios, single uneventful runs, no wind/failure combination, and none of it touches hover or transition phases (all the geometry stress was in cruise). Worth a real re-tuning pass before touching `MAX_TURN_ANGLE_DEG`/`MIN_WAYPOINT_SEPARATION_M` for real, not a config change based on this alone.

**The one real weak point found: overspeed cruise (S5).** Matches the exact mechanism from the original Phase 3 finding (commanding speed above `FW_AIRSPD_TRIM` makes the altitude controller trade altitude for airspeed) -- on this VTOL it doesn't cause outright failure the way sharp turns didn't, but it does cost a large, real time penalty (4x). `DEFAULT_CRUISE_SPEED_M_S` should stay matched to trim speed for this airframe too, not just for the old fixed-wing one.

**Caveat on S1's timing:** mid-test, checking on this result took an extended, irregular real-world gap (working through the MAVSDK-bypass connection problem above), and PX4 SITL runs in lockstep at 1x real speed -- so I can't rule out that S1 struggled or circled for a while before eventually completing during that gap. The clean, unambiguous timing data for S4/S5 above comes from a corrected polling method (tracking the log's line offset at mission start, so only genuinely new completion markers count) built specifically because the S1 read was unreliable. If S1's case matters later, re-run it with that same method.

## Mission Agent Integration (2026-08-18)

The C++ `mission_agent` (`onboard/mission-agent/`, see
`docs/architecture/mission-agent-design.md`) is now real, running code, not
just a box in `ARCHITECTURE.md`'s diagram. Built via 11 TDD tasks (subagent-driven,
each with an independent spec+quality review), then verified end-to-end: full
mission upload/start/execute/land through the real chain -- backend ->
WireGuard tunnel -> Mission Agent (TCP/JSON) -> MAVSDK (local link) -> PX4 --
confirmed via three independent log sources agreeing on the outcome, not one
process's self-report taken at face value. Full test record:
[`tests/simulation/mission_agent_integration.md`](tests/simulation/mission_agent_integration.md).

The backend no longer holds any MAVSDK connection at all -- `VehicleConnection`
is now a plain TCP/JSON client of the Mission Agent (`app/mission_agent_client.py`).
This closes the gap this document flagged back in Phase 2/3: the companion
computer's role (command validation, geofence enforcement, the MAVSDK relay to
PX4) was always meant to live in a separate onboard process, not inside the
ground-station backend -- that's now actually true, not just documented as the
target architecture.

**Two real findings from this integration pass, not anticipated in the
design**, both now fixed and worth remembering for future work on this
machine specifically:
- A plain `git merge --ff-only` silently corrupted three shell scripts with
  CRLF line endings on this Windows/WSL checkout (autocrlf on checkout, not a
  content bug -- the committed blobs were correct LF). Fixed with a new
  `.gitattributes` (`*.sh`/`*.py` forced to `eol=lf`) so it can't recur.
- `mission_agent`'s stdout is block-buffered when redirected to a log file,
  same class of issue as the Python `print()` buffering problem found earlier
  in this project's VTOL debugging (see above) -- its "connected"/"serving"
  log lines can lag real readiness by tens of seconds. Confirmed the process
  was actually functional well before the log caught up via
  `/proc/<pid>/net/tcp` rather than waiting on the log alone.

**What this does NOT yet close**, to avoid overclaiming: Telemetry Agent and
Network Manager (the other two `onboard/` services) are still unbuilt --
the Mission Agent's own `TelemetryPublisher` is a deliberately minimal
stand-in (see the design doc), not those services' real design (rate-classing,
buffering under bad links). The command-replay-protection gap (stale/duplicate
`command_id` rejection) noted in `NETWORKING.md` is still open -- this
integration pass used the same simple JSON protocol the design doc specified,
which explicitly deferred that. And the "distinct onboard companion process"
now genuinely exists (closing a structural gap this document flagged as
needing infrastructure not yet built), but a companion-service-restart fault
test (crash the Mission Agent specifically, confirm PX4/backend behavior,
distinct from a plain backend restart) has not actually been run yet -- worth
doing as real Phase 5-style fault injection now that the infrastructure to do
it finally exists, not assumed safe by analogy to the backend-restart test.

## Status

**Milestone 1 is complete.** SITL, backend, frontend, mission upload/validation/execution, live telemetry, emergency RTL, and the network-outage/failsafe behavior are all working end-to-end and verified against live PX4 SITL.

**Phase 4 (network split) and Phase 5 (fault injection) are complete**, aside from items that need infrastructure not yet built (command auth for stale/duplicate command rejection -- still open). The companion-service-restart test that needed a distinct onboard process now has that infrastructure (see "Mission Agent Integration" below), but the actual fault-injection test hasn't been run yet. Backend and PX4 communicate over a real WireGuard tunnel between two isolated Linux network namespaces (`NETWORKING.md`'s Phase 4 Implementation section). The aircraft was flown, or attempted to be recovered, under 250ms latency, 15% packet loss, and a full mid-flight link interruption + reconnection -- all passing, all documented in `tests/simulation/`.

**Phase 6 (failsafe testing) is complete, aside from GPS degradation/loss**, which is blocked on simulator backend choice (needs Gazebo/mavlink-simulator, not SIH) and tracked honestly as not attempted rather than assumed to work. Battery failsafe and geofence breach (both pre-flight rejection and genuine in-flight recovery) are fully verified.
