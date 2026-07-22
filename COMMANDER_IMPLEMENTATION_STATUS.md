# Commander Rules Fork — Implementation Status & Tracker

Working index for the `cockatrice-commander` fork (adds MTG Commander/EDH rules
enforcement to Cockatrice). This file is the **entry point**: scope, the phase-tracking
table, the protocol-extension registry, and known limitations/next candidates. Each
phase's actual implementation detail — what's built, what's deliberately excluded, and
why — lives in its own file under [`doc/commander-status/`](doc/commander-status/),
linked from the table below. See the "Commander-Rules Fork of Cockatrice" section of
[README.md](README.md) for the user-facing feature description.

**Build/test process** (the Docker toolchain this host actually builds with, GTest,
`format.sh`, the Xvfb/screenshot UI-testing recipe) lives in `CLAUDE.md`, not here —
that's the single source of truth for *how* to verify a change.
[`doc/commander-status/testing-and-ops.md`](doc/commander-status/testing-and-ops.md)
holds only what doesn't fit there (real-multiplayer EC2 hosting, testing-efficiency
tips).

## Scope

Fork of [Cockatrice/Cockatrice](https://github.com/Cockatrice/Cockatrice), adding
enforcement of the official [Commander/EDH rules](https://mtgcommander.net/index.php/rules/),
loosely following the phased design in
[`doc/design-docs/mtg-commander-rules-engine.md`](doc/design-docs/mtg-commander-rules-engine.md)
(mirrored in-repo so it survives if the source disappears/changes). That doc proposes a
much larger 9–13 month, 9-phase roadmap (turn structure, stack/priority, mana,
abilities, combat, full state-based actions, a 4-player UI overhaul). **This fork
intentionally implements only a scoped-down subset**, corresponding closely to the
design doc's own §8 "Recommended Starting Point," extended incrementally with each
extension gated on its own explicit sign-off — see `CLAUDE.md`'s design principles for
the philosophy (manual "physical simulator," advisory not blocking, reuse existing
mechanisms over new protocol).

**Before treating any unimplemented phase/feature as an oversight**, check its
per-phase doc below — most gaps are deliberate, documented scope decisions, not things
to silently fill in.

## Phase tracking

| Phase | Status | Doc |
|---|---|---|
| 1. Foundation, build setup & `RulesEngine` architecture | Done | [phase1-foundation.md](doc/commander-status/phase1-foundation.md) |
| 2. Commander deck validation | Done | [phase2-deck-validation.md](doc/commander-status/phase2-deck-validation.md) |
| 3. Command zone & commander tracking | Done | [phase3-command-zone.md](doc/commander-status/phase3-command-zone.md) |
| 4. Turn structure automation | Automation done; phase-order, next-turn, and casting/attack-timing enforced | [phase4-turn-structure.md](doc/commander-status/phase4-turn-structure.md) |
| 5. Priority passing (simplified stand-in for a full stack) | Partial, by design | [phase5-priority-stack.md](doc/commander-status/phase5-priority-stack.md) |
| 6. Mana system (pool auto-empty + real cost payment for casting from hand) | Narrow slice done | [phase6-mana.md](doc/commander-status/phase6-mana.md) |
| 7. Card ability system (display parsing + a real execution engine) | Done at scope (Increments 1–2, Stages 1–6) | [phase7-card-abilities.md](doc/commander-status/phase7-card-abilities.md) |
| 8. Combat system | Declare-attacker + Stages A/B/C/D (targeting, blocking, automated damage/death, deathtouch/trample/indestructible, first strike/double strike) done | [phase8-combat.md](doc/commander-status/phase8-combat.md) |
| 8 (extension). Static/continuous abilities | First slice done — P/T-boost + keyword-grant anthem/lord effects, combat-math consumers only | [phase8-static-abilities.md](doc/commander-status/phase8-static-abilities.md) |
| 9. State-based actions (advisory warnings) | Partial, by design | [phase9-state-based-actions.md](doc/commander-status/phase9-state-based-actions.md) |
| §12 UI design & enhancements | Ongoing | [ui-enhancements.md](doc/commander-status/ui-enhancements.md) |

## Current status

Everything in the table above is implemented and GTest-covered per its own doc's "Testing"
section; the full suite passes with zero known regressions and `format.sh --cmake --branch
master` is clean. Most rows are also live-verified per their own doc. **As of 2026-07-21, the
Xvfb/UI-testing infrastructure itself is fixed** (see `CLAUDE.md`'s updated UI-testing note and
`phase6-mana.md`'s "Testing" section) — the "card clicks/drags don't register" conclusion three
prior sessions reached was actually a version-notification dialog silently swallowing every
scripted click, not a fundamental limitation. The Phase 6 real-spell-casting-mana-payment
addendum is now live-verified end to end using the fixed infrastructure
(`.uitest/scenario.py run mana_gate`), as is its follow-on hybrid/Phyrexian/`{X}`
addendum (`.uitest/scenario.py run variable_mana_gate`). The static/continuous abilities extension
(`phase8-static-abilities.md`) is the one row still GTest-only — its own live-verification
attempt predates this fix and was never re-attempted against it, not a remaining infrastructure
gap. All work is on the `commander-rules`
branch, pushed to `origin` (this host's only remote — see `CLAUDE.md`'s git-remotes
note, since that setup has changed across hosts this fork has been developed on).

**As of 2026-07-20, by explicit user direction, this fork no longer holds to
"advisory/manual-only, never hard automation"** (see CLAUDE.md's Design Principles
section) — Phase 8 Stages A/B/C/D are the first Commander mechanic built as genuine
automated enforcement (real attack targeting, blocker declaration, automated combat
damage, automated creature death, deathtouch/trample/indestructible, first
strike/double strike) rather than an advisory counter/warning.

**As of 2026-07-21, by explicit user request, turn structure itself is now enforced**
(`phase4-turn-structure.md`'s new "Turn-structure and timing enforcement" section,
`phase8-combat.md`'s new "Attack-timing enforcement" section): phases must be stepped
through in order (`Command_SetActivePhase` rejects anything but exactly one step
forward), `Command_NextTurn` now requires the active player and the Cleanup step
(previously had no active-player check at all), attackers can only be declared during
Declare Attackers, and sorcery-speed cards (everything but instants, including lands)
can only be played during the controller's own main phase while they hold priority.
GTest-covered (`tests/rules/rules_engine_test.cpp`'s new `canAdvanceToPhase`/
`canEndTurn`/`canDeclareAttacker`/`canCastSorcerySpeed` cases) and live-verified via a
new `.uitest/scenario.py run turn_structure_gate` scenario (`.uitest/turnstructuretest.cod`:
Plains, Lightning Bolt) confirming, via the client's own debug log: a land blocked outside
a main phase, an instant exempt from that same block in the same phase, a legal
one-step "Next Phase" (Tab) advance actually landing on First Main, and the
previously-blocked land succeeding once there. **Found and fixed two real bugs this way,
not just confirmed the happy path**: (1) this fork's own `mana_gate`/`variable_mana_gate`
scenarios built their test hand via a "click the Draw phase button 3 times" trick whose
first click relied on jumping straight from Untap to Draw — exactly what the new
`canAdvanceToPhase()` enforcement now correctly rejects, so both scenarios silently drew
zero cards until fixed to step forward legally via Tab instead (and gained an extra Tab
into First Main before casting anything, since the new timing gate would otherwise block
their sorcery-speed test cards too); (2) `.uitest/scenario.py`'s `ensure_client()` never
established window input focus for a freshly-launched client (no window manager in this
Xvfb setup), so a scenario that starts with a keyboard shortcut — as `turn_structure_gate`'s
phase-stepping does — sent nothing at all until some click landed on the window first; every
prior scenario had dodged this by coincidence (always clicking before ever pressing a key).
Fixed by clicking a known-safe point inside the game board right after launch, scoped to
local-game mode only (the same fix at the wrong screen position, or applied to the Home-tab
"connect" scenario too, was confirmed live to actively break things instead).

**The Phase 7/8 staged execution-engine roadmap and the `libcockatrice_rules`
reorganization are both fully worked through at this fork's current scope.** Any of the
following would be a new, separate, explicit design decision — not a next increment to
pick up unprompted:

- Partner/Background commander support (see `phase2-deck-validation.md`'s known
  limitations).
- Double-faced-card back-face color identity (same doc).
- Any further combat depth beyond Stages A/B/C/D — protection, damage prevention,
  planeswalker/battle damage, player-chosen damage-assignment order (see
  `phase8-combat.md`).
- Converting Phase 9's advisory life ≤ 0 / poison / commander-damage warnings into
  automatic loss — now possible given the lifted restriction, but not yet done.
- Any further Phase 5/6 depth beyond what's documented (real general stack resolution;
  flashback/alternate-cast-source costs; monocolored hybrid/snow/split-cost payment,
  currently left ungated rather than partially enforced — plain hybrid/Phyrexian/`{X}`
  payment is now done, see `phase6-mana.md`'s second addendum).

## Protocol extensions used

This fork is protocol-`.proto`-free by design everywhere it reasonably can be (see
`CLAUDE.md`'s design principles) — the few genuine exceptions are tracked here so a
future change doesn't collide with a retired or in-use extension number.

| Message | Type | Ext # | Introduced | Notes |
|---|---|---|---|---|
| `Command_PassPriority` | `GameCommand` | 1035 | Phase 5 | |
| `Event_PriorityChanged` | `GameEvent` | 2023 | Phase 5 | |
| `Command_ActivateTargetedEffect` | `GameCommand` | 1036 | Phase 7 Stage 2 | **Retired** in Stage 3, replaced by `Command_ActivateAbility`. Never reuse 1036. |
| `Command_ActivateAbility` | `GameCommand` | 1037 | Phase 7 Stage 3 | |
| `Event_AbilityActivated` | `GameEvent` | 2024 | Phase 7 Stage 3 | |
| `Event_AbilityResolved` | `GameEvent` | 2025 | Phase 7 Stage 3 | |

## Known limitations

- Single designated commander only — no Partner/Background pairing.
- Color identity doesn't pull in a double-faced card's back face.
- No automated stack resolution for arbitrary spells. Combat damage and creature
  death *are* now automated (Phase 8 Stages A/B/C/D) for plain numeric-P/T creatures,
  including deathtouch/trample/indestructible/first strike/double strike, but not
  protection — see the Phase 5/7/8/9 docs for exactly what narrower slice of each *is*
  covered.
- No legend rule, 0-toughness, or illegal-aura state-based actions (see
  `phase9-state-based-actions.md`).
- Static/continuous abilities (anthem/lord effects) are recognized for only two exact line
  shapes (P/T boost, keyword grant, both scoped to "creatures you control"), consumed by combat
  math only — no client display of the boost, no creature-type restrictions, no combined
  single-line phrasing — see `phase8-static-abilities.md`.
- Command zone has no dedicated context menu (graveyard/exile do).
- Real mana payment for casting spells from hand only covers non-land cards with a
  cleanly parseable printed cost actually leaving the `HAND` zone via click-to-play or a
  single-card drag. Plain generic/colored symbols, two-color hybrid, Phyrexian, and
  variable (`{X}`) costs are all gated (see `phase6-mana.md`'s two addenda); monocolored
  hybrid, snow, and split-cost symbols are still unparseable and thus ungated, as are
  face-down plays, multi-card drags, and casting an Aura/Equipment via a drag-attach
  arrow.
