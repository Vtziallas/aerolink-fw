# AeroLink-FW

Experimental cellular-connected autonomous fixed-wing UAV platform.

> **Status:** Phase 0 — architecture and planning. No flight code yet. This README will be filled out (demo video, results, "what I learned") as milestones are actually completed — nothing here is claimed until it's implemented and tested.

## Overview

AeroLink-FW is a fixed-wing UAV that receives high-level mission instructions (upload mission, fly waypoints, loiter, return-to-launch, abort) from a ground control station over a secured 4G/LTE link, and executes them autonomously on a PX4-based autopilot. The ground link is never flight-critical: the aircraft is designed to remain safe and complete or gracefully abort its mission if cellular connectivity, the ground backend, or the browser disappear.

Scope: civilian engineering education, research, simulation, and lawful VLOS flight testing. No weapons, payload delivery, or covert surveillance functionality. See [`SAFETY.md`](SAFETY.md) and [`docs/flight-tests/`](docs/flight-tests/) for the regulatory boundaries this project operates within.

## Why I Built It

Portfolio/research project built while working toward aerospace software engineering roles (autonomous systems, flight software, robotics). Deliberately structured as a systems-engineering exercise, not just a flying demo: requirements traceability, failsafe design, simulation-first development, and fault-injection testing before any hardware is involved.

## Architecture

See [`ARCHITECTURE.md`](ARCHITECTURE.md) for the full system design, component breakdown, and flight-critical / mission-critical / non-critical separation.

## Repository Layout

```
ground-station/    React/TS frontend + Python/FastAPI backend
onboard/            C++/MAVSDK mission agent, telemetry agent, network manager
shared/             Protocol schemas shared across components
simulation/         PX4 SITL configs, network fault injection, test scenarios
tools/              Log analysis, mission generation
tests/              Integration, simulation, and protocol tests
infrastructure/     WireGuard rendezvous config, CI
docs/               Architecture, safety, networking, hardware, flight-test docs, ADRs
```

## Development Roadmap

Phase 0 (architecture) → Phase 1 (PX4 SITL) → Phase 2 (GCS against SITL) → Phase 3 (mission API) → Phase 4 (simulated LTE network) → Phase 5 (network fault injection) → Phase 6 (failsafe testing) → Phase 7 (HIL/bench) → Phase 8 (physical integration) → Phase 9 (lawful VLOS flight testing).

Details in [`ARCHITECTURE.md`](ARCHITECTURE.md).

## Documentation

- [`ARCHITECTURE.md`](ARCHITECTURE.md) — system design
- [`SAFETY.md`](SAFETY.md) — failsafe state machine, safety requirements
- [`NETWORKING.md`](NETWORKING.md) — secure ground/air network design
- [`SIMULATION.md`](SIMULATION.md) — SITL and fault-injection approach
- [`HARDWARE.md`](HARDWARE.md) — hardware requirements (filled in ahead of Phase 7/8)
- [`TESTING.md`](TESTING.md) — testing levels and philosophy
- [`FLIGHT_TEST_PLAN.md`](FLIGHT_TEST_PLAN.md) — lawful VLOS flight test procedure (Phase 9)
- [`docs/adr/`](docs/adr/) — architecture decision records

## License

TBD.
