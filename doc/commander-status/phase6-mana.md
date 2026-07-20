# Phase 6: Mana System

**Status: narrow slice done (auto-empty at phase end). Real cost validation/auto-tap
became partially in-scope later via Phase 7 Stage 4, which handles a specific
sub-case — see that phase's doc; this file covers only the pool-emptying slice.**

Design doc §3 Phase 6 (originally estimated 4–5 weeks) specs a full `ManaPool`/
`ManaCost` pair that parses a printed cost and validates/auto-pays it. That's out of
reach standalone — cost validation needs a parsed mana cost per card, which needs
Phase 7's card-ability engine as a prerequisite (later built, narrowly, in Stage 4).

## What's already there for free

Cockatrice's existing `Server_Player::setupZones()` already creates per-player
`w`/`u`/`b`/`r`/`g`/`x` counters (ids 1–6) for every game — vanilla Cockatrice's
existing answer to "mana pool" under its manual-simulator model, just unlabeled and
with no phase-boundary behavior. No new counters, UI, or protocol were needed to have
*a* mana pool.

## What's implemented

Rule 500.4 — a mana pool empties at the end of every step and phase.
`Rules::RulesEngine::manaCounterNames()` returns the fixed 6-name list;
`Server_Player::emptyManaPool()` zeroes any of the calling player's counters whose name
is in that list, enqueuing `Event_SetCounter` only for ones that actually changed
(dedup via the existing `setCount()`/`didChange` convention).
`Server_Game::setActivePhase()` calls it for **every** player (not just the active one
— rule 500.4 empties everyone's pool) on every phase/step transition.

## Deliberately still excluded

Cost parsing, `canPay()` validation, auto-tap suggestions, and mana-ability activation
from context menus were out of scope for this slice specifically — those needs are
each addressed separately: mana-ability *production* recognition/one-click tap is Phase
7 Increments 1–2, and cost *payment* is Phase 7 Stage 4. This file only ever covers
zeroing counters at phase boundaries, never reading or validating a cost.

## Testing

`tests/rules/rules_engine_test.cpp`'s `ManaCounterNamesCoversTheFiveColorsPlusColorless`.
Live-verified: a real solo Commander game, manually setting a color counter to a
nonzero value and confirming it zeroes automatically on the next phase transition, with
no spurious events for already-zero counters and no effect on unrelated counters
(life, tax, poison).
