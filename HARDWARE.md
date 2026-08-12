# Hardware

Phase 7 (hardware-in-the-loop / bench testing) starts now that Phase 6 (failsafe testing in simulation) is complete. This document defines engineering requirements for each component category -- not specific parts, not shopping links. Component selection comes after requirements are clear, per `project_prompt.txt` §16.

## Target Airframe

[Stork VTOL Pack](https://flightory.com/product/stork-vtol-pack/) -- a 3D-printed QuadPlane VTOL (see [ADR 0002](docs/adr/0002-quadplane-vtol-airframe.md) for the decision and its consequences). Known specs from the vendor listing:

- Wingspan 1620mm, length 1000mm, all-up weight 1800-3100g, cruise speed 50-70 km/h
- Printed in LW-PLA/ASA + PC/PETG, minimum 220x220mm print bed
- Propulsion: 4x vertical-lift motors (vendor suggests T-Motor F90 1300KV) + 1x rear pusher motor (vendor suggests BrotherHobby Avenger 2812 910KV)
- Control surfaces: single-channel ailerons, elevator, rudder (4 servos total: 2 aileron via Y-cable + elevator + rudder)
- Vendor-suggested electronics: SpeedyBee F405 Wing flight controller, Matek M10Q GPS, dual ESCs (4-in-1 for lift motors + separate for pusher), 4S-6S battery, ELRS receiver

**This project can't take the vendor's electronics list as-is.** Everything built in Phases 1-6 assumes PX4 (via MAVSDK), and the SpeedyBee F405 Wing does not appear in PX4's supported-board list (checked directly against the PX4 source tree, not assumed) -- it reads as an ArduPilot-ecosystem recommendation, which is extremely common for FPV-wing-style boards but incompatible with this project's whole software stack. The motors, ESCs, GPS, and receiver are firmware-agnostic and fine as starting candidates; the flight controller specifically needs a PX4-supported alternative (see Flight Controller below). PX4's SIH simulator does have a matching `sihsim_standard_vtol` model (4 lift motors + 1 forward motor, confirmed by reading `ROMFS/px4fmu_common/init.d-posix/airframes/10043_sihsim_standard_vtol`), so the simulation side of this transition is solved -- it's specifically the physical FC that needs a different choice than the vendor's default.

**HIL vs. physical integration, and why that split matters here:** "Hardware-in-the-loop" means *real flight-controller hardware*, running PX4 (not SITL), against *simulated aerodynamics* -- the physics stay in software, only the FC and companion computer become real. No airframe, motor, or servos are needed for that. Those come in Phase 8 (physical aircraft integration). Splitting the component list this way avoids scope creep: don't spec or buy an airframe/motor/ESC/propeller/servo stack before HIL has validated that the real flight controller + companion computer + network stack behaves the way SITL predicted.

| Needed for Phase 7 (HIL/bench) | Needed additionally for Phase 8 (physical integration) |
|---|---|
| Flight controller | Printed QuadPlane airframe |
| Companion computer | Lift motors (x4) + their ESC |
| Power module (bench supply is fine initially) | Cruise/pusher motor + its ESC |
| GNSS (real fix, even sitting outdoors) | Propellers (lift x4 + cruise x1) |
| LTE modem + SIM | Flight battery |
| Independent safety link (RC receiver) | Servos (ailerons/elevator/rudder) |
| Wiring/power regulation for the bench setup | Airspeed sensor (needs real airflow to validate) |
| | Antennas mounted on the actual airframe (placement is airframe-specific) |

## Decisions Only I Can Make First

Before any category below can turn into an actual part number, these need answers -- they're not engineering derivations, they're project-scope choices:

- **Target endurance** (minutes of flight per battery, and roughly what split between hover and cruise time) -- drives battery capacity, which drives mass, which drives motor/prop sizing on *both* propulsion systems, which feeds back into power budget. Nothing downstream is stable until this is picked. VTOL adds a wrinkle plain fixed-wing didn't have: hover current draw (all 4 lift motors) is typically much higher than cruise current draw (1 pusher motor + control surfaces), so "endurance" really means two numbers -- how long can it hover, how long can it cruise -- not one.
- **Payload/avionics mass budget** -- companion computer + LTE modem + power module + wiring, as a fixed mass the airframe must carry beyond its own structure and propulsion. Rough estimate needed even before finalizing print settings/material choice, since it constrains whether the stock Stork design's payload bay is sufficient.
- ~~Launch method~~ -- resolved by going VTOL: vertical takeoff/landing removes the hand-launch-vs-runway question and its airframe/motor-sizing implications entirely. Real, non-trivial simplification (see ADR 0002).
- **Approximate budget** -- component categories below span an order of magnitude in cost depending on grade (hobby vs. semi-professional), and that mostly determines which specific parts are even worth researching. VTOL also means budgeting for *two* propulsion systems, not one.
- **Test site constraints** (from `FLIGHT_TEST_PLAN.md`, once a real site is identified) -- matters less for takeoff/landing area now (VTOL needs much less clear space than a runway/hand-launch fixed-wing would), but still matters for cruise-phase airspace and any regulatory VTOL-specific considerations.

## Component Categories

### Flight controller (Phase 7)

**Why it exists:** runs PX4 itself -- the flight-critical tier in `ARCHITECTURE.md` §4. Everything else is built around whatever this board can interface with.

**Requirements:** confirmed official PX4 firmware support (not just "PX4-capable" per a forum post -- check the board actually appears in PX4's own supported-hardware list, since the vendor's own suggested board for this airframe does not, see Target Airframe above); Pixhawk-standard connector/pinout preferred (keeps us on well-supported PX4 hardware rather than a bespoke board), though an FPV-style F4/F7 board with confirmed PX4 support is also viable if it has enough outputs; **at least 8 PWM/DShot outputs** -- this specific airframe needs 4 lift motors + 1 pusher motor + servos (2 aileron via Y-cable, elevator, rudder), confirmed against `sihsim_standard_vtol`'s own `PWM_MAIN_FUNC1-8` assignments, not guessed; enough UART/I2C/CAN ports for GNSS + airspeed + telemetry radio + companion-computer link simultaneously; triple-redundant IMU preferred (matches what real aerospace-grade autopilots do) -- not a hard requirement for VLOS testing, but cheap insurance, and arguably more valuable here than on a pure fixed-wing given the added vibration environment from 4 lift motors (see Cross-Cutting Concerns).

**Interfaces:** UART to companion computer (the MAVLink/MAVSDK link this whole project is built around), UART/I2C to GNSS, analog or I2C to airspeed sensor, PWM/DShot outputs to lift-motor ESC + pusher ESC + servos (8 outputs minimum, see Requirements), CAN optional (for DroneCAN peripherals, not required for V1).

**Power:** typically fed from the power module's regulated 5V rail, not directly from the battery -- needs the power module chosen alongside it, not independently.

**Mass:** small (tens of grams) relative to everything else; not a real constraint on this component itself, but its mounting location matters for vibration isolation (see Cross-Cutting Concerns).

**Reliability/failure modes:** this is the one component whose failure the project's whole safety architecture (`SAFETY.md`) cannot route around -- if the FC dies, nothing downstream can compensate. Buy from a board with an active PX4-compatible firmware release, not end-of-life hardware.

**What I need to know before selecting:** UART port count required (companion link + GNSS + airspeed + reserve for telemetry radio during bench/RC testing = at least 3, ideally 4+); physical mounting envelope in whatever airframe gets chosen later (affects which board sizes are viable).

### Companion computer (Phase 7)

**Why it exists:** the mission-critical tier -- runs the (future) onboard mission agent, telemetry agent, network manager, and the LTE/WireGuard link. See `ARCHITECTURE.md` §4.

**Requirements:** enough CPU/RAM to run our C++ mission agent, MAVSDK, and the network stack without being the bottleneck; a UART or USB interface to the flight controller; Linux support with a maintained kernel (security patches matter given this is the internet-facing side of the vehicle, per `NETWORKING.md`'s threat model).

**Interfaces:** UART/USB to flight controller; USB to LTE modem (most modems are USB); Ethernet/WiFi optional for bench development convenience, not for flight.

**Power:** the single biggest avionics power draw after the motor itself -- a full SBC (not a microcontroller) pulling real current under CPU/network load, not idle-spec numbers. Must be sized into the power budget using worst-case (network-active, CPU-busy) draw, not typical.

**Mass:** meaningful (tens to ~100g depending on board + heatsink/case), and its physical placement matters for CG (see Cross-Cutting Concerns) -- generally wants to sit near the airframe's CG target, not wherever there happens to be space.

**Reliability/failure modes:** `SAFETY.md`'s "companion computer disappears -> PX4 falls back to its own built-in failsafes" already covers this -- the requirement here is just that its failure must be silent/safe (i.e., it can crash or lose power without taking the FC's UART bus down with it), which is mostly a wiring/isolation concern, not a board-selection one.

**What I need to know before selecting:** actual compute requirements once the C++ mission agent exists and its CPU profile is known (currently unbuilt, so this is provisional); USB port count needed (LTE modem at minimum, camera later if the vision extension in `project_prompt.txt` §24 happens).

### GNSS (Phase 7)

**Why it exists:** PX4's primary position source, feeding the EKF and everything downstream (mission navigation, geofence enforcement -- see `tests/simulation/geofence_breach.md` for why this matters in practice, not just theory).

**Requirements:** update rate compatible with PX4's EKF (5-10Hz typical, check current PX4 docs rather than assume); RTK optional (not needed for VLOS Open-category testing at the accuracy this project targets, real cost/complexity add).

**Interfaces:** UART or I2C to flight controller, per whatever the chosen FC's GNSS port expects.

**Power:** small, typically fed from the FC directly.

**Mass:** small, but antenna placement (not the receiver module itself) is the real physical-integration concern -- see Cross-Cutting Concerns.

**Reliability/failure modes:** `SAFETY.md`'s GPS_DEGRADED/GPS_LOST events are currently design-only (not yet tested, per `SIMULATION.md`'s Phase 6 findings -- blocked on SITL simulator backend, not hardware). Bench/HIL testing is exactly where this gap could finally get closed, since a real GNSS receiver can be physically shielded/moved indoors to induce a real degraded/lost fix, which SIH couldn't simulate.

