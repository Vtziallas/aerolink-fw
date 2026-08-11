# Simulation Test: Low-Battery / Critical-Battery Failsafe

Validates `SAFETY.md`'s LOW_BATTERY -> RETURN_TO_HOME / CRITICAL_BATTERY ->
EMERGENCY (immediate land) policy, using PX4's own battery failsafe state
machine rather than anything in our own code -- this is what makes the
policy real rather than aspirational.

## Background

PX4's real battery thresholds: `BAT_LOW_THR` (15%, warning only),
`BAT_CRIT_THR` (7%, triggers the configured critical action), `BAT_EMERGEN_THR`
(5%, triggers immediate landing). The actual *action* taken is
`COM_LOW_BAT_ACT`, which defaults to `0` (warning only, no failsafe mode
change) -- so out of the box, PX4 will never autonomously respond to a low
battery. Also, SITL's simulated battery is floored at `SIM_BAT_MIN_PCT`
(default 50%, see `SIMULATION.md`'s Phase 3 findings), well above every one
of these thresholds, so it can never even report low/critical/emergency
under default settings.

None of this involves the ground station at all -- battery failsafe is
entirely a PX4/vehicle-side concern, which is exactly the point: it must
keep working with the ground link completely absent, same as everything
else in `SAFETY.md`.

## SETUP

- `sudo bash simulation/network/set-battery-test-params.sh` -- removes the
  SITL battery floor (`SIM_BAT_MIN_PCT=0`), speeds up simulated drain
  (`SIM_BAT_DRAIN=0.3`, i.e. 1% per 0.3s instead of 1% per 60s, so the test
  finishes in under a minute instead of over an hour), and enables the real
  failsafe action (`COM_LOW_BAT_ACT=3`: return at critical, land at
  emergency).

## ACTION

1. Upload and start a normal 3-waypoint mission (battery only drains while
   armed, so arming is what starts the clock).
2. Let it fly with no further intervention, observing PX4's own log.
3. `sudo bash simulation/network/revert-battery-test-params.sh` afterward to
   restore PX4's real defaults.

## EXPECTED RESULT

- At 15% (`BAT_LOW_THR`): a warning is logged, no mode change.
- At 7% (`BAT_CRIT_THR`): the configured critical action fires (per
  `COM_LOW_BAT_ACT=3`, this means "return").
- At 5% (`BAT_EMERGEN_THR`): the emergency action fires (immediate landing),
  overriding whatever was happening before.
- All of this happens autonomously -- no command sent from the ground
  station or our backend at any point.

## PASS CRITERIA

- [x] Low-battery warning logged at the low threshold.
- [x] A failsafe action (not just a warning) fires at the critical threshold.
- [x] An emergency action (landing) fires at the emergency threshold.
- [x] The aircraft ends the test landed and disarmed, not crashed.

## Result

**PASS.** Executed 2026-08-11. PX4's log, unedited:

```
WARN  [health_and_arming_checks] Low battery
INFO  [tone_alarm] battery warning (slow)
WARN  [failsafe] Failsafe activated: entering Hold for 5 seconds
INFO  [tone_alarm] battery warning (fast)
ERROR [health_and_arming_checks] Emergency battery level
WARN  [failsafe] Failsafe activated
INFO  [commander] Landing detected
INFO  [commander] Disarmed by landing
```

All three thresholds fired correctly and in the right order, entirely
autonomously -- no command was sent from our backend at any point during
this sequence. `COM_FAIL_ACT_T`'s "Hold for 5 seconds" behavior (a brief
pause before executing the failsafe action, giving a human operator a window
to intervene if present) is worth knowing about but didn't need any
handling from us.

Test params (`SIM_BAT_MIN_PCT`, `SIM_BAT_DRAIN`, `COM_LOW_BAT_ACT`) reverted
to PX4 defaults afterward via `revert-battery-test-params.sh`.
