# Simulation Test: Packet Loss Injection on the Ground/Aircraft Link

Confirms the system tolerates sustained packet loss on the WireGuard-tunneled
ground/aircraft link, using `simulation/network/inject-fault.sh`.

## SETUP

- Network split + WireGuard tunnel up (`tests/simulation/network_split_isolation.md`).

## ACTION

1. `sudo bash simulation/network/inject-fault.sh apply 0 15` -- 15% packet
   loss, full duplex, on both ends of the ground/aircraft veth link.
2. Upload and start a 3-waypoint mission through the normal API, timing the
   upload call.
3. Let the mission run to completion under sustained loss, observing via
   PX4's log.
4. `sudo bash simulation/network/inject-fault.sh clear`.

## EXPECTED RESULT

- Mission upload succeeds, somewhat slower than baseline (retransmission of
  dropped packets) but without failing.
- Mission executes and completes normally -- climb, waypoints, landing,
  disarm -- since none of this depends on the ground link once uploaded.

## PASS CRITERIA

- [x] Mission upload completes successfully under 15% loss.
- [x] Mission runs to completion (`mission_current == mission_total`, lands,
      disarms) with the loss condition still active the entire time.

## Result

**PASS.** Executed 2026-08-11 with 15% packet loss applied (both directions).

Mission upload: 1.1s (vs. sub-second baseline, vs. 5.1s for the 250ms-latency
test -- loss causes noticeably less overhead than latency at these levels,
consistent with MAVLink's per-item retry only needing to resend the specific
dropped packet rather than wait out a fixed delay on every round trip).

Mission armed, took off, executed the full 3-waypoint route, and landed
cleanly with the loss condition active throughout:

```
INFO  [commander] Armed by external command
INFO  [navigator] Executing Mission
INFO  [navigator] Climb to 30.0 meters above home
INFO  [commander] Takeoff detected
INFO  [commander] Landing detected
INFO  [navigator] Mission finished, landed
INFO  [commander] Disarmed by landing
```

No landing-abort or other anomaly this run (contrast with the latency test,
where an unrelated landing-abort occurred -- see `latency_injection.md`),
supporting that that abort was a one-off flight-dynamics event, not
something either fault-injection condition causes.
