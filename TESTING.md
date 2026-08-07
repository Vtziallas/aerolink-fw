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
| SYS-FLT-001 | _pending_ | Not yet implemented |
| SYS-FLT-002 | _pending_ | Not yet implemented |
| SYS-NET-001 | _pending_ | Not yet implemented |
| SYS-NET-002 | _pending_ | Not yet implemented |
| SYS-SEC-001 | _pending_ | Not yet implemented |
| SYS-SEC-002 | _pending_ | Not yet implemented |
| SYS-MIS-001 | _pending_ | Not yet implemented |
| SYS-MIS-002 | _pending_ | Not yet implemented |
| SYS-LOG-001 | _pending_ | Not yet implemented |
| SYS-SAF-001 | _pending_ | Not yet implemented |

## Status

No tests exist yet. This table is updated as each requirement gets real, passing coverage — never marked done from a single manual run.
