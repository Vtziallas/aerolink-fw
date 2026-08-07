# Networking

## Goals

The system communicates over the public cellular Internet, so security is a first-class requirement — not an afterthought layered on later. Core principle: a remote network actor must not be able to obtain aircraft control simply by discovering an IP address or port. Raw, unauthenticated MAVLink is never exposed to the Internet.

## Constraints

- The UAV has no public IP — it sits behind carrier NAT on a dynamic address.
- Connectivity is unreliable: variable latency, variable bandwidth, packet loss, and full outages must all be designed for, not treated as edge cases (see `SIMULATION.md`).

## Topology

```
UAV (companion computer)
  ↓ outbound WireGuard tunnel
4G/LTE → Internet
  ↓
WireGuard Rendezvous Server (static IP)
  ↑ outbound WireGuard tunnel
Ground Station Backend
```

Both the companion computer and the GCS backend are WireGuard *clients* dialing out to a small always-on rendezvous server. This sidesteps NAT/dynamic-IP entirely — the aircraft never needs to accept an inbound connection.

## Threat Model

| Threat | Mitigation |
|---|---|
| Unauthorized command injection | Transport auth (WireGuard peer keys) + application-level command signing; mission agent enforces an allow-list of valid commands per current flight state |
| Replayed commands | `command_id` nonce cache + timestamp window, rejected by the mission agent |
| Stolen credentials | Device-specific keys, out-of-band provisioning, scoped/rotatable credentials |
| Compromised server | Rendezvous server only relays encrypted WireGuard traffic; it does not hold command-signing keys |
| Compromised companion computer | PX4's own failsafes are independent of the companion computer (see `SAFETY.md`); MAVLink message signing evaluated as an additional layer |
| Packet sniffing | WireGuard transport encryption |
| Spoofed telemetry | Same device-identity/signing chain applies to telemetry, not just commands |
| Duplicate messages | Idempotent command handling keyed on `command_id` |
| Corrupted messages | Transport integrity (WireGuard) + schema validation on receipt |
| Denial of connectivity | Explicitly designed for — see `SAFETY.md`'s LTE-loss failsafe policy; loss of network is a safety non-event by design |

## Layers

1. **Transport:** WireGuard tunnel. No inbound ports open on the aircraft side, ever.
2. **Device identity:** Aircraft and each authorized GCS instance have their own WireGuard keypair, provisioned out-of-band.
3. **Application-layer auth:** Every `GroundCommand` carries `command_id`, `aircraft_id`, `timestamp`, signed with an operator credential distinct from the transport key.
4. **Replay protection:** Mission agent rejects stale timestamps or previously-seen `command_id`s.
5. **Authorization:** Mission agent enforces which command types are valid for the aircraft's current state.
6. **Key rotation:** WireGuard keys rotated on a defined schedule via a provisioning script; revocation = removing a peer from the rendezvous server config.
7. **Firewall:** Rendezvous server exposes only the WireGuard UDP port. Companion computer has no other service reachable from the tunnel except what the mission agent explicitly binds.
8. **MAVLink signing:** Evaluated as an additional defense-in-depth layer at the MAVSDK boundary.

## Explicit Non-Goals

- No raw MAVLink exposed to the public Internet under any configuration.
- No reliance on a third-party mesh-VPN control plane (see `ARCHITECTURE.md` §5 for the WireGuard-vs-Tailscale-vs-custom-broker comparison and reasoning).

## Status

Design only — implemented starting Phase 4 (simulated LTE/network architecture). This document will be updated with the actual rendezvous server config approach, key provisioning scripts, and any deviations discovered during implementation.
