# Flight Test Plan

**SIMULATION / AUTHORIZED OPERATIONS ONLY** until every item below is satisfied. This document governs Phase 9 and is not activated by anything earlier than a passing Phase 7/8 (HIL, bench, physical integration).

## Preconditions Before Any Physical Flight

- [ ] All Phase 0–6 simulation acceptance criteria met (`SIMULATION.md`)
- [ ] Failsafe state machine validated under fault injection (`SAFETY.md`, `TESTING.md`)
- [ ] Hardware-in-the-loop and bench testing complete (`HARDWARE.md`)
- [ ] Independent RC safety-override link tested and confirmed to bypass the companion computer entirely
- [ ] Applicable EASA / national aviation-authority requirements identified for the specific test location
- [ ] Test site confirmed within permitted airspace, clear of airports/restricted zones/uninvolved people
- [ ] VLOS maintained for the entire flight; no BVLOS without separate, explicit authorization
- [ ] Altitude and geofence limits configured and validated in software (`SYS-MIS-001`)
- [ ] Test mission is deliberately simple (not the full HOME→A→B→C→HOME profile on the first flight)

## Regulatory Boundaries

Every constraint from `SAFETY.md`'s "Regulatory Boundaries" section applies without exception. No feature that constitutes BVLOS or moves the operation outside the Open category is flown without corresponding authorization.

## Status

Not applicable yet — no physical aircraft exists. This document will be filled in with the actual test site, safety pilot roles, abort procedures, and mission sequencing once Phase 8 is reached.
