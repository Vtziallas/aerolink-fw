# Telemetry Agent Design

Status: approved, not yet implemented (design phase, 2026-08-22).

## Scope

This document covers the **Telemetry Agent** only -- the second of the three
onboard companion-computer services `ARCHITECTURE.md` describes
(`onboard/mission-agent/`, now built; `onboard/telemetry-agent/`, this
document; `onboard/network-manager/`, still deferred). `ARCHITECTURE.md`
scopes it as: "Subscribes to MAVLink streams, buckets them into rate classes,
forwards over the WireGuard tunnel, buffers/drops intelligently under bad
links."

## Why now

The Mission Agent's own `TelemetryPublisher` (`onboard/mission-agent/src/telemetry_publisher.h/.cpp`)
has always been a deliberately minimal stand-in for this -- a flat periodic
push of the full `StateSnapshot` as JSON, with no rate-classing and no
backpressure policy under a degraded link. `SIMULATION.md`'s "What this does
NOT yet close" section flags this explicitly. This project also already has
real fault-injection test infrastructure (namespace-based latency/packet-loss
injection, `tests/simulation/latency_injection.md`) to verify degraded-link
telemetry behavior against, rather than needing new infrastructure the way
Phase 7 hardware work does -- making this a well-scoped, directly-testable
next step.

## Architecture

```
Frontend (browser)
   |  WebSocket /ws/telemetry (unchanged)
Backend (Python/FastAPI)
   |  JSON over TCP (NEW -- separate connection from the Mission Agent's)
Telemetry Agent (C++, NEW)
   |  MAVSDK, PX4's second onboard MAVLink instance (udp://:14030)
PX4
   |  MAVSDK, PX4's first onboard MAVLink instance (udp://:14540)
Mission Agent (C++, existing)
```

The Telemetry Agent and Mission Agent are genuinely independent processes,
each with its own MAVLink connection to PX4 and its own TCP connection to the
backend. Killing either one does not affect the other, PX4, or the aircraft's
flight -- the same process-isolation property the companion-service-restart
fault test (`tests/simulation/companion_service_restart.md`) already proved
for the Mission Agent.

**Why a second, genuinely separate MAVLink instance, not a second client on
the Mission Agent's existing one:** PX4/`mavsdk_server` can only reliably
track one learned UDP partner per instance -- a second independent MAVSDK
client on the *same* port previously wedged silently forever during this
project's own stress-test debugging (see `SIMULATION.md`'s Phase-history
findings). Reading PX4's actual SITL startup script
(`ROMFS/px4fmu_common/init.d-posix/px4-rc.mavlink`) found the fix already
provisioned: a second onboard-mode MAVLink instance, explicitly commented
"Onboard link to camera" (PX4 listens on local port 14280, sends to remote
port 14030), currently unused by anything in this project. It runs the same
`-m onboard` mode as the Mission Agent's own link (just a different
instance/port pair, 14580/14540), so the same telemetry set the Mission
Agent already successfully subscribes to is available here too, with zero
collision risk since it's an entirely separate PX4-side MAVLink module.

**Network topology (simulation):** the Telemetry Agent runs inside
`aircraft-net` alongside PX4 and the Mission Agent, reusing the existing
WireGuard/network-namespace infrastructure unchanged. The backend
(`ground-net`) opens a second, independent tunnel connection to the
Telemetry Agent's TCP port.

**This retires the Mission Agent's `TelemetryPublisher`.** It was always a
stand-in for this exact service, never the intended long-term telemetry
owner. `onboard/mission-agent/src/telemetry_publisher.h/.cpp` and its test
are deleted; `AgentServer` drops the `ITelemetrySink` interface and goes back
to being command-only. The backend swaps its telemetry source from the
Mission Agent's connection to the new Telemetry Agent's, feeding the same
`/ws/telemetry` WebSocket -- the frontend needs no changes at all.

## Protocol

