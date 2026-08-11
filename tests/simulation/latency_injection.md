# Simulation Test: Latency Injection on the Ground/Aircraft Link

Confirms the system remains functional -- just slower -- under degraded
latency on the WireGuard-tunneled ground/aircraft link, using
`simulation/network/inject-fault.sh`. Matches `SIMULATION.md`'s Phase 5 list
(50/100/250/500ms).

## SETUP

- Network split + WireGuard tunnel up (`tests/simulation/network_split_isolation.md`).
- Mission already flying is not required; this test uploads its own.

## ACTION

1. `sudo bash simulation/network/inject-fault.sh apply 250 0` -- 250ms
   latency, full duplex, on both ends of the ground/aircraft veth link (not
   the WireGuard interface itself, so this degrades the underlying transport
   the same way bad cellular RF would).
2. Upload and start a 3-waypoint mission through the normal API, timing the
   upload call.
3. Observe the mission executing (armed, climbing, flying) via PX4's log and
   `/api/status`.
4. Send an RTL command through the normal emergency API, timing it.
5. `sudo bash simulation/network/inject-fault.sh clear` to remove the
   impairment.

## EXPECTED RESULT

- Mission upload and RTL both succeed, taking noticeably longer than the
  unimpaired case (each MAVLink item/ack round-trip now carries the injected
  delay) but not failing or timing out.
- The aircraft's own autonomous behavior (climb, mission execution, PX4's
  internal safety logic) is unaffected by the link latency, since none of it
  depends on the ground link once a mission is uploaded.

## PASS CRITERIA

- [x] Mission upload completes successfully (not just eventually errors out).
- [x] Mission executes autonomously exactly as it would with no injected
      latency.
- [x] RTL command reaches PX4 and is acted on.
- [ ] ~~Live WebSocket telemetry remains connected throughout~~ -- did NOT
      pass as originally worded; see Result.

## Result

**PASS with one real finding.** Executed 2026-08-11 with 250ms latency applied.

- Mission upload: **5.1s** (vs. sub-second with no injected latency) --
  functional, just slower, consistent with multiple sequential MAVLink
  item-ack round trips each now carrying ~250-500ms.
- Mission armed and executed autonomously: confirmed via PX4 log
  (`Executing Mission`, `Climb to 30.0 meters above home`, `Takeoff
  detected`) -- unaffected by the link latency, as expected.
- **Unrelated but notable:** during this run, PX4 autonomously aborted its
  landing approach and entered an indefinite hold (`Holding at 30 m above
  landing waypoint` -- PX4's own `mission_base.cpp` landing-abort logic,
  which only evaluates flight-dynamics state, not network/GCS status). This
  was not caused by the injected latency -- data-link-loss failsafes are
  disabled (`NAV_DLL_ACT=0`) and this abort path doesn't reference the link
  at all -- but it did surface a real gap: **we have no "resume mission" or
  explicit "land now" command**, only RTL. Used RTL to recover, which worked
  (**0.5s** round trip under the same 250ms latency) and the aircraft landed
  normally afterward. Adding a resume/land-now control is a reasonable
  follow-up for the mission panel.
- **WebSocket telemetry disconnected** partway through
  (`ConnectionClosedError: ... keepalive ping timeout`) under the combined
  latency + `socat` relay + WireGuard tunnel path. Not yet root-caused to a
  specific layer (client library's default ping timeout vs. relay buffering
  vs. tunnel) -- flagged as a real follow-up rather than dismissed, since a
  ground station that silently loses its telemetry view under bad-but-not-
  catastrophic latency is a genuine UX/observability gap, even though (per
  the network-outage test) it does **not** affect flight safety.
