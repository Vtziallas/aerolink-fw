# Hardware

Phase 7 (hardware-in-the-loop / bench testing) starts now that Phase 6 (failsafe testing in simulation) is complete. This document defines engineering requirements for each component category -- not specific parts, not shopping links. Component selection comes after requirements are clear, per `project_prompt.txt` §16.

**HIL vs. physical integration, and why that split matters here:** "Hardware-in-the-loop" means *real flight-controller hardware*, running PX4 (not SITL), against *simulated aerodynamics* -- the physics stay in software, only the FC and companion computer become real. No airframe, motor, or servos are needed for that. Those come in Phase 8 (physical aircraft integration). Splitting the component list this way avoids scope creep: don't spec or buy an airframe/motor/ESC/propeller/servo stack before HIL has validated that the real flight controller + companion computer + network stack behaves the way SITL predicted.

| Needed for Phase 7 (HIL/bench) | Needed additionally for Phase 8 (physical integration) |
|---|---|
| Flight controller | Fixed-wing airframe |
| Companion computer | Electric motor |
| Power module (bench supply is fine initially) | ESC |
| GNSS (real fix, even sitting outdoors) | Propeller |
| LTE modem + SIM | Flight battery |
| Independent safety link (RC receiver) | Servos |
| Wiring/power regulation for the bench setup | Airspeed sensor (needs real airflow to validate) |
| | Antennas mounted on the actual airframe (placement is airframe-specific) |

## Decisions Only I Can Make First

Before any category below can turn into an actual part number, these need answers -- they're not engineering derivations, they're project-scope choices:

- **Target endurance** (minutes of flight per battery) -- drives battery capacity, which drives mass, which drives motor/prop sizing, which feeds back into power budget. Nothing downstream is stable until this is picked.
- **Payload/avionics mass budget** -- companion computer + LTE modem + power module + wiring, as a fixed mass the airframe must carry beyond its own structure and propulsion. Rough estimate needed even before an airframe is chosen, since it constrains which airframes are viable.
- **Launch method** -- hand-launch vs. a runway/dolly. Affects airframe choice (some airframes are not hand-launch-safe), motor sizing for initial climb-out margin, and the VLOS test site requirements in `FLIGHT_TEST_PLAN.md`.
- **Approximate budget** -- component categories below span an order of magnitude in cost depending on grade (hobby vs. semi-professional), and that mostly determines which specific parts are even worth researching.
- **Test site constraints** (from `FLIGHT_TEST_PLAN.md`, once a real site is identified) -- available area affects airframe wing loading/stall speed tolerance, and altitude limits affect climb performance margins needed.

## Component Categories

### Flight controller (Phase 7)

**Why it exists:** runs PX4 itself -- the flight-critical tier in `ARCHITECTURE.md` §4. Everything else is built around whatever this board can interface with.

**Requirements:** Pixhawk-standard connector/pinout (keeps us on well-supported PX4 hardware rather than a bespoke board); enough UART/I2C/CAN ports for GNSS + airspeed + telemetry radio + companion-computer link simultaneously; triple-redundant IMU preferred (matches what real aerospace-grade autopilots do, and is standard on current Pixhawk-class boards) -- not a hard requirement for VLOS testing, but cheap insurance.

**Interfaces:** UART to companion computer (the MAVLink/MAVSDK link this whole project is built around), UART/I2C to GNSS, analog or I2C to airspeed sensor, PWM/DShot outputs to ESC + servos, CAN optional (for DroneCAN peripherals, not required for V1).

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

**Why it exists:** fixed-wing-specific -- PX4's TECS (total energy control system) and stall-margin logic need real airspeed, not just GPS groundspeed (see `ARCHITECTURE.md` §17's theory list: airspeed vs. groundspeed is one of the fixed-wing concepts this project is meant to teach).

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

### Fixed-wing airframe (Phase 8)