Newline-delimited JSON over a single persistent TCP connection, on a new
port separate from the Mission Agent's (5760). One-way, agent -> backend --
telemetry is a push, not a request/ack exchange, so there's no command
schema and no ack chain to design here.

```json
{"class": "critical", "field": "armed", "value": true, "timestamp": "ISO8601"}
{"class": "position", "latitude_deg": 47.398, "longitude_deg": 8.546, "relative_altitude_m": 30.2, "timestamp": "ISO8601"}
{"class": "bulk", "battery_voltage_v": 16.1, "airspeed_m_s": 12.3, "timestamp": "ISO8601"}
```

Each tier serializes and sends independently, at its own cadence -- see
Rate Classes below. `class` lets the backend's `TelemetryAgentClient`
re-assemble a merged snapshot for `/ws/telemetry` without needing to know
which tier a field came from.

## Rate Classes

Three tiers, split by criticality, each with its own buffer shape and
backpressure policy -- this differentiated behavior is the actual point of
"buckets them into rate classes... buffers/drops intelligently," not just
three independent timers:

- **Critical/event** (armed state, flight mode, health, mission-progress
  changes): a real bounded FIFO queue, depth 200, each message given a real
  write budget (2000ms, mirroring the Mission Agent's own `kAckWriteBudgetMs`
  precedent -- "this matters, wait for it"). Drops oldest-first only if the
  queue actually fills, which only happens under a genuinely sustained
  outage. Nothing here is coalesced: three consecutive mission-progress
  events are three distinct facts an operator should not silently lose.
- **Position** (position/attitude, ~10Hz): a single-slot *coalescing*
  buffer, not a queue -- a new sample overwrites the pending one. There is no
  value in ever delivering a stale intermediate position once a newer one
  exists. Zero-wait write (matches the Mission Agent's existing
  `kTelemetryWriteBudgetMs = 0`): if the socket is busy, skip this tick, try
  again next tick.
- **Bulk** (battery, airspeed/groundspeed/heading, ~1Hz): same single-slot
  coalescing + zero-wait policy as Position, for the same reason -- an old
  battery reading is worthless once a newer one exists.

## Internal Components

- **`MavlinkTelemetrySource`** -- wraps a MAVSDK `System`, connects to PX4's
  second onboard instance (`udp://:14030`). Subscribes to armed, flight
  mode, health, mission progress, position, attitude, battery,
  airspeed/groundspeed/heading, and routes each update into the
  `RateClassMux`.
- **`RateClassMux`** -- owns the three tiers' buffers and their distinct
  policies described above. Pure logic given a stream of typed updates --
  the primary unit-test surface, no PX4 or socket dependency.
- **`TelemetryServer`** -- TCP listener, one persistent backend connection.
  Drains each tier per its own cadence/policy and writes newline-delimited
  JSON. Reuses the Mission Agent's `AgentServer` socket-hardening pattern
  directly (`SIGPIPE` ignored, `MSG_NOSIGNAL`, non-blocking socket +
  `poll()`-based backpressure) -- that was a real, hard-won fix from the
  Mission Agent's final review; no reason to rediscover the same bug class.
- **`main.cpp`** -- wiring, same shape as the Mission Agent's.

**No shared library with the Mission Agent.** Both projects need MAVSDK
connection boilerplate, but they connect to different MAVLink instances with
different subscription sets, and coupling two independently-restartable
processes to a shared internal library works against the isolation reason
they're separate processes in the first place. Small, duplicated, focused
code beats a premature shared abstraction across a two-consumer boundary.

## Concurrency

Same reality as the Mission Agent: MAVSDK's C++ SDK runs its own internal
thread(s) for telemetry callbacks, so this is unavoidably a small
multi-threaded program. `RateClassMux`'s buffers are mutex-protected; the TCP
server can run on the main thread. Standard practice for a bridging service
like this.

## Error Handling

- **PX4 connection lost/not yet established:** stop pushing fresh samples,
  matching the Mission Agent's own `is_connected()` pattern -- no fabricated
  data sent downstream.
