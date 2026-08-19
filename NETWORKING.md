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

**The Mission Agent's TCP port has no protocol-level authentication of its own.** `AgentServer` binds to all interfaces (`INADDR_ANY`) on port 5760 and accepts commands from whoever connects first — there is no key, token, or signature check in the JSON protocol, so anyone who can reach that port has full command authority over the aircraft (upload mission, arm and start it, RTL). What makes that safe today is entirely the boundary described below: the WireGuard tunnel is the only path into `aircraft-net`, and that namespace's firewall drops everything except WireGuard's UDP port, so 5760 is not reachable from outside the tunnel. This is the same "no raw unauthenticated protocol exposed to the Internet" property MAVLink had before — the protocol changed, the mitigation did not — but note that this mitigation is now load-bearing for a *new* surface, and one that speaks commands rather than just relaying them. Layers 3-6 below (command signing, replay protection, per-state authorization) are what would remove this dependence; they remain design-only, so the netns/WireGuard boundary must not be weakened or bypassed for convenience in the meantime.

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

## Phase 4 Implementation (simulated ground/aircraft network split)

Scripts: [`simulation/network/`](simulation/network/).

**Scope decision:** implemented as a **direct 2-peer WireGuard tunnel** between two Linux network namespaces (`ground-net`, `aircraft-net`) rather than the full 3-node rendezvous topology shown above. The rendezvous hop exists in the production design to solve NAT traversal for a UAV with no public IP -- that's not a problem two local namespaces have, so building it now would be complexity without a matching lesson. The rendezvous server remains the plan for when this connects to a real UAV over real LTE (Phase 8+); nothing about the 2-peer tunnel needs to change to add it later, since the tunnel software itself doesn't care whether its peer is directly reachable or relayed.

**Topology actually built:**

```
[Windows: frontend]
       |  (WSL localhost forwarding)
[default WSL namespace] --veth(10.201.0.0/24)-- [ground-net: backend]
     (socat TCP relay,                                 |
      forwards :8000)                          WireGuard tunnel (10.99.0.0/24)
                                                over veth "internet" link (10.200.0.0/24)
                                                         |
                                                [aircraft-net: PX4 SITL]
```

**Key finding: which PX4 MAVLink instance to target matters.** PX4's "onboard/offboard" instance (the one used in Phase 1-3 same-machine testing, port 14580/14540) is started with a fixed `-o <remote-port>` and always sends to `localhost` -- it cannot be redirected to a different network namespace/host by changing the client's connect address, because PX4 itself, not the client, decides where to send. PX4's "GCS" instance (port 18570) is started *without* `-o`/`-m onboard`, which makes it a pure listener that learns the peer's address dynamically from whichever source first sends it a packet -- the same pattern QGroundControl uses. That's the instance that works across a real network/tunnel. Through Phases 4-6, the backend used it directly, connecting via `udpout://10.99.0.2:18570` (dial out to PX4's tunnel IP) instead of `udpin://:14540` (wait for PX4 to dial a fixed local port).

**Superseded as of the Mission Agent (2026-08-18): nothing speaks MAVLink across the tunnel any more.** The Mission Agent runs inside `aircraft-net` alongside PX4 and owns the only MAVLink link, using the *local onboard* instance (`udp://:14540`) precisely because it no longer has to cross a network to reach PX4. The tunnel now carries the agent's own newline-delimited JSON/TCP protocol between the backend (`ground-net`) and the agent's port 5760, and PX4's GCS-style instance no longer needs to be reachable across it at all. The finding above is retained because it's still the reason the *agent* targets 14540 rather than 18570. Full topology and rationale: [`docs/architecture/mission-agent-design.md`](docs/architecture/mission-agent-design.md) (Network Topology, and "Why the local/onboard MAVLink instance").

**Firewall enforced, not just documented:** `aircraft-net`'s only external-facing interface drops everything except WireGuard's UDP port (`iptables -P INPUT DROP` plus an explicit accept for `udp --dport 51820`). Raw MAVLink is not reachable from outside the tunnel -- verified, not assumed.

**The `socat` relay is a local-topology artifact, not a production concern.** Because this whole simulation runs on one Windows machine, the frontend (native Windows, for WSL2 localhost-forwarding reasons -- see `SIMULATION.md` Phase 2 findings) needs a path into `ground-net`, which isn't the default WSL namespace. A real deployment has the frontend and backend on the same side of the tunnel already; this relay only exists to bridge WSL's namespace-vs-host boundary for local testing.

**Verified end-to-end:** mission upload, mission execution (climb, waypoints, landing, disarm), and live WebSocket telemetry all confirmed working through the real tunnel -- not just a successful handshake. Full test record: [`tests/simulation/network_split_isolation.md`](tests/simulation/network_split_isolation.md).

## Status

Application-layer auth (command signing, replay protection, per-state command authorization -- Layers 3-6 above) is still design-only; not needed yet since there's no untrusted party on this tunnel to defend against in simulation. The rendezvous-server hop (vs. today's direct 2-peer tunnel) is deferred until there's a real UAV with no public IP to solve NAT traversal for.
