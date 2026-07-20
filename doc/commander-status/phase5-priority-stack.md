# Phase 5: Priority Passing (simplified stand-in for the Stack System)

**Status: partial (simplified) — real priority-passing, no independent stack resolution
beyond what Phase 7's pending-ability stack later added.**

Design doc §3 Phase 5 calls for a real `Server_Stack` (LIFO resolution) and
`Server_PriorityManager`, needing a genuine card-rules engine. **This section covers
priority only** — real stack resolution for arbitrary card effects is still out of
reach (no card-scripting engine exists for spells generally); what Phase 7 built is a
narrower resolvable stack scoped to its own recognized activated/triggered abilities
only, not a general spell stack. See the Phase 7 doc.

**Researched before implementing**: XMage's `GameImpl.playPriority()` was read directly
(GPL/MIT-licensed, so legally reusable, though Phase 5 only borrowed the algorithmic
shape: round-robin priority passing starting at the active player, reset on any action,
resolve-or-advance once everyone passes in succession) — not literally portable, since
XMage's simulate-and-execute model differs fundamentally from Cockatrice's
server-authoritative/fully-manual-client-actions architecture.

## What's implemented

- **Protocol** (first proto change in this fork): `Command_PassPriority`
  (`GameCommand` ext 1035), `Event_PriorityChanged` (`GameEvent` ext 2023,
  `priority_player_id` field, `-1` = no one holds it). See the root tracker doc's
  protocol-extension registry for the full list across all phases.
- `Rules::RulesEngine` tracks the priority holder and passed-set (see the Phase 1
  doc). Resets to the active player at the start of every phase/step, and whenever a
  card moves onto the (purely visual, per-player) `STACK` zone from anywhere else —
  modeling a cast/activation starting a fresh round at the mover (rules
  601.2i/602.2h/117.3d simplified). Reordering cards already on the stack doesn't
  retrigger it.
- **A priority round is scoped to exactly one triggering event** (a phase/step change,
  or a card moving onto the Stack zone) and starts at exactly one player. If a round
  exhausts (everyone passes with nothing new happening), priority simply **stops** —
  no one holds it — rather than auto-advancing the phase. This is a corrected model;
  see "Correction" below for why.
- **Priority is advisory, not enforcement**: holding priority doesn't gate any other
  command (moving cards, tapping, drawing all remain available to anyone at any time).
- **Client UI**: a "Pass Priority" `PhaseButton` (gold double-chevron icon) in the
  phase toolbar sends `Command_PassPriority`; pulses (reusing the existing
  active-highlight animation) while the local player holds priority. A double-click
  toggles an **auto-pass** mode (off by default, small cyan dot badge) that
  auto-sends `Command_PassPriority` whenever the local player holds priority. A 4px
  cyan border on the priority holder's avatar badge is visible to every
  player/spectator, not just the holder. Log lines: "`<player>` has priority." /
  "Everyone has passed. No one has priority."

## Correction: priority model and round semantics

The first version of `advancePriority()` auto-advanced the phase whenever a round
exhausted, which also reset priority back to the active player. Live testing (a
solo/last-player-standing game with auto-pass enabled) caused an infinite loop:
passing re-fired auto-pass immediately, forever, stopped only by servatrice's own flood
protection. Fixed to the "round stops at exactly one player/one trigger" model
described above — phase advancement is always a deliberate player action, matching the
fork's manual "physical simulator" philosophy (see `CLAUDE.md`'s design principles).

## Deliberate non-fix: priority passing ignores `turnOrderReversed`

`nextPriorityPlayer()` always steps in ascending player-id order and does **not**
consult the `turnOrderReversed` flag, whereas `Server_Game::nextTurn()` does — so under
a table's "Reverse Turn" convenience, priority would pass in the opposite direction
from the turn. **Intentionally left unfixed**: turn reversal is a Cockatrice table
convenience, not a real MTG mechanic, so the non-reversed case is sufficient for
Commander play. A scoped decision, not an oversight.

## Testing

`tests/rules/rules_engine_test.cpp` — `nextPriorityPlayer()` pure logic (advance,
wrap-around, skip-passed, skip-conceded, edge cases) plus the full priority-round state
machine (start → pass → next/exhaust/no-op/clear, including the solo
auto-pass-loop regression case). Live-verified end to end (see `CLAUDE.md`'s
UI-testing recipe) including the corrected no-loop behavior and the Stack-zone-move
trigger.
