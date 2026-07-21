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
| 4. Turn structure automation | Partial, by design | [phase4-turn-structure.md](doc/commander-status/phase4-turn-structure.md) |
| 5. Priority passing (simplified stand-in for a full stack) | Partial, by design | [phase5-priority-stack.md](doc/commander-status/phase5-priority-stack.md) |
| 6. Mana system (pool auto-empty only) | Narrow slice done | [phase6-mana.md](doc/commander-status/phase6-mana.md) |
| 7. Card ability system (display parsing + a real execution engine) | Done at scope (Increments 1–2, Stages 1–6) | [phase7-card-abilities.md](doc/commander-status/phase7-card-abilities.md) |
| 8. Combat system | Declare-attacker + Stages A/B/C/D (targeting, blocking, automated damage/death, deathtouch/trample/indestructible, first strike/double strike) done | [phase8-combat.md](doc/commander-status/phase8-combat.md) |
| 9. State-based actions (advisory warnings) | Partial, by design | [phase9-state-based-actions.md](doc/commander-status/phase9-state-based-actions.md) |
| §12 UI design & enhancements | Ongoing | [ui-enhancements.md](doc/commander-status/ui-enhancements.md) |

## Current status

Everything in the table above is implemented, GTest-covered, and live-verified per its
own doc's "Testing" section; the full suite passes with zero known regressions and
`format.sh --cmake --branch master` is clean. All work is on the `commander-rules`
branch, pushed to `origin` (this host's only remote — see `CLAUDE.md`'s git-remotes
note, since that setup has changed across hosts this fork has been developed on).

**As of 2026-07-20, by explicit user direction, this fork no longer holds to
"advisory/manual-only, never hard automation"** (see CLAUDE.md's Design Principles
section) — Phase 8 Stages A/B/C/D are the first Commander mechanic built as genuine
automated enforcement (real attack targeting, blocker declaration, automated combat
damage, automated creature death, deathtouch/trample/indestructible, first
strike/double strike) rather than an advisory counter/warning.

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
- Any further Phase 5/6 depth beyond what's documented (real general stack resolution,
  full mana cost validation outside Phase 7 Stage 4's narrow activated-ability case).

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
- Command zone has no dedicated context menu (graveyard/exile do).