**What I need to know before selecting:** whether the chosen FC has a dedicated GNSS connector (most Pixhawk-standard boards do, simplifying this to "buy the matching module").

### Airspeed sensor (Phase 8)

**Why it exists:** relevant to this airframe's cruise/fixed-wing flight phase specifically, not the hover phase -- PX4's TECS (total energy control system) and stall-margin logic need real airspeed during forward flight, not just GPS groundspeed (see `ARCHITECTURE.md` §17's theory list: airspeed vs. groundspeed is one of the fixed-wing concepts this project is meant to teach). Also feeds the VTOL transition logic itself: PX4 uses airspeed to help decide when the transition from hover to cruise (and back) is safe to complete.

**Requirements:** pitot-static compatible with PX4's airspeed driver; measurement range covering the airframe's expected stall speed through cruise speed with margin.

**Interfaces:** I2C or analog to flight controller.

**Power:** negligible.

**Mass:** negligible for the sensor; the pitot tube's mounting position (ahead of the propeller wash, away from fuselage-induced airflow disturbance) is the real engineering constraint, and it's airframe-specific -- hence Phase 8, not 7.

**Reliability/failure modes:** a blocked/iced pitot tube is a classic real-aviation failure mode (this is literally why real airliner incidents have happened) -- worth understanding conceptually now even though our VLOS test scope won't encounter icing. PX4 does have airspeed-consistency checks against GPS-derived speed; whether to rely on them or fly without airspeed-dependent modes if the sensor is suspect is a `SAFETY.md`-level policy question for later.

**What I need to know before selecting:** the airframe's expected speed envelope (depends on the launch-method and endurance decisions above).

### Power module (Phase 7)

**Why it exists:** provides the FC with regulated power and voltage/current sensing, isolated from the noisier motor/ESC power path.

**Requirements:** current rating with margin over total avionics + motor draw; voltage/current sensing accurate enough for PX4's battery-percentage estimation (directly relevant given `tests/simulation/battery_failsafe.md` -- the whole failsafe chain we just verified in simulation depends on accurate real readings in hardware).

**Interfaces:** battery input, regulated output to FC, analog/I2C sense lines to FC for voltage/current telemetry.

**Power:** this *is* the power budget's supply side -- see Cross-Cutting Concerns.

**Mass:** small, placement flexible.

**Reliability/failure modes:** a power module that reports voltage/current incorrectly silently breaks the entire battery-failsafe chain (`BAT_LOW_THR`/`BAT_CRIT_THR`/`BAT_EMERGEN_THR`) without any obvious symptom until the aircraft is actually critically low and the failsafe doesn't fire -- calibration/verification against a real multimeter belongs in the bench-test checklist (`TESTING.md`) once hardware exists.

**What I need to know before selecting:** total avionics current draw (companion computer + FC + GNSS + servos + LTE modem, worst case) -- can't size this until the companion computer and LTE modem are chosen.

### LTE modem (Phase 7)

**Why it exists:** the physical realization of the "4G/LTE" link `NETWORKING.md`'s whole tunnel design assumes.

**Requirements:** band compatibility with whatever carrier/SIM will actually be used at the test site (verify real coverage, don't assume); USB or M.2 interface matching the companion computer; power draw within budget under active transmission, not idle.

**Interfaces:** USB (most common for companion-computer-attached modems) or M.2 if the companion computer supports it natively.

**Power:** transmission-power draw spikes matter more than idle draw for budgeting -- a modem under poor signal conditions (exactly the scenario `SIMULATION.md`'s Phase 5 fault-injection tests are meant to approximate) will draw more, not less, power trying to maintain the link.

**Mass:** small for the modem itself; antenna placement matters more (see Cross-Cutting Concerns).

**Reliability/failure modes:** this is deliberately *not* flight-critical (the entire point of `SAFETY.md`'s design principle and the Phase 4/5 tests already proving it) -- so its failure mode requirement is simply "fails safely," which is already satisfied by the software architecture regardless of which modem is chosen.

**What I need to know before selecting:** the actual carrier and SIM plan for the test site (a "Decisions Only I Can Make First" item, not yet answered).

### SIM (Phase 7)

**Why it exists:** carrier connectivity for the modem above.

**Requirements:** data plan with coverage at the actual test site; check for carrier-side NAT/CGNAT behavior, though this project's WireGuard design (aircraft dials *out* to the rendezvous server) is specifically chosen to not care whether the SIM gets a public IP -- see `NETWORKING.md`.

**What I need to know before selecting:** the test site (again, not yet chosen).

### Independent safety link (Phase 7)

**Why it exists:** `SAFETY.md`'s "Independent Safety Override" section -- a human-supervised abort path wired directly to the flight controller, bypassing the companion computer entirely. This is a **hard requirement before any physical flight**, not optional hardening.

**Requirements:** conventional RC receiver wired directly to the FC's RC input, entirely independent of the companion computer/LTE path (so it survives everything Phase 4/5 already proved can safely disappear); PX4's `NAV_RCL_ACT`/`COM_RC_IN_MODE` params (already seen in this project's PX4 exploration, currently at defaults) will need review once this exists, since they govern RC-loss behavior which interacts with this safety link.

**Interfaces:** standard RC receiver-to-FC connection (PPM/SBUS/similar, per the chosen FC's supported protocols).

**Power:** negligible, typically FC-supplied.

**Reliability/failure modes:** this component's entire purpose *is* a failure-mode mitigation for everything else -- it doesn't have its own meaningful failure-mode analysis beyond "must not share a single point of failure with the companion computer or LTE path," which the "wired directly to the FC" requirement already satisfies by construction.

**What I need to know before selecting:** applicable national/EASA requirements for the independent safety link at the actual test site (a `FLIGHT_TEST_PLAN.md`/regulatory question, not a technical one).

### Wiring / power regulation (Phase 7, extended in Phase 8)

**Why it exists:** connects everything above; also where electrical-noise isolation actually gets implemented, not just specified.

**Requirements:** separate power rails (or at minimum, adequately filtered/isolated shared rails) for the FC+sensors vs. the motor/ESC path; wire gauge sized to actual current, not guessed.

**What I need to know before selecting:** finalized component list (this category is inherently last, since it depends on everything else's power/interface requirements).

### QuadPlane airframe (Phase 8)

**Why it exists:** carries everything else; already chosen (Stork VTOL Pack, see Target Airframe above) rather than an open selection, but its print settings and payload-bay usage still need engineering decisions.

**Requirements:** payload bay sized for companion computer + power module + modem + wiring with margin, within the stock design's volume (or requires a print modification if it doesn't fit -- to be checked once the companion computer is chosen); print material matched to structural role (the vendor's "LW-PLA/ASA + PC/PETG" split implies different parts use different materials for weight vs. strength -- worth understanding which part is which before printing, not just following a generic profile).

**What I need to know before selecting print settings/material:** payload mass budget (a "Decisions Only I Can Make First" item) and companion-computer physical dimensions, once chosen.

### Lift propulsion -- 4x motors + ESC (Phase 8)

**Why it exists:** provides vertical thrust for takeoff, landing, and hover -- the "quad" half of QuadPlane. Grouped as one system because the 4 motors, their ESC, and the props they turn can't be sized independently of each other or of the airframe's hover weight.

**Requirements:** combined thrust with real margin (commonly 2:1 thrust-to-weight or better for stable hover authority, not just enough to barely lift off) over the airframe's all-up weight (1800-3100g per the vendor spec, actual value depends on final component choices); a 4-in-1 ESC (matching the vendor's suggestion) simplifies wiring versus 4 separate ESCs, at the cost of a single point of failure for all 4 lift motors -- worth weighing against `SAFETY.md`'s failure-mode thinking once VTOL-specific failsafe policy exists (currently a gap, see below).

**What I need to know before selecting:** confirmed all-up weight once the airframe, avionics, and battery are closer to final (thrust-to-weight sizing can't be done from the vendor's weight *range* alone).

### Cruise propulsion -- pusher motor + ESC + propeller (Phase 8)

**Why it exists:** forward thrust for the fixed-wing cruise phase -- the "plane" half of QuadPlane. Sized independently from the lift system since it only needs to overcome cruise drag, not lift the aircraft's weight.

**Requirements:** enough thrust for the target cruise speed (50-70 km/h per the vendor spec) and climb-out margin during/after transition; propeller matched to the motor/voltage combination for cruise efficiency, not static thrust (different design point than the lift props, which are optimized for static/hover thrust).

**What I need to know before selecting:** target cruise speed/climb margin (feeds from the endurance decision above) and battery voltage (shared with the lift system if using one battery for both, which is the vendor's implied configuration at 4S-6S).

### Battery (Phase 8)

**Why it exists:** endurance and total-mass driver for *both* propulsion systems, if shared (the vendor's 4S-6S suggestion implies a single battery powering lift and cruise together, which is the simpler wiring choice but means hover and cruise current draw both come from the same pack and C-rating).

**Requirements:** capacity sized to the target hover time (higher current draw) and cruise time (lower current draw) as two separate legs of one mission profile, not a single "endurance" number; C-rating with margin over the *lift system's* peak draw specifically, since 4 simultaneous motors at takeoff/landing is almost certainly the highest instantaneous current draw the battery ever sees, higher than cruise; mass traded directly against endurance -- there's no way around this being an iteration, not a lookup.

**What I need to know before selecting:** target endurance split between hover and cruise (a "Decisions Only I Can Make First" item, now a two-part question because of VTOL).

### Servos (Phase 8)

**Why it exists:** actuates ailerons/elevator/rudder during the cruise/fixed-wing flight phase, per PX4's control outputs. Not used during pure hover (attitude control there comes from differential lift-motor thrust, same as a multicopter).

**Requirements:** 4 servos per the vendor spec (2 ailerons via Y-cable counted as one PX4 control channel, elevator, rudder -- matches `sihsim_standard_vtol`'s `CA_SV_CS_COUNT=3` control-surface-type count); torque sufficient for control-surface aerodynamic loads at cruise airspeed (not just static bench torque); voltage compatible with whatever the power module's servo rail supplies.

**What I need to know before selecting:** control-surface sizing, which comes from the airframe (already fixed by the Stork design, so this becomes a more straightforward lookup than it would be for a from-scratch airframe).

## Cross-Cutting Concerns

These aren't separate components -- they're constraints that any component selection has to satisfy, and the reason the categories above keep saying "what I need to know before selecting."

- **Weight:** running mass budget, every category above contributes a line item; must stay under the airframe's rated all-up-weight range (1800-3100g) with margin, not exactly at the limit. Weight matters more for a VTOL than it did for the pure-fixed-wing plan, since hover thrust-to-weight is a hard requirement, not just a performance nicety.
- **Center of gravity:** battery and companion computer are the two heaviest movable-position items -- their placement is often how CG gets tuned to the airframe's target range, rather than needing to be exactly fixed in advance. For a QuadPlane, CG also has to sit correctly relative to the four lift-motor thrust points (for balanced hover) *and* the wing's aerodynamic center (for stable cruise) -- a tighter joint constraint than a pure fixed-wing or pure multicopter would have individually.
- **Power budget:** now two profiles, not one -- hover current draw (all 4 lift motors simultaneously, the likely peak-current event of the whole mission) and cruise current draw (1 pusher motor + control surfaces, much lower), each checked against battery capacity/C-rating separately, plus avionics load (companion computer, LTE modem) present in both. This is a real spreadsheet exercise once component candidates exist, not a rule of thumb.
- **Electrical noise:** now 5 motors/ESCs (4 lift + 1 cruise) instead of 1, so more potential noise sources coupling into GNSS, airspeed, or LTE antennas/receivers -- mitigated by physical separation and power-rail isolation, verified on the bench (Phase 7) before it's harder to debug in the airframe (Phase 8).
- **Antenna placement:** GNSS, LTE, and RC/telemetry antennas need mutual isolation and, for GNSS specifically, a clear sky view unobstructed by the airframe or other antennas -- airframe-specific, hence a Phase 8 concern even though the GNSS/LTE hardware itself is chosen in Phase 7.
- **Cooling:** companion computer thermal management inside an enclosed fuselage -- a real constraint once a full SBC is drawing sustained CPU/network load in a non-ventilated bay, not a theoretical one.
- **Vibration:** IMU isolation from motor/airframe vibration modes -- more relevant here than on a single-motor fixed-wing, since 4 simultaneous lift motors during takeoff/landing/hover put the FC in a higher-vibration environment than 1 cruise motor alone; soft-mounting the FC (and preferring triple-redundant IMU, see Flight controller section) matters more, not something to skip because a board has "built-in vibration damping" marketing.
- **Failure modes:** every component's individual failure mode feeds `SAFETY.md` -- this is where hardware selection and the failsafe state machine actually connect, not two separate concerns. VTOL adds failure modes that didn't exist in the pure-fixed-wing plan (single lift motor out during hover, failed/aborted transition) that are not yet analyzed in `SAFETY.md` -- tracked as a gap, not silently assumed safe.

## Status

Requirements definition underway (this document), now updated for the QuadPlane VTOL decision (see [ADR 0002](docs/adr/0002-quadplane-vtol-airframe.md)). No parts purchased. Phase 7 (HIL/bench) needs answers to the "Decisions Only I Can Make First" items above before the Phase-7-relevant categories (flight controller, companion computer, power module, GNSS, LTE modem/SIM, independent safety link) can turn into actual candidate parts. The flight controller category specifically is now gated on finding a board with *confirmed* official PX4 support -- the vendor's suggested SpeedyBee F405 Wing does not appear in PX4's supported board list (checked directly against `PX4-Autopilot/boards`), so this can't be assumed and needs to be resolved before any FC purchase.
