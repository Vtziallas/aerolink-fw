# ADR 0001: Record Architecture Decisions

## Status

Accepted

## Context

This project will make a number of significant technology and architecture choices (protocol format, networking approach, language per component, etc.). These decisions and their reasoning should be recorded so future changes can be evaluated against why the original choice was made, not just what it was.

## Decision

We will use Architecture Decision Records, one per significant decision, stored in `docs/adr/` as sequentially numbered Markdown files following this template (Context / Decision / Consequences).

## Consequences

Significant choices already made in `ARCHITECTURE.md` (JSON-then-Protobuf, WireGuard over Tailscale/custom broker, C++ for the onboard mission agent) will be backfilled as individual ADRs as the project proceeds, rather than only living in prose form.
