# Hardware

No hardware is purchased or specified until the simulation stack (Phases 0–6) works reliably. This document exists now only to record the requirement categories and the parameters that will drive component selection later — not to shop.

## Component Categories (requirements to define before selection)

For each category below, before choosing a specific part: define requirements, interfaces, power draw, mass budget, reliability/failure modes, and what has to be known about the rest of the airframe first.

- **Fixed-wing airframe** — wing loading, payload/avionics bay size, hand-launch vs. runway
- **Electric motor** — thrust required for target mass + climb performance, KV vs. prop/voltage match
- **ESC** — current rating margin over motor max draw, telemetry feedback support
- **Propeller** — matched to motor/ESC/airspeed target, static vs. cruise thrust tradeoff
- **Battery** — capacity vs. mass tradeoff, C-rating for motor current, endurance target
- **Servos** — torque for control-surface loads at cruise airspeed, voltage compatibility
- **Flight controller** — Pixhawk-compatible, sensor suite, I/O for GNSS/airspeed/servos
- **GNSS** — accuracy, update rate, interference immunity, antenna placement away from LTE/other RF
- **Airspeed sensor** — pitot-static, required for fixed-wing stall margin and TECS control
- **Power module** — voltage/current sensing to the flight controller, isolated from motor noise
- **Companion computer** — CPU/RAM for the mission agent + telemetry agent, power draw, interface to flight controller
- **LTE modem** — carrier/band compatibility, power draw, antenna requirements
- **SIM** — data plan with adequate coverage for the test area
- **Antennas** — GNSS, LTE, and (test-phase) RC/telemetry antennas placed to avoid mutual interference
- **Independent safety link** — separate RC hardware wired directly to the flight controller, independent of the companion computer (see `SAFETY.md`)
- **Wiring / power regulation** — voltage rails for FC, companion computer, servos, modem; isolation from motor/ESC electrical noise

## Cross-cutting concerns to resolve before any purchase

- **Weight** — full component mass budget vs. airframe payload capacity
- **Center of gravity** — placement of battery/companion computer/modem relative to CG target
- **Power budget** — total current draw (motor + avionics + companion computer + modem) vs. battery capacity and endurance target
- **Electrical noise** — motor/ESC noise coupling into GNSS, airspeed, or LTE antennas
- **Antenna placement** — GNSS, LTE, RC/telemetry mutual isolation
- **Cooling** — companion computer thermal management in an enclosed fuselage
- **Vibration** — IMU isolation from motor/airframe vibration modes
- **Failure modes** — what happens to flight safety if each component fails individually (feeds directly into `SAFETY.md`)

## Status

Not started. Populated with actual part selections and reasoning once Phase 6 (failsafe testing in simulation) is complete and Phase 7 (HIL/bench testing) begins.