- **Backend TCP connection drops:** the Telemetry Agent keeps running per
  the tier policies above (buffering critical events, coalescing the rest);
  when the backend reconnects it resumes normally. The backend's
  `TelemetryAgentClient` reuses the exact reconnect-with-backoff loop already
  built and proven for `MissionAgentClient` this project -- no new pattern to
  invent.
- **Telemetry Agent process crashes:** PX4 keeps flying (separate MAVLink
  instance, unaffected) and the Mission Agent keeps accepting commands
  (separate process, separate port) -- only the telemetry display goes
  stale. Mission control never depends on telemetry delivery. This mirrors
  `tests/simulation/companion_service_restart.md`'s finding for the Mission
  Agent, and should get its own equivalent live fault test once built.
- **Malformed/unexpected MAVSDK callback data:** log and skip that one
  sample rather than crashing the process -- telemetry loss for one field on
  one tick is not worth taking the whole service down over.

## Testing Approach

- **Unit tests** for `RateClassMux` -- pure logic, no PX4 or socket needed:
  each tier's coalescing vs. queueing behavior, backpressure/drop behavior
  once a tier's buffer is full or the write budget is exceeded. Same Catch2
  framework as the Mission Agent.
- **Unit/integration tests** for `TelemetryServer`'s TCP protocol using a
  fake sink, same style as `test_agent_server.cpp`.
- **Live-only harness** for `MavlinkTelemetrySource`, mirroring the Mission
  Agent's `manual_mavlink_test.cpp` -- MAVSDK connection code isn't
  meaningfully unit-testable without real PX4.
- **Fault-injection re-verification** reusing the existing latency/packet-loss
  namespace infrastructure from `tests/simulation/latency_injection.md` --
  confirms the tiers actually behave differently under a real degraded link,
  not just in isolated unit tests. Documented as a new
  `tests/simulation/telemetry_agent_rate_classing.md`, same SETUP/ACTION/
  EXPECTED RESULT/PASS CRITERIA format as everything else in
  `tests/simulation/`.
- **Companion-service-restart re-verification**, mirroring
  `companion_service_restart.md`: kill the Telemetry Agent mid-flight,
  confirm PX4 and the Mission Agent are unaffected and the backend detects
  and recovers from the telemetry-link drop independently of mission
  control.
- **Build system:** CMake + nlohmann/json, matching the Mission Agent
  exactly.

## Consequences

- **Backend rework required:** a new `TelemetryAgentClient`
  (`ground-station/backend/app/telemetry_agent_client.py`, mirroring
  `MissionAgentClient`'s reconnect logic), and `app/vehicle.py`/`app/main.py`
  swap their telemetry source to it. `/ws/telemetry`'s outward shape and the
  frontend should not need to change.
- **Mission Agent shrinks:** `TelemetryPublisher` and its test are deleted;
  `ITelemetrySink` is removed from `AgentServer`, which goes back to being
  purely command/ack. `CMakeLists.txt` updated accordingly. This is a real,
  visible reduction in the Mission Agent's scope, not just an addition
  elsewhere -- worth calling out since it changes an already-shipped,
  already-tested component.
- **Simulation launch scripts affected:** a new `launch-telemetry-agent-only.sh`
  (single-process, matching `launch-px4-only.sh`/`launch-agent-only.sh`'s
  fault-testing pattern), and `launch-aircraft.sh` gets the Telemetry Agent
  added as a third co-launched process for normal (non-fault-testing) use.
  `launch-ground.sh` needs a new env var pointing at the Telemetry Agent's
  address/port, alongside the existing Mission Agent one.
- **Not in scope for this pass:** the Network Manager (still deferred,
  ARCHITECTURE.md's third onboard service) and any LTE-specific link-quality
  metrics it would own -- the Telemetry Agent's own view of its backend
  connection state is sufficient for this pass's link-degradation handling.
