# Testing

"It worked once" is not acceptable engineering evidence for a system that flies. Every system requirement in `ARCHITECTURE.md` §8 must map to at least one test before it is relied on.

## Testing Levels

| Level | Scope | Runs |
|---|---|---|
| Unit tests | Individual functions/classes (mission validation logic, command schema checks, state machine transitions) | Every commit (CI) |
| Integration tests | Multiple components together (backend ↔ mission agent over a real or loopback network) | Every commit (CI) |
| Simulation tests | Full stack against PX4 SITL, including scripted missions | CI where feasible, manually otherwise |
| Fault-injection tests | Simulation tests plus injected latency/loss/outage/malformed input (see `SIMULATION.md`) | Before each phase gate from Phase 5 onward |
| Hardware-in-the-loop tests | Real flight controller + companion computer, simulated aerodynamics | Phase 7 |
| Bench tests | Full physical hardware stack, no flight | Phase 7–8 |
| Controlled flight tests | Lawful VLOS flight per `FLIGHT_TEST_PLAN.md` | Phase 9 |

## Format

Every test — especially simulation and fault-injection tests — is defined with:

- **SETUP** — starting conditions
- **ACTION** — what is done/injected
- **EXPECTED RESULT** — what the system should do
- **PASS CRITERIA** — how success is objectively determined

## Requirements Traceability

Each `SYS-*` requirement in `ARCHITECTURE.md` gets a traceability entry here once its first test exists:

| Requirement | Test(s) | Status |
|---|---|---|
| SYS-FLT-001 | `network_outage_during_mission.md`, `latency_injection.md`, `packet_loss_injection.md`, `link_interruption_reconnection.md` | Passing (manual simulation tests, 2026-08-11) |
| SYS-FLT-002 | `network_outage_during_mission.md`, `link_interruption_reconnection.md` | Passing (manual simulation tests, 2026-08-11) |
| SYS-NET-001 | `network_outage_during_mission.md`, `link_interruption_reconnection.md` | Passing (manual simulation tests, 2026-08-11) |
| SYS-NET-002 | _pending_ | Not yet implemented — needs the onboard network manager (Phase 4) |
| SYS-SEC-001 | _pending_ | Not yet implemented — no command auth yet (Phase 4, `NETWORKING.md`) |
| SYS-SEC-002 | _pending_ | Not yet implemented — no replay protection yet (Phase 4) |
| SYS-MIS-001 | `ground-station/backend/tests/test_mission.py` | Passing (automated unit tests) |
| SYS-MIS-002 | _pending_ | Not yet implemented — command-vs-state validation is Phase 4 command-API territory |
| SYS-LOG-001 | _pending_ | Not yet implemented — no persistence/audit trail yet (Phase 13 in the original roadmap numbering) |
| SYS-SAF-001 | _pending_ | Not yet implemented — needs physical hardware (Phase 7+) |
| SYS-SEC-003 | `tests/simulation/network_split_isolation.md` | Passing (manual simulation test, 2026-08-11) |
| SYS-SAF-002 | `tests/simulation/battery_failsafe.md` | Passing (manual simulation test, 2026-08-11) |

## Status

Automated unit test coverage exists for mission validation (`ground-station/backend/tests/`, run on every backend change). Six manual simulation tests exist in `tests/simulation/` (network-outage/failsafe, network-split isolation, latency, packet loss, link interruption/reconnection, battery failsafe) — all passing, but manual, not yet automated into CI (formalizing that is Phase 5/6 follow-up work, not done). Everything else in the table above is genuinely not implemented yet, not just untested.
