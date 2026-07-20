# Phase 8: Combat System

**Status: narrow slice done — see Phase 7's Stage 6.**

Design doc §3 Phase 8, the design doc's own largest single-phase estimate. This fork's
combat work is documented as Phase 7 Stage 6 (the roadmap's two efforts converged
there, since finishing Phase 7's card-ability engine is what let a keyword like
Vigilance matter mechanically) — see `phase7-card-abilities.md`'s "Stage 6" section for
the full research, scope, and implementation writeup.

## Summary

A real "declare as attacker" toggle (rule 508.1a/508.1f: tap unless vigilance), a
visual indicator, and auto-clearing when combat ends — built almost entirely from
pre-existing-but-unused infrastructure (`Server_Card::attacking` was already fully
wired through the protocol but had no producer or consumer), zero new protocol
messages.

## Explicitly out of scope

- Declaring or removing blockers.
- Any combat-damage calculation — no reliable attacker→blocker link exists (arrows are
  untyped), and no numeric power/toughness representation exists anywhere in this
  codebase (P/T is a free-form display string).
- Any creature-death/graveyard automation — this fork has never automated death, even
  for the simpler life ≤ 0 case (Phase 9 is advisory-only by design).

Any further combat depth is a new, separate, explicit design decision — not a gap left
over from this phase, and not something to fill in without discussing size/risk first
(per `CLAUDE.md`'s standing rule for Phases 6–8).