**Why it exists:** carries everything else; determines wing loading, stall speed, and payload bay volume, which cascade into every other Phase 8 category.

**Requirements:** payload bay sized for companion computer + power module + modem + wiring with margin; wing loading appropriate to the target launch method and test-site wind conditions; hand-launch-safe structure if that's the chosen launch method.

**What I need to know before selecting:** payload mass budget and launch method (both "Decisions Only I Can Make First" items).

### Electric motor, ESC, propeller (Phase 8)

Grouped because they can't be specified independently of each other -- motor KV, prop pitch/diameter, and battery voltage form one coupled system, and ESC current rating must have margin over whatever that system's peak draw turns out to be.

**Requirements:** enough thrust for the target climb performance at the chosen airframe's total mass (structure + avionics + battery); ESC current rating with real margin (not exactly matched) over motor max draw; propeller matched to the motor/voltage combination for efficient cruise, not just static thrust.

**What I need to know before selecting:** total aircraft mass (which itself depends on the battery, which depends on endurance target) -- this is the most interdependent category in the whole list, expect to iterate.

### Battery (Phase 8)

**Why it exists:** endurance and total-mass driver.

**Requirements:** capacity sized to the target endurance at the motor system's actual cruise current draw (not peak, not idle); C-rating with margin over the motor's max current draw; mass traded directly against endurance -- there's no way around this being an iteration, not a lookup.

**What I need to know before selecting:** target endurance (a "Decisions Only I Can Make First" item).

### Servos (Phase 8)

**Why it exists:** actuates ailerons/elevator/rudder per PX4's control outputs.

**Requirements:** torque sufficient for control-surface aerodynamic loads at cruise airspeed (not just static bench torque); voltage compatible with whatever the power module's servo rail supplies.

**What I need to know before selecting:** control-surface sizing, which comes from the airframe choice.

## Cross-Cutting Concerns

These aren't separate components -- they're constraints that any component selection has to satisfy, and the reason the categories above keep saying "what I need to know before selecting."

- **Weight:** running mass budget, every category above contributes a line item; must stay under the airframe's rated payload capacity with margin, not exactly at the limit.
- **Center of gravity:** battery and companion computer are the two heaviest movable-position items -- their placement is often how CG gets tuned to the airframe's target range, rather than needing to be exactly fixed in advance.
- **Power budget:** total current draw (motor at cruise + all avionics under worst-case load, especially the companion computer and LTE modem under active use) against battery capacity and C-rating, cross-checked against the target endurance decision above. This is a real spreadsheet exercise once component candidates exist, not a rule of thumb.
- **Electrical noise:** motor/ESC switching noise coupling into GNSS, airspeed, or LTE antennas/receivers -- mitigated by physical separation and power-rail isolation, verified on the bench (Phase 7) before it's harder to debug in the airframe (Phase 8).
- **Antenna placement:** GNSS, LTE, and RC/telemetry antennas need mutual isolation and, for GNSS specifically, a clear sky view unobstructed by the airframe or other antennas -- airframe-specific, hence a Phase 8 concern even though the GNSS/LTE hardware itself is chosen in Phase 7.
- **Cooling:** companion computer thermal management inside an enclosed fuselage -- a real constraint once a full SBC is drawing sustained CPU/network load in a non-ventilated bay, not a theoretical one.
- **Vibration:** IMU isolation from motor/airframe vibration modes -- standard practice is soft-mounting the FC, not something to skip because a board has "built-in vibration damping" marketing.
- **Failure modes:** every component's individual failure mode feeds `SAFETY.md` -- this is where hardware selection and the failsafe state machine actually connect, not two separate concerns.

## Status

Requirements definition underway (this document). No parts purchased. Phase 7 (HIL/bench) needs answers to the "Decisions Only I Can Make First" items above before the Phase-7-relevant categories (flight controller, companion computer, power module, GNSS, LTE modem/SIM, independent safety link) can turn into actual candidate parts.
