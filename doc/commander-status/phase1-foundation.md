# Phase 1: Foundation, Build Setup & the RulesEngine Architecture

**Status: done.**

Design doc §3 Phase 1 asks for a foundation library + build setup. This fork's actual
foundation went through two shapes across its history:

1. **Originally**: new logic lived directly inside existing
   `libcockatrice_card`/`libcockatrice_models`/`libcockatrice_network` behind
   `isCommanderGame()` gates — no separate rules library.
2. **Reorganized (2026-07-16)**: stood up `libcockatrice_rules/` + `Rules::RulesEngine`
   as a pure-decision, no-I/O layer, and made the fork **wholly Commander-only** end to
   end — every `isCommanderGame()` gate removed, since the fork no longer supports
   non-Commander play at all.

## `Rules::RulesEngine` design

`libcockatrice_rules/libcockatrice/rules/rules_engine.{h,cpp}` is a **pure decision
layer**: static, stateless helpers (`phaseAutomationFor`, `nextPriorityPlayer`,
`isCombatPhase`, `manaCounterNames`, `planManaPayment`, `isCommanderCard`,
`commanderTaxCounterNameForMove`) plus small instance state for the priority round and
the pending-ability stack. **No I/O** — `Server_Game`/`Server_Player` hold a
`RulesEngine`, consult it, and perform the resulting side effects (broadcasting events,
creating counters, moving cards). This split is what makes the engine unit-testable
without a live participant — see `tests/rules/rules_engine_test.cpp`.

**Architecture decision (2026-07-16), still current**: do **not** build the design
doc's full `GameState`/`RulesEngine` "real engine" — that only pays for itself once
there's genuine card-effect execution (stack resolution, mana payment, combat), which
Phase 7 later built incrementally, inside this same class, on its own explicit
sign-offs. Pulling the scattered `isCommanderGame()` decision logic into one seam was
the right-sized move at the time, not a wholesale re-architecture.

Migrated verbatim from `Server_Game` at the time: `phaseAutomationFor()`,
`nextPriorityPlayer()`, the priority-round state machine. Migrated later
(2026-07-20 follow-up): `isCommanderCard()`/`commanderTaxCounterNameForMove()`, out of
inline conditionals in `Server_Player::setupZones()`/`onCardBeingMoved()` — closing the
last flagged organizational deferral from the original reorg. Counter *creation*/I/O
loops (per-commander tax counter, cross-player damage counters) intentionally stayed in
`Server_Player`/`Server_Game` — `RulesEngine` does no I/O by design, so there's no
decision left to extract from a loop that's pure allocation.

## Commander-only client & module migration

- Every client- and server-side `isCommanderGame()` gate was removed. The create-game
  dialog's game-type picker and the deck editor's format combo box were removed
  entirely; defaults (40 life, 4 players, hardcoded `"commander"` format string) are no
  longer conditional on anything.
- `CommanderRules` + `CommanderCounterNames` moved into `libcockatrice_rules`
  (unconditionally built). **`CommanderDeckValidator` deliberately stayed** in
  `libcockatrice_models_deck_list` — it needs `DeckListModel`, and that library isn't
  built in a server-only (`WITH_CLIENT=OFF`, `WITH_ORACLE=OFF`) configuration, so moving
  the validator into the always-built `libcockatrice_rules` would break that CI
  configuration. Treat this as a scoped decision if revisiting library boundaries
  later, not an oversight.

## Gotcha for future source-file moves across CMake targets

A `ninja <target>`/`make <target>` build can report **zero errors** after moving a
source file between CMake targets while silently linking a *stale* archive that still
contains the old `.o` (`ar` archives aren't stripped of members that fall off a
`CMakeLists.txt` source list just because the list changed). Always force a fresh
`cmake` reconfigure and confirm the new target's build directory actually contains the
moved file's `.o` after any cross-target file move.

## Testing

`tests/rules/rules_engine_test.cpp` covers every decision helper and the
priority/pending-ability state machine directly, with no live server object needed —
see the root tracker doc for the current pass count.
