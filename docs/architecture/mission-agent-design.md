# Mission Agent Design

Status: approved, not yet implemented (design phase, 2026-08-14).

## Scope

This document covers the **Mission Agent** only -- one of the three onboard
companion-computer services `ARCHITECTURE.md` describes (`onboard/mission-agent/`,
`onboard/telemetry-agent/`, `onboard/network-manager/`, all currently empty
scaffolding). The other two are deliberately out of scope here and will get
their own design docs later: building all three at once was judged too large
for one design/implementation pass, and the Mission Agent is the core piece --
command validation, failsafe-relevant state tracking, and the actual MAVSDK
relay to PX4.

## Why now

Everything built in Phases 1-6 has the Python ground-station backend talking
directly to PX4 over MAVSDK -- a deliberate simplification while the
software/network architecture was being proven out. That was never the real
architecture; `ARCHITECTURE.md`'s own component diagram has always shown the
backend talking to a Mission Agent, which is the only thing with a MAVLink/MAVSDK
link to PX4. This closes that gap, and is also the project's deliberate C++
learning vehicle (`ARCHITECTURE.md` Technology Stack: "Deliberate C++ learning
vehicle; MAVSDK gives a typed abstraction over MAVLink").

## Architecture

```
Frontend (browser)
   |  REST/WebSocket (unchanged)
Backend (Python/FastAPI)
   |  JSON over TCP (NEW -- replaces the backend's direct MAVSDK connection)
Mission Agent (C++, NEW)
   |  MAVSDK, local onboard link (udp://:14540)
PX4
```

The frontend-facing REST/WebSocket API doesn't change at all. Only the
backend's *internal* implementation changes: `app/vehicle.py`'s
`VehicleConnection` currently holds a MAVSDK `System` directly and talks to
PX4 itself -- it will instead become a TCP/JSON client of the Mission Agent,
with the same external interface (`upload_mission()`, `start_mission()`,
`return_to_launch()`, telemetry state) so `app/main.py`'s endpoints don't need
to change.

**Why the local/onboard MAVLink instance, not the GCS-style tunnel instance:**
the Mission Agent runs alongside PX4 (same host in simulation, physically wired
together on real hardware), so it should use the fixed local link PX4's
"onboard" instance provides (port 14540, the same one Phase 1-3 originally
used) rather than the GCS-style instance (18570) that was only ever needed
because the backend had to reach PX4 across the WireGuard tunnel with no
intermediary. Once the Mission Agent exists, nothing needs to speak raw MAVLink
across that tunnel anymore -- the tunnel instead carries this document's new
JSON/TCP protocol. See `NETWORKING.md`'s Phase 4 finding for why this
onboard-vs-GCS instance distinction matters at all.

**Network topology (simulation):** the Mission Agent runs inside the
`aircraft-net` namespace (same as PX4), reusing all the existing
WireGuard/network-namespace infrastructure from Phase 4 unchanged. The backend
(in `ground-net`) connects to the Mission Agent's TCP port across the same
tunnel that previously carried MAVLink directly.

## Protocol

Newline-delimited JSON over a single persistent TCP connection. V1 assumes a
single ground station (one operator, matching the project's whole PC-based-GCS
framing) -- no need to support multiple simultaneous backend connections.

**Command** (backend -> agent), matching `project_prompt.txt`'s schema:

```json
{
  "command_id": "uuid",
  "aircraft_id": "string",
  "timestamp": "ISO8601",
  "command_type": "UPLOAD_MISSION | START_MISSION | RETURN_TO_LAUNCH",
  "parameters": { },
  "mission_version": 1
}
```

**Ack** (agent -> backend), the full chain from the spec:

```json
{
  "command_id": "uuid",
  "status": "RECEIVED | VALIDATED | ACCEPTED | REJECTED | PX4_ACTION_STARTED",
  "reason": "string, populated only on REJECTED"
}
```

`RECEIVED` and `VALIDATED` (or `REJECTED`, terminating the chain) apply to
every command type. `PX4_ACTION_STARTED` only follows `ACCEPTED` for commands
that actually trigger a PX4 action (`START_MISSION`, `RETURN_TO_LAUNCH`) --
`UPLOAD_MISSION`'s chain ends at `ACCEPTED`, since nothing is "started" by
uploading a mission.

**Telemetry** (agent -> backend, pushed periodically at ~2-4Hz, not
request/response): position, altitude, attitude, battery, flight mode, armed
state, mission progress -- the same fields `VehicleState` already tracks
today, just sourced from the agent instead of directly from MAVSDK.

This telemetry push is a deliberate, minimal inclusion: without it the
frontend map/dashboard goes dark the moment the backend stops holding its own
MAVSDK connection. It is **not** a preview of the future Telemetry Agent's
real design (rate-classed streams, buffering/dropping under bad links) --
just enough to keep the system usable end-to-end with the Mission Agent in
place.

**Validation stays double-layered**, matching the existing design and what
the Phase 6 geofence test already proved valuable: the backend still validates
on upload like it does today, and the Mission Agent *independently*
re-validates before ever touching PX4 -- identity, timestamp, schema,
current-state legality, geofence, mission bounds. Neither trusts the other.
Replay protection (stale/duplicate `command_id` rejection) is explicitly
**out of scope for V1** -- `NETWORKING.md` already flags this as deliberately
deferred, and this design doesn't change that.

## Internal Components

- **`MavlinkConnection`** -- wraps a MAVSDK `System`, connects locally to PX4
  (`udp://:14540`). Exposes: arm, upload_mission, start_mission,
  return_to_launch, plus telemetry subscriptions.
- **`StateTracker`** -- holds the latest known vehicle state (armed, flight
  mode, position, health, mission progress), kept current by MAVSDK telemetry
  callbacks. Mutex-protected (see Concurrency below).
- **`CommandValidator`** -- pure logic: given a parsed command + current
  `StateTracker` snapshot + geofence config, returns accept or
  reject-with-reason. No PX4 dependency -- this is the primary unit-test
  surface.
- **`AgentServer`** -- TCP listener, one persistent backend connection.
  Parses newline-delimited JSON, runs commands through `CommandValidator`
  then `MavlinkConnection`, sends back the ack chain.
- **`TelemetryPublisher`** -- timer-driven, serializes `StateTracker` and
  pushes it over the same connection.

**Deliberate simplification: no separate SAFETY.md-style state machine.**
`StateTracker` just mirrors what PX4 already reports; `CommandValidator`
checks per-command preconditions against that (e.g. reject `UPLOAD_MISSION`
if armed). Reimplementing PX4's own state machine in parallel would risk
drifting out of sync with what PX4 actually does -- PX4 already *is* that
state machine. This also resolves the "how much failsafe policy belongs
here" question: V1 tracks state and rejects illegal commands; it does not
implement new failsafe behavior beyond what PX4 already provides (e.g. the
`LTE_LOST` "grace period then LOITER" policy `SIMULATION.md` flagged as
deferred to "the future onboard mission agent" stays deferred past this
design too).

## Concurrency

MAVSDK's C++ SDK already runs its own internal thread(s) for telemetry
callbacks -- there's no way to avoid this being a small multi-threaded
program. `StateTracker` gets a mutex; the TCP server can run on the main
thread. This is standard, well-understood practice for a bridging service
like this, not a place to contort the design to fake single-threadedness.

## Error Handling

- **PX4 connection lost/not yet established:** `AgentServer` keeps accepting
  backend connections and responds to commands with a clear rejection rather
  than hanging or crashing -- matches `HARDWARE.md`'s "companion computer
  failure must be silent/safe" requirement.
- **Backend TCP connection drops:** Mission Agent keeps running, PX4
  continues whatever it's doing unaffected, and the agent accepts a new
  connection when the backend reconnects. This is exactly the `LTE_LOST`
  scenario Phase 5/6 already proved safe -- no new logic needed here, just
  "don't crash when the socket closes."
- **Malformed JSON / schema violation:** reject with a clear error, don't
  drop the connection.
- **PX4 itself rejects a command** (e.g. `arm()` denied): surface as
  `REJECTED` with PX4's real rejection reason, the same pattern the Python
  backend already uses today rather than guessing preemptively.

## Testing Approach

- **Unit tests** for `CommandValidator` -- pure logic, no PX4 needed, runs in
  CI. Framework: **Catch2** (lightweight, low setup overhead) over GoogleTest,
  though GoogleTest remains a reasonable alternative if resume-recognizability
  matters more than setup simplicity.
- **Integration tests** reuse the existing `aircraft-net` SITL setup from
  Phase 4-6 -- run the real Mission Agent against real PX4, drive it with a
  small test client, verify the full command -> ack -> PX4 flow end-to-end.
  Documented the same SETUP/ACTION/EXPECTED RESULT/PASS CRITERIA way as
  everything in `tests/simulation/`.
- **Build system:** CMake (standard for MAVSDK C++ projects, matches
  MAVSDK's own examples) + **nlohmann/json** for the wire protocol.

## Consequences

- **Backend rework required:** `app/vehicle.py`'s `VehicleConnection` swaps
  its MAVSDK `System` for a TCP/JSON client of the Mission Agent.
  `app/main.py`'s endpoints and the frontend should not need to change.
- **Simulation launch scripts affected:** `simulation/network/launch-aircraft.sh`
  and `launch-ground.sh` (and the stress-test variant) will need updating once
  the Mission Agent exists -- PX4 no longer needs its GCS-style MAVLink
  instance reachable across the tunnel, and the backend's
  `VEHICLE_SYSTEM_ADDRESS` env var changes meaning (points at the Mission
  Agent's TCP port, not a MAVLink `udpout://` address). Not part of this
  design doc's scope to update yet -- covered in the implementation plan.
- **`SIMULATION.md`/`NETWORKING.md` will need a findings update** once this
  is actually built and tested, same as every other phase in this project --
  real results, not just the design, go in those documents.
