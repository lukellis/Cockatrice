# Commander Rules Fork — Implementation & Build Status

Working notes for the `cockatrice-commander` fork (adds MTG Commander/EDH rules
enforcement to Cockatrice). This file tracks implementation + build/test progress
across sessions so work can resume cleanly if a session is interrupted (e.g. OOM
in the sandbox). See the "Commander-Rules Fork of Cockatrice" section of
[README.md](README.md) for the user-facing feature description.

> **See also:** [Design & Implementation Review (2026-07-16)](#design--implementation-review--2026-07-16)
> at the end of this file — a cross-cutting review covering known issues, the
> "reorganize-don't-re-architect" decision on a bigger rules engine, a
> token-efficiency plan for build/test iteration, and an EC2 hosting runbook for
> real multiplayer play-testing.

## Origin / goal

Fork of [Cockatrice/Cockatrice](https://github.com/Cockatrice/Cockatrice), adding
enforcement of the official [Commander/EDH rules](https://mtgcommander.net/index.php/rules/),
loosely following the phased design in
[jeffyche/fun-stuff's mtg-commander-rules-engine doc](https://github.com/jeffyche/fun-stuff/blob/master/design-docs/mtg-commander-rules-engine.md),
mirrored in-repo at
[`doc/design-docs/mtg-commander-rules-engine.md`](doc/design-docs/mtg-commander-rules-engine.md)
(that doc proposes a much larger 9–13 month roadmap; what's implemented here is a
real, scoped-down subset — deck validation + command zone + tax/damage counters +
life default — not the full stack/priority/combat engine).

### Tracking against the design doc's phases (§3)

| Design doc phase | Status here | Notes |
|---|---|---|
| §3 Phase 1: Foundation & Build Setup | **Done (reorganized 2026-07-16)** | Originally *diverged*: new logic lived directly in existing `libcockatrice_card`/`libcockatrice_models`/`libcockatrice_network` behind `isCommanderGame()` gates, with no separate rules library. **Reversed 2026-07-16 across two increments**: Increment 1 stood up `libcockatrice_rules/` + `RulesEngine` (server-side foundation, always-on). Increment 2 made the fork **wholly Commander-only** end to end (every client- and server-side `isCommanderGame()` gate removed; create-game/deck-editor UI collapsed to Commander-only; `CommanderRules`/`CommanderCounterNames` migrated into `libcockatrice_rules`, with `CommanderDeckValidator` deliberately left in `libcockatrice_models_deck_list` — see Increment 2's writeup below for why). Foundation-first: the library/seam landed first; real enforcement still grows inside it later (Increment 3+, not yet scoped). Still stays on Cockatrice's original branding/protocol (doc §0) — not forking to an independent ecosystem. See the [Design & Implementation Review](#design--implementation-review--2026-07-16) for the full increment history. |
| §3 Phase 2: Commander Deck Validation | **Done** | `CommanderDeckValidator` (100-card count, singleton, color identity, legality), wired into both server-side game-start and a live client-side deck-editor status label. |
| §3 Phase 3: Command Zone & Commander Tracking | **Done** | Command zone, commander tax counter, per-opponent commander-damage counters, client-side lethal-damage warning. Matches doc's proposed `CommanderState` fields (cast count, damage-dealt-to map) conceptually, implemented as counters rather than a dedicated struct, consistent with how Cockatrice already tracks all other numeric game state. |
| §3 Phase 4: Turn Structure Enforcement | **Partial** | Automatic untap-all and automatic draw at the untap/draw steps, gated to Commander games only (`Server_Game::isCommanderGame()`). Deliberately **not** implemented: phase-order enforcement (doc's "phase advancement requires explicit action or timer" — players can still freely jump phases, matching Assisted Mode's non-blocking philosophy), discard-to-hand-size at end step. See "Phase 4" section below for full detail. |
| §3 Phase 5: Priority & Stack System | **Partial (simplified)** | Real priority-passing (round-robin, protocol messages added) researched against XMage's `GameImpl.playPriority()`; no real stack (LIFO resolution of card effects) since that needs a card-rules engine this fork doesn't have. A round starts at one trigger (phase change, or a card moving onto the Stack zone) and simply stops when exhausted, rather than resolving a stack object or auto-advancing the phase. Full client UI: Pass Priority button, auto-pass toggle, cross-player priority highlight, log lines. See "Phase 5" section below. |
| §3 Phase 6: Mana System | **Narrow slice done (2026-07-16)** | Cost validation/auto-tap needs Phase 7 (out of reach) — still not implemented. The one in-scope slice (auto-empty mana pool at phase end, rule 500.4, reusing existing counters/hooks) is implemented, tested, and live-verified; see "Phase 6" section below. |
| §3 Phase 7: Card Ability System | **Increments 1–2 done, plus follow-ups** | Evergreen keyword recognition + display (Increment 1) and mana ability recognition + one-click tap-and-add (Increment 2, [`doc/design-docs/phase7-increment2-mana-abilities-plan.md`](doc/design-docs/phase7-increment2-mana-abilities-plan.md)) — both implemented, tested, and live-verified. Two follow-ups since: basic lands' whole-line-reminder-text ability recognized (previously missed), and mana-ability activation folded into the generic multi-select Tap action (batch tap-for-mana across a whole selection, with a choice dialog for ambiguous/dual-color abilities) instead of a separate per-card menu item. Increments 3–4 (triggered abilities, community ability data) still need a real rules engine and are out of scope. See "Phase 7" section below. |
| §3 Phase 8: Combat System | Not started | Out of current scope. |
| §3 Phase 9: State-Based Actions | **Partial** | Advisory (non-blocking) warnings, matching the existing commander-damage pattern, for the three other most common causes of loss: life ≤ 0 (rule 104.3a), drawing from an empty library (rule 104.3b), and ≥10 poison counters (rule 104.3c). See "Phase 9" section below. Not covered: any SBA that isn't a simple counter/zone threshold (e.g. legend rule, no-commander-in-any-zone edge cases). |
| §12: UI Design & Enhancements | Not started | No 4-player grid layout, stack/priority visualization, mana pool widget, or combat UI. Command zone has a basic panel (§12.6) but not the full commander-tax/partner display proposed. |

This fork's scope corresponds almost exactly to the design doc's own **§8
"Recommended Starting Point"** (deck validation, commander damage tracking via the
existing counter system, 40 starting life) — i.e. it deliberately validates the
architecture with the doc's own lowest-risk/highest-value slice before attempting
anything from Phases 4–9, rather than partially implementing the harder phases.

### Design philosophy (why it's built this way)

- Cockatrice is a manual/self-officiated "physical simulator" — no automated mana
  payment, combat damage resolution, turn structure, or state-based-action loss
  detection (not even life ≤ 0 today). So Commander tax/damage are implemented as
  **auto-created but manually-incremented counters** (same pattern as existing life
  totals), with an **advisory, non-blocking** client-side warning at lethal
  commander damage — not a hard auto-loss. This matches the design doc's own
  "Assisted Mode" philosophy.
- Reused Cockatrice's existing generic **Zone** (`Server_CardZone`/`CardZoneLogic`)
  and **Counter** (`Server_Counter`/`CounterState`) abstractions wherever possible
  instead of inventing new protocol messages — the entire diff is **protocol-compatible,
  zero `.proto` file changes**, which keeps risk low.
- Deck-construction rules (100-card singleton, color identity, legality) are
  enforced via the existing per-card legality-flag infrastructure in
  `DeckListModel`, extended rather than replaced.

## Phase 4: Turn Structure Automation

Implemented in a follow-up session after tasks 1–8 (which cover deck validation +
command zone/tax/damage, i.e. design doc §3 Phases 2–3) were complete and fully
tested. This is the first genuinely *automated* gameplay behavior in this fork —
everything before this was either passive counters (manually incremented, like
life totals) or client-side advisory warnings. Scoped narrowly given that higher
risk profile:

- **What it does:** `Server_Game::setActivePhase()` now triggers, Commander games
  only:
  - Entering the **Untap step** (phase index 0): untaps everything the active
    player controls, reusing the exact same `Server_AbstractPlayer::setCardAttrHelper()`
    call the existing manual "untap all" action already uses — including its
    existing respect for `AttrDoesntUntap`-flagged permanents.
  - Entering the **Draw step** (phase index 2): draws 1 card for the active
    player via the existing `Server_Player::drawCards()`, **except** the very
    first draw step of a strict **two-player** game's starting player (rule
    103.8a). Verified against the actual rules text rather than trusting the
    design doc's own phrasing ("skip on turn 1 in multiplayer") — rule 103.8c
    says multiplayer games never skip the first draw step; only 2-player and
    Two-Headed Giant games do. This fork's Commander default is 4-player
    free-for-all, so in practice this skip essentially only matters for Duel
    Commander (2-player).
  - The decision of *what* to do (`CommanderPhaseAutomation::{None,UntapActivePlayer,DrawForActivePlayer}`)
    is factored into a pure static method, `Server_Game::phaseAutomationFor(phase, turnNumber, playerCount)`,
    kept separate from the mechanical/side-effecting part specifically so the
    rules logic (including the skip-first-draw arithmetic) is unit-testable
    without needing a fully constructed, participant-registered game.
  - Phase index 0/2 meaning is only assumed *coupled to the client's phase
    ordering* (`cockatrice/src/game/phase.cpp`) — there's no shared server/client
    phase enum today (a pre-existing architectural gap, not something this
    fork introduces). Documented in a code comment at the point of use.
- **Correctness gap found and fixed along the way:** the existing command
  zone/tax/damage hooks (`Server_Player::setupZones()`, `onCardBeingMoved()`,
  `Server_Game::doStartGameIfReady()`) were **not actually gated by game format
  at all** — they ran for every game unconditionally, only "self-gating" by
  coincidence of deck content (a card happened to be set as the deck's banner
  card). Added `Server_Game::isCommanderGame()` (checks the room's selected
  game-type labels for a "commander" substring, mirroring the existing
  client-side check in `dlg_create_game.cpp`, which was refactored to share the
  same `CommanderRules::gameTypeLabelIsCommander()` helper) and gated the
  command-zone redirect in `setupZones()` on it. This matters because
  `DeckList::bannerCard` can be set as a purely cosmetic "cover card" on
  non-Commander decks too — before this fix, such a deck's cover card would
  have been incorrectly redirected into a command zone instead of the library.
- **Deliberately not done:** phase-order enforcement (a player can still freely
  jump to any phase in any order — Assisted Mode is about warnings, not
  blocking, and enforcing strict order is a bigger UX change affecting all
  play styles) and discard-to-hand-size at the end step (design doc's third
  Phase 4 bullet; left for a future increment).
- **Testing:** `tests/movecard_tests/commander_turn_structure_test.cpp` (12
  cases) — pure logic tests for `phaseAutomationFor()` (all phase/turn/player-count
  combinations, including the 103.8a/103.8c skip-first-draw distinction),
  integration tests for `isCommanderGame()` against real `Server_Game`/`Server_Room`
  objects (including an out-of-range game-type-index robustness case), and
  direct verification of the reused `setCardAttrHelper`/`drawCards` mechanisms
  (untap respects `AttrDoesntUntap`, draw moves the correct card). Plus 2 new
  cases in `commander_rules_test.cpp` for `gameTypeLabelIsCommander()`.
  **Not covered:** a true end-to-end test that `setActivePhase()` fires
  automation for a *registered* game participant — `Server_Game::addPlayer()`
  requires a live `Server_AbstractUserInterface`, and there's no lighter-weight
  seam to inject a participant for testing. Compensated for by testing the
  decision logic and the reused mechanisms separately, both directly.
- **Pre-existing bug found (not part of this fork's diff, but worth knowing for
  future sessions):** constructing and destructing multiple `Server_Game`/
  `Server_Room` instances as stack locals in sequence within one test process
  segfaulted deterministically (reproduced 3/3 plain runs, 0/5 under gdb — a
  classic timing/memory-layout-dependent signature). **Root cause found:**
  `Server_Game::~Server_Game()` (`server_game.cpp`) calls `deleteLater()` on
  itself at the very end of its own destructor body — undefined behavior
  (posting a deferred self-deletion event for an object that's already being
  destructed). Every other existing test using these classes
  (`reverse_card_move_test.cpp`) only ever constructs **one** instance per
  process, so this had never been hit before. Out of scope to fix as part of
  this Commander-focused diff (touches core game-lifecycle code with no
  Commander-specific angle). Workaround used in tests: heap-allocate
  `FakeServer`/`Server_Room`/`Server_Game` and deliberately never free them
  (the test process is short-lived) rather than use stack locals, which avoids
  exercising the buggy destructor path.

## Phase 5: Priority Passing (simplified stand-in for the Stack System)

Design doc §3 Phase 5 ("Priority & Stack System") calls for a real `Server_Stack`
(LIFO resolution) and `Server_PriorityManager`, with new protocol messages for
casting spells and resolving stack objects — a genuine card-rules engine
(parsing and executing arbitrary card effects), which is squarely out of reach
for this fork (no card-scripting engine exists here, and building one is its
own multi-month project — see design doc §3 Phase 7's own 8–12 week estimate
for "Card Ability System", a prerequisite for real stack resolution).

**Researched before implementing:** looked at how existing open-source MTG
rules engines structure this, specifically
[Forge](https://github.com/Card-Forge/forge) and
[XMage](https://github.com/magefree/mage) (both GPL-licensed, community-built,
full rules-enforcing engines — see
[cgomesu.com's comparison](https://cgomesu.com/blog/forge-xmage-mtg/)). Fetched
and read XMage's `GameImpl.playPriority()` (`Mage/src/main/java/mage/game/GameImpl.java`)
directly from `github.com/magefree/mage` for the canonical algorithm shape:
round-robin priority passing starting from the active player; if a player acts,
passed-flags reset and the round restarts; once everyone passes in succession,
resolve the top stack object if the stack is non-empty (then reset and
continue), otherwise end the priority round (advance to the next step/phase).
This is the real CR 117.3b/117.4/405.5 priority algorithm, implemented in Java
against a full card-effects engine neither of which exist in this C++/Qt fork.
**Borrowed the algorithmic shape, not the code** (different language, and
Cockatrice's architecture — server-authoritative state with fully-manual client
actions — is fundamentally different from XMage's simulate-and-execute model).

**What's implemented — deliberately simplified from the above:**
- New protocol messages (first time this fork has touched `.proto` files —
  every prior phase stayed protocol-compatible; this genuinely needs a new
  command/event pair): `Command_PassPriority` (`GameCommand` ext 1035) and
  `Event_PriorityChanged` (`GameEvent` ext 2023, `priority_player_id` field).
  Both purely additive — no existing message changed.
- `Server_Game` tracks `priorityPlayerId` and `priorityPassedBy`, Commander
  games only (`isCommanderGame()`). Resets to the active player at the start
  of every phase (in `setActivePhase()`, alongside the existing untap/draw
  automation). `Server_Player::cmdPassPriority()` validates the caller
  currently holds priority (or is a judge), then calls
  `Server_Game::advancePriority()`.
- `advancePriority()` finds the next non-passed, non-conceded player in turn
  order via the pure, unit-tested `Server_Game::nextPriorityPlayer()`. If
  everyone eligible has passed, **this is where the simplification from real
  Magic happens**: rule 117.4 would resolve the top of the stack here; instead,
  since this fork's `STACK` zone (already existed, per-player, purely a manual
  visual aid with no resolvable objects — see the original architecture
  research) has nothing this fork can execute, "everyone passed" simply
  **stops** — priority becomes held by no one (`priority_player_id: -1`) until
  the next priority-triggering event. **This replaced an earlier version that
  auto-advanced the phase/turn here instead** — see "Correction: priority
  model and round semantics" below for why that was wrong and what changed.
  Players who want to represent casting/resolving something via the Stack
  zone still do so manually, exactly as before this feature existed —
  priority-passing doesn't block or require that (moving a card onto the
  Stack zone *does* now start a fresh priority round at the mover, see below,
  but doesn't require or block the move itself).
- **Priority is advisory, not enforcement**: holding or not holding priority
  does **not** gate any other existing command (moving cards, tapping,
  drawing, etc. all remain available to any player at any time, as today).
  This mirrors the Assisted Mode philosophy used throughout this fork and
  avoids a much bigger, riskier behavioral change (blocking actions based on
  priority) that couldn't be verified without live multiplayer play-testing.
- Tests: `Server_Game::nextPriorityPlayer()` pure logic (8 cases: advance,
  wrap-around, skip-passed, skip-conceded, all-ineligible, solo player, empty
  order, never-returns-self) plus a `cmdPassPriority` gating test
  (game-not-started rejection — see the file for why deeper integration
  testing isn't lightweight here, same limitation as Phase 4's tests).
- **Client-side UI: done** (a later session). A "Pass Priority" button (gold
  double-chevron icon, `cockatrice/resources/phases/pass_priority.svg`) in the
  phase toolbar, gated on `GameMetaInfo::isCommanderGame()`, sends
  `Command_PassPriority`; a `GameEventHandler::priorityChanged` signal wires
  the previously-server-only `Event_PriorityChanged` into
  `TabGame::setPriorityPlayer()`, pulsing the button (reusing `PhaseButton`'s
  existing active-highlight animation) while the local player holds priority.
  See "UX additions" below for the auto-pass toggle, cross-player priority
  highlight, and log lines built on top of this.

### Correction: priority model and round semantics

After the client UI above shipped, live verification (screenshot + debug-log
cross-referencing, per this repo's established practice) surfaced a real
**infinite loop**: enabling the new auto-pass toggle (see below) in a solo
test game caused `Command_PassPriority` to fire forever, stopped only by
servatrice's own flood protection ("You are flooding the game"). Root cause:
the original `advancePriority()` auto-advanced the phase whenever a round was
exhausted, which *also* reset priority back to the active player — in a
solo game (or last-player-standing), that's the same player who just passed,
so auto-pass re-fired immediately, forever.

The user's correction, now implemented: **a priority round is scoped to
exactly one triggering event** (a phase/step change, or a spell cast/ability
activation — modeled here as a card moving onto the Stack zone) **and starts
at exactly one player** (the active player for a phase change, or whoever
triggered it for a spell/ability). If that round is exhausted (everyone
passes in succession with no new trigger), **priority simply stops** — no one
holds it — rather than cascading into an automatic phase change. This
restores the fork's core "manual physical simulator" principle (see
`CLAUDE.md`'s design principles): phase advancement is always a deliberate
player action.

Implementation:
- `Server_Game::broadcastPriorityChange(int playerId)` — shared helper
  (clear who's passed, set the new holder, broadcast) — used by both
  `setActivePhase()` (phase-change trigger) and the new
  `Server_Game::resetPriorityTo(int playerId)`.
- `Server_Player::onCardBeingMoved()` calls `resetPriorityTo(playerId)`
  whenever a card moves onto the `STACK` zone from anywhere else (rules
  601.2i/602.2h/117.3d simplified) — this is how a spell cast or ability
  activation starts a fresh priority round at the caster, since this fork
  represents both as a manual move to the Stack zone. Reordering cards
  already on the stack doesn't retrigger it.
- `advancePriority()`'s exhaustion branch now broadcasts
  `priority_player_id: -1` and returns, instead of calling `nextTurn()` /
  `setActivePhase()`.
- An initial client-side band-aid (a "once per phase" auto-pass guard) was
  tried and discarded once the server fix was understood — it would have
  wrongly suppressed legitimate auto-pass after a *second* spell cast in the
  same phase. The corrected server model needs no client-side guard at all:
  once priority becomes "no one" (-1), it can never equal any real local
  player id, so the auto-pass trigger condition naturally stops firing.
- Verified live: manually passing priority now logs "Everyone has passed.
  No one has priority." with no automatic phase change; enabling auto-pass
  and advancing a phase sends exactly one `Command_PassPriority` and then
  stays stable (checked via repeated `grep -c` against the debug log with a
  pause in between) with no flood warning. The new spell/ability trigger
  (`resetPriorityTo` via a Stack-zone move) reuses the same, already-verified
  `broadcastPriorityChange()` path as the phase-change trigger, but wasn't
  independently exercised through the live UI (moving a card onto the Stack
  zone via synthetic input) in this session — worth a follow-up live check.

### UX additions (built on the corrected model)

- **Auto-pass toggle**: double-click the Pass Priority button (mirrors the
  existing Untap/Draw double-click-for-alternate-action convention) to
  auto-send `Command_PassPriority` whenever the local player holds priority.
  Off by default. A small cyan dot badge (new `PriorityButton` subclass of
  `PhaseButton` in `phases_toolbar.{h,cpp}`, following the same
  subclass-for-one-visual pattern as `CommandZone`/`PileZone`) indicates when
  it's on.
- **Priority-holder highlight, visible to every player/spectator** — not just
  locally. The toolbar pulse only ever showed *your own* client whether *you*
  held priority; there was no way to see who else had it. New
  `PlayerLogic::holdsPriorityChanged` signal, plumbed through
  `PlayerGraphicsItem` to `PlayerTarget` (the avatar/name badge rendered on
  the shared board), draws a 4px cyan border around the current holder's
  badge — a different visual channel from the existing white active-turn
  outline on `TableZone`, since priority and the active turn are tracked
  independently.
- **Priority-passing log lines**: new
  `GameEventHandler::logPriorityChanged`/`logPriorityCleared` signals wired
  to `MessageLogWidget`, logging both "`<player>` has priority." and
  "Everyone has passed. No one has priority."

## Phase 6: Mana System — auto-empty slice implemented (2026-07-16)

**Status: the narrow "auto-empty at phase end" slice below is implemented, tested, and
live-verified.** The user picked this from three offered next steps (the alternatives
were migrating command-zone/tax logic into `RulesEngine`, or building more
`.uitest/scenario.py` scenarios). Cost validation/auto-tap/payment remain out of scope,
per the original scoping below.

**What's implemented**: `Rules::RulesEngine::manaCounterNames()` (new static method,
`libcockatrice_rules/libcockatrice/rules/rules_engine.{h,cpp}`) returns the fixed list
`{"w","u","b","r","g","x"}` — the names `Server_Player::setupZones()` already gives its
per-player mana counters. `Server_Player::emptyManaPool(GameEventStorage &ges)` (new
method, `server_player.{h,cpp}`) loops that player's counters, zeroing (via the existing
`Server_Counter::setCount(0)`) any whose name is in that list and enqueuing
`Event_SetCounter` only for ones that actually changed (reusing `setCount()`'s existing
`didChange` return, same dedup convention as `cmdSetCounter`/`cmdIncCounter`).
`Server_Game::setActivePhase()` calls it for **every** player (not just the active one,
unlike the untap/draw automation above) on every phase/step transition, via one shared
`GameEventStorage` sent in a single batch.
- **Tests**: `tests/rules/rules_engine_test.cpp` gained
  `ManaCounterNamesCoversTheFiveColorsPlusColorless` (asserts the exact 6-name list).
  Full suite: **20/20 pass**, zero regressions. `format.sh --cmake --branch master` clean
  (no changes needed).
- **Verified live** (local servatrice + Xvfb + real client, debug-log cross-reference —
  not just screenshots, per this repo's log-first testing practice): a real solo
  Commander game (Atraxa, Forest, Sol Ring), starting mana pool at 0 for all 6 colors.
  Manually set White to 1 (`Command_IncCounter` → `Event_SetCounter { counter_id: 1
  value: 1 }`), then advanced Untap → Upkeep: `Event_SetActivePhase { phase: 1 }` was
  immediately followed by `Event_SetCounter { counter_id: 1 value: 0 }` — no client-side
  action needed, the pool emptied server-side on the phase transition alone. Repeated
  with Green (set to 1, advanced Upkeep → Draw): same auto-empty fired for Green
  (`counter_id: 5`), and critically **no spurious event fired for the already-zero White
  counter** — confirming the `didChange` dedup avoids event spam on every single phase
  change for players with an empty pool (the common case). Life (40), Commander Tax (0),
  and poison (0) were confirmed untouched throughout (no unexpected `Event_SetCounter`
  for those counter ids in the log).
- **Deliberately still excluded**, unchanged from the original scoping: cost parsing,
  `canPay()` validation, auto-tap suggestions, mana-ability activation from context
  menus (Phase 7's territory) — this slice only ever zeroes counters, never reads or
  validates a cost.

### Original scoping (context for the slice above)

Design doc §3 Phase 6 (4–5 week estimate) specs a `ManaPool`/`ManaCost` pair that
parses a card's printed cost and validates/auto-pays it (`canPay()`, `pay()`,
auto-tap UI). That's a hard no at this fork's scope: cost validation needs a
parsed mana cost per card, which needs Phase 7's card-ability engine (the
design doc's own 8–12 week estimate) as a prerequisite — the same reason real
stack resolution (Phase 5) and combat (Phase 8) are out of reach.

**What's already there, and was easy to miss**: Cockatrice's existing
`Server_Player::setupZones()` already creates per-player `w`/`u`/`b`/`r`/`g`/`x`
counters (ids 1–6, colored, manually incremented via left/right-click or a
"Set counter..." dialog) for **every** game, not just Commander — this is
already vanilla Cockatrice's answer to "mana pool" under its manual-simulator
model, just unlabeled and with no phase-boundary behavior. No new counters,
UI, or protocol are needed to have *a* mana pool; it's been sitting there the
whole time.

**The one gap that actually fits this fork's scope and pattern**: rule 500.4
— a mana pool empties at the end of every step and phase. Today nothing does
that; W/U/B/R/G/C counters silently persist turn to turn, which is wrong but
harmless (Cockatrice already tolerates far larger manual-bookkeeping gaps by
design). Automating just the emptying — not the payment/validation side — is
mechanically identical to Phase 4's auto-untap/auto-draw and Phase 9's SBA
counters: reuse `Server_Game::setActivePhase()` (already fires on every
phase/step transition, confirmed granular to the real 11 steps/phases in
`cockatrice/src/game/phase.cpp`, not just top-level phases) and
`Server_Counter::setCount(0)` (the exact primitive `cmdSetCounter()` already
uses), looped over **every** player's mana counters, not just the active
player's (rule 500.4 empties everyone's pool, unlike untap/draw which are
active-player-only). Gate on `isCommanderGame()`, same as every other
automation in this fork.

**Estimated size/risk**: small — one new loop in an already-existing,
already-tested hook, no new protocol messages, no new client UI, no card
data. Comparable to Phase 4's automation work, not the design doc's 4–5 week
Phase 6 estimate (which is almost entirely the cost-validation/auto-tap part
this scope explicitly excludes). Main risk is player confusion if pool-empty
fires somewhere real players don't expect it to (e.g. mana that under real
rules would persist through a "spell can add mana this step" exception,
which this simplified version can't detect) — same category of
already-accepted simplification as Phase 4's phase automation.

**Deliberately excluded from this scope, and why**: cost parsing, `canPay`
validation, auto-tap suggestions, mana-ability activation from context
menus — all need Phase 7's card-ability engine to know what a card costs or
does, which doesn't exist here and isn't a reasonable unilateral addition
(explicit user sign-off required before any of Phases 6–8 gets a real
implementation pass, per `CLAUDE.md`'s design principles).

**Status: scoped only, not implemented.** Written up for a decision on
whether the narrow "auto-empty at phase end" slice above is worth building,
same as Phase 5 was researched and documented before implementation was
authorized.

## Phase 7: Card Ability System — Increments 1–2 (keyword recognition, mana abilities)

Design doc §3 Phase 7 (8–12 week estimate, four increments) is the prerequisite for
real stack resolution, mana cost validation, and combat — a genuine card-rules engine
(parse rules text, execute effects, targeting). That stays explicitly out of reach here,
same conclusion as every other phase-6-through-8 note in this doc. After Phase 6 was
scoped down to a documented decision point rather than implemented, the user directed
moving to "prerequisite" Phase 7 work specifically — this increment is that: the
**smallest, self-contained, read-only slice**, deliberately chosen to not cross into
ability execution.

**What's implemented**: `CardKeywords::parse(const CardInfo&)` (new
`libcockatrice_card/libcockatrice/card/ability/card_keywords.{h,cpp}`, deliberately *not*
under `format/` alongside `CommanderRules` — this is general card-ability infrastructure,
not Commander-specific, so it's kept in its own clearly-separated location) recognizes a
card's printed evergreen keyword abilities (Deathtouch, Defender, Double strike, First
strike, Flash, Flying, Haste, Hexproof, Indestructible, Lifelink, Menace, Reach, Trample,
Vigilance — the classic costless evergreen set; keywords with attached costs/variable text
like "Ward {2}" are excluded, since a plain string match isn't meaningful for those) from
rules text, and `cockatrice/src/interface/widgets/cards/card_info_text_widget.cpp` displays
them as a "Keywords:" row in the existing card-info properties table (both in the deck
editor and in-game — this widget is shared by `CardInfoFrameWidget`, used in both places).

- **Parsing approach, and why it's conservative**: reuses the same
  regex-over-rules-text pattern already proven for `CommanderRules::colorIdentity()`
  (reminder text in parentheses stripped first, same convention). Rather than a naive
  substring search for keyword names (which would false-positive on abilities that merely
  *mention* a keyword — e.g. "Destroy target creature with flying" doesn't itself have
  flying), each line of rules text is only treated as a keyword line if it reduces
  entirely to a comma/"and"-separated list of recognized keywords with nothing else on
  it. This correctly excludes granted/conditional abilities ("Whenever this creature
  attacks, it gains flying until end of turn") while still catching real printed keyword
  lines like "Flying, vigilance, deathtouch, lifelink" (Atraxa, Praetors' Voice's actual
  printed text, used as the live-verification case below).
- **Explicitly display-only**: nothing reads `CardKeywords::parse()`'s output to gate,
  automate, or enforce anything — no combat logic, no ability activation, no rules
  effect. It exists purely so a player can see which evergreen keywords a card has
  without reading the full rules text. This keeps it firmly on the "advisory" side of
  this fork's Assisted-Mode philosophy, same category as the SBA warnings in Phase 9.
- **Known heuristic limits, stated plainly**: this is pattern-matching on text
  formatting conventions, not real natural-language understanding — a card whose keyword
  line has unusual phrasing, or a future card that breaks the "keyword lines are pure
  comma lists" convention, could be missed (false negative) or misread. Given the
  display-only, non-blocking use, that's an acceptable trade-off consistent with how the
  rest of this fork treats its other regex-based text parsing (color identity has the
  same class of edge cases, documented in its own code comments).
- **Testing**: new `tests/card_ability/card_keywords_test.cpp` (12 cases, GTest, no
  card database needed — constructs `CardInfo` directly like `commander_rules_test.cpp`):
  single keyword, trailing period, comma-separated list, "and"-separated list,
  case-insensitivity/canonicalization, multiple keyword lines, reminder-text stripping,
  the two false-positive-avoidance cases above (granted keyword, mentioned keyword),
  empty text, a keyword line alongside an unrelated ability line, and the keyword-list
  constant itself. All 12/12 pass.
- **Verified live**, not just compiled: local `cockatrice` client, deck editor, added
  Atraxa Praetors' Voice (whose real printed text is "Flying, vigilance, deathtouch,
  lifelink"), confirmed via screenshot that the card-info panel's Description tab shows
  a new "Keywords:" row reading "Deathtouch, Flying, Lifelink, Vigilance" (alphabetically
  sorted, canonical capitalization) — correctly parsed from the real card data, not a
  synthetic test fixture.
- **Deliberately excluded**: Increments 2–4 (mana-ability parsing/auto-activation,
  triggered-ability parsing/queueing, community ability-data file) all need real
  execution semantics this fork doesn't have infrastructure for. Not attempted.

**Status: Increment 1 implemented, tested, and live-verified.** Increments 3–4 remain
out of scope pending a real design discussion, per the same guardrail as Phases 6 and 8.

### Increment 2: Mana ability recognition + one-click activation — implemented

Authorized by the user after Increment 1 shipped ("we should build it"). Implemented
following the plan doc, [`doc/design-docs/phase7-increment2-mana-abilities-plan.md`](doc/design-docs/phase7-increment2-mana-abilities-plan.md),
with one deliberate deviation from its dispatch-wiring suggestion, noted below.

**What's implemented**: `ManaAbilities::parse(const CardInfo&)` (new
`libcockatrice_card/libcockatrice/card/ability/mana_abilities.{h,cpp}`, same location
convention as `CardKeywords`) recognizes a card's simplest fixed-color `{T}: Add
{X}{X}...{X}.` mana abilities — every `{X}` must be the same single mana symbol
(`{W}{U}{B}{R}{G}{C}`), repeated symbols are fine (Sol Ring's `{T}: Add {C}{C}.` → one
`ManaAbility{"C", 2}`), and a card can have more than one qualifying line. Any line with
a player choice ("Add one mana of any color"), an additional/alternative cost, or
trailing conditional text fails the exact-line match and is skipped, never guessed at —
same conservative philosophy as `CardKeywords::parse()`. In the client, `CardMenu`
(`cockatrice/src/game_graphics/player/menu/card_menu.cpp`) calls this parser when
building a table-zone card's context menu and — only if the card is currently untapped —
adds one "Tap: Add {X}..." action per qualifying line, icon-colored to match the mana
symbol's actual counter color (read live from `PlayerLogic::getCounters()`, not a
hardcoded duplicate palette). Clicking the action calls a new
`PlayerActions::actActivateManaAbility(const CardItem *card, const QString &manaSymbol,
int amount)`, which batches a `Command_SetCardAttr` (tap) and a `Command_IncCounter`
(the matching w/u/b/r/g/x counter, incremented by `amount`) into the same command list
every other manual action in this fork already uses.

- **Deliberate deviation from the plan doc**: the plan suggested adding a new
  `CardMenuActionType` enum value and routing through the existing
  `PlayerActions::cardMenuAction(QList<CardItem*>, CardMenuActionType)` dispatcher (the
  same one `cmTap`/`cmClone`/etc. use). Reading that dispatcher's actual behavior showed
  why that doesn't fit here: every action reachable through it operates on
  `gameScene->selectedCards()` — the *current multi-selection* at click time, which can
  differ from the specific card that was right-clicked (right-clicking a card doesn't
  change the existing selection in this codebase). That's fine for generic actions like
  tap (applying "tap" to an unrelated multi-selection is still meaningful), but wrong
  here: the mana symbol/amount is parsed from one specific card's text, so applying it to
  a different selected card would tap the wrong permanent and add the wrong color/amount
  of mana. Instead, this follows the codebase's other existing precedent for
  actions needing per-instance data beyond a bare type tag —
  `actAddCardCounter(QList<CardItem*>, int counterId)` — and takes the specific `card`
  the menu was built for directly, bypassing `cardMenuAction()`/`CardMenuActionType`
  entirely. No enum value was added.
- **Counter naming quirk, reused correctly**: colorless mana's counter is named `"x"`,
  not `"c"` (`Server_Player::setupZones()`) — both the client-side icon-color lookup and
  the server-command counter-id lookup replicate this mapping (small, independent copies
  in `card_menu.cpp` and `player_actions.cpp`, same "second copy is fine" convention as
  `withoutReminderText()`'s three existing copies).
- **Testing**: new `tests/card_ability/mana_abilities_test.cpp` (9 cases, GTest, no card
  database needed): single fixed-color line, repeated-symbol line (Sol Ring), reminder
  text stripped (a parenthesized basic-land ability line correctly produces nothing),
  mixed-symbol choice line not matched, additional-cost line not matched, conditional
  line not matched, two qualifying lines both returned, no qualifying lines returns
  empty, case-insensitive tokens matched. All 9/9 pass, and the full `ctest` suite
  (19 executables, up from 18) passes with zero regressions.
- **Verified live**, not just compiled: local `servatrice` + Xvfb + the real client, a
  real 1-player Commander game. Confirmed via screenshot and debug-log cross-reference:
  - Sol Ring (real printed text `{T}: Add {C}{C}.`, already in
    `.uitest/sample_cards.xml`) shows a new "Tap: Add {C}{C}" context-menu action while
    untapped, in the expected position (right after "Turn Over", before "Clone").
  - Clicking it sends one batched command,
    `Command_SetCardAttr { zone: "table" card_id: 2 attribute: AttrTapped attr_value:
    "1" }` + `Command_IncCounter { counter_id: 6 delta: 2 }`, and the server responds
    with `Event_SetCounter { counter_id: 6 value: 2 }` (counter 6 = "x"/colorless,
    confirmed against the game's own counter list) + the matching
    `Event_SetCardAttr` — exact wire-level confirmation the tap and the +2 colorless
    landed together, not just that the card visually rotated.
  - Right-clicking the now-tapped Sol Ring again correctly no longer shows the action.
  - Right-clicking Baleful Strix (untapped, real printed text `Flying, deathtouch` — no
    mana ability) correctly shows no such action, confirming the parser doesn't
    false-positive on an unrelated keyword-only card.
- **Deliberately excluded**: same boundary as the plan doc — player-choice mana
  abilities (Command Tower and similar), additional/alternative-cost abilities,
  conditional/restricted abilities, non-`{T}` mana effects, and any auto-payment during
  casting (that's Phase 6, still undecided). Increments 3–4 (triggered abilities,
  community ability data) remain out of scope pending a real design discussion.

**Status: implemented, tested, and live-verified.**

### Follow-up fix: basic lands were excluded by the reminder-text strip

Found while answering a user question about the mana pool, before starting a new increment: the
original `ManaAbilities::parse()` stripped **all** parenthesized text as reminder text before
line-matching, same convention as `CardKeywords`/`CommanderRules`. That's correct for reminder
text that merely *explains* an unrelated printed ability, but basic lands are a special case —
their mana ability (rule 305.6) isn't a printed ability at all, so Oracle/MTGJSON text renders it
as reminder text that **is** the entire line (e.g. Forest's real text, `({T}: Add {G}.)`, exactly
matching this repo's own `.uitest/sample_cards.xml`). The blanket strip discarded that whole line,
so Forest/Island/Swamp/Mountain/Plains — the most common mana source in any real game — never got
the one-click "Tap: Add {X}" action, only nonbasic mana rocks/lands whose ability text isn't
wrapped in parens (e.g. Sol Ring).

**Fix**: `ManaAbilities::parse()` now checks each line for whether the parentheses wrap the
*entire* line first; if so, the parens are unwrapped and the inner text is matched directly,
rather than stripped. A line where reminder text is only part of a larger line (explaining an
unrelated ability) still goes through the original strip-then-match path unchanged. Same "reduces
entirely to X" conservatism as the rest of this parser — no broadening of what counts as a match,
just correcting which text counts as the ability line for the one whole-line-parenthetical case
basic lands hit.

- Updated `tests/card_ability/mana_abilities_test.cpp`: replaced `ReminderTextIsStripped` (which
  asserted the now-corrected-away behavior) with `BasicLandWholeLineReminderTextIsRecognized`
  (Forest's real text now returns `ManaAbility{"G", 1}`) and added
  `MidLineReminderTextIsStillStripped` (`"Flying (reminder text explaining flying.)"` still
  correctly returns empty, confirming non-whole-line reminder text is unaffected). All 10/10
  cases pass.
- **Verified live**, not just compiled: relinked `cockatrice` against the updated
  `libcockatrice_card` (no full rebuild needed — only this one library changed), local servatrice
  + Xvfb + a real solo Commander game with a 3-card test deck (Atraxa, Forest, Sol Ring). Drew and
  played the real `Forest` card from `.uitest/sample_cards.xml`, confirmed via screenshot: the
  Card Info panel shows its actual text `({T}: Add {G}.)`, right-clicking it now shows "Tap: Add
  {G}" (previously absent), clicking it taps the Forest and the sidebar's green mana counter goes
  from 0 to 1 — cross-referenced against the message log ("testuser sets counter Green to 1 (+1)"
  / "testuser taps Forest."). Right-clicking again post-tap correctly shows no action, matching
  existing untapped-only gating.

### Follow-up feature: multi-select tap-for-mana + color-choice abilities

User feedback after the reminder-text fix above: manually right-clicking each land was still
tedious with many mana sources selected at once (e.g. five Forests), and asked for the *existing*
generic "Tap / Untap" action itself to handle mana for a whole multi-selection in one click, with
a picker dialog for any card whose contribution isn't a single unambiguous color (a genuine choice
ability like a dual land or Command Tower, or a card with more than one qualifying ability line).
Explicitly scoped down for cards needing battlefield/commander-state inspection to resolve (e.g.
Command Tower's real "in your commander's color identity" restriction, Reflecting Pool's "already
produced by a land you control"): per the user's direction, these are simplified to "any of the
five colors, let the player pick the legal one themselves" rather than attempting real inspection.

**`ManaAbilities` parsing extended** (`mana_abilities.{h,cpp}`): `ManaAbility` changed from a
single `producedSymbol` field to `symbolOptions` (a `QStringList`, `isChoice()` when it has more
than one entry). Three shapes now recognized, in addition to the existing fixed-color one:
- `{T}: Add {X} or {Y}.` / `{T}: Add {X}, {Y}, or {Z}.` (comma/"or"-separated, as opposed to the
  existing fixed pattern's directly-*concatenated* symbols, which still means "all simultaneously"
  — e.g. Sol Ring's `{C}{C}`, unchanged) — a real regex-parsed choice among the listed colors.
- `{T}: Add one mana of any color.` and Command Tower's real qualified phrasing (`"...in your
  commander's color identity."`) — recognized by exact phrase match, both simplified to a 5-way
  choice among W/U/B/R/G.
- Reflecting Pool's real phrasing (`"Add a color of mana already produced by a land you
  control."`) — same 5-way-choice simplification.
- New tests in `mana_abilities_test.cpp` cover all three shapes plus a directly-concatenated
  mixed-symbol negative case; full suite now 15/15.

**Client-side integration — folded into the generic Tap action, not a separate menu item**: the
previous Increment 2 per-card "Tap: Add {X}" context-menu action (`CardMenu::addManaAbilityActions()`)
is removed entirely. Instead, `PlayerActions::cardMenuAction()`'s `cmTap`/`cmUntap` handling now
intercepts *before* the existing generic per-card toggle loop (design doc §3 Phase 7 "Increment 2"
follow-up, since it needs to reason about the whole selection at once, not one card at a time):
- `computeManaTapChoices(cardList)` — for every table-zone, currently-untapped card in the
  selection, flattens its `ManaAbilities::parse()` result into individual (color, amount) options
  (one fixed ability = one option; a choice ability, or more than one qualifying ability line on
  one card = one option per color). Cards with exactly one option are unambiguous; cards with more
  are collected as pending choices.
- If nothing needs a choice, `actApplyTap()` sends one batch immediately: a tap-toggle command per
  selected card (existing per-card-toggle semantics unchanged — mixed-selection tap/untap still
  toggles each card independently) plus one `Command_IncCounter` per card being tapped with an
  unambiguous mana ability. No dialog, true one-click for the common case (e.g. five Forests).
- If any cards need a choice, nothing is sent yet — `requestManaAbilityChoiceDialog(cardList,
  choices)` is emitted instead (same request/response pattern as this file's other dialogs, e.g.
  `requestSetPTDialog`). `PlayerDialogs::onManaAbilityChoiceDialogRequested()` builds a small
  `QDialog` (one `QComboBox` row per ambiguous card, OK/Cancel), and only on OK calls
  `actApplyTap(cardList, chosenOptions)` — which rebuilds the *entire* batch (toggles for every
  selected card + auto-applied mana for unambiguous cards + the user's chosen color for ambiguous
  ones) from scratch and sends it as one command list. Cancel sends nothing at all — the action is
  atomic, no partial tap/mana state on cancel (verified live below).
- `manaTapOptionsForCard()` (the per-card flattening helper) and `actApplyTap()` both live in
  `player_actions.cpp`; the small `ManaTapOption`/`ManaTapChoice` plain structs are declared in
  `player_actions.h` so both `PlayerActions` and `PlayerDialogs` can share them across the
  signal/slot boundary.
- **Verified live**, not just compiled: relinked `cockatrice` (only the `libcockatrice_card` +
  `cockatrice` targets needed rebuilding), local servatrice + Xvfb + a real solo Commander game
  with a 6-card deck (Atraxa, 3×Forest, Command Tower, Sol Ring). Three cases confirmed via
  screenshot + message-log cross-reference:
  1. Rubber-band-selecting all 3 Forests and choosing "Tap / Untap" once tapped all three
     simultaneously and incremented the green counter 0→3 in one batch, no dialog — the "five
     Forests" case the user asked for.
  2. Ctrl-selecting Command Tower (a 5-way choice, its real printed text confirmed in the Card
     Info panel: `{T}: Add one mana of any color in your commander's color identity.`) together
     with Sol Ring (unambiguous) and tapping: a dialog appeared with exactly one row ("Command
     Tower", a 5-item combo box: Add {W}/{U}/{B}/{R}/{G}) — Sol Ring correctly got no row. Picking
     {U} and clicking OK tapped both cards and set Colorless to 2 (+2, from Sol Ring, auto-applied)
     and Blue to 1 (+1, from the chosen option) in the same batch.
  3. Untapping both, then tapping Command Tower alone and clicking Cancel on the dialog: confirmed
     via screenshot and log (no new entries) that the card stayed untapped and no counters changed
     — the all-or-nothing cancel behavior works as designed.
- **Deliberately excluded**: real battlefield/commander-color-identity inspection for Command
  Tower/Reflecting-Pool-style abilities (simplified to "any of five colors" instead, per explicit
  user direction); mana dorks/creatures work through the same generic path as lands/artifacts
  (any table-zone permanent with a recognized ability qualifies) but weren't separately live-tested
  this session, since the codepath doesn't distinguish card type — only zone/tapped-state and the
  parsed ability data matter.

**Status: implemented, tested, and live-verified.**

## Phase 9: State-Based Actions (advisory warnings)

Design doc §3 Phase 9 calls for full state-based-action checking. Real Magic re-checks
SBAs continuously (rule 704.3) and would need to interrupt/replace this fork's manual,
non-blocking model to do that properly — out of scope for the same reason turn
structure/priority enforcement is only ever advisory here (see CLAUDE.md's design
principles). What's implemented instead follows the exact pattern already established
for lethal commander damage (§3 Phase 3): a client-side `QMessageBox::warning` fired the
moment a counter/zone crosses its lethal threshold, informational only — it does not end
the game, remove the player, or block further actions. Three more of the most common
real-game loss conditions are covered this way:

- **Life ≤ 0** (rule 104.3a) — `PlayerGraphicsItem::onCounterAdded()` connects to the
  existing `life` `CounterState::valueChanged` signal (Commander games only, via
  `isCommanderGame()`) and fires on the `> 0 → ≤ 0` crossing. No server changes needed —
  `life` already exists as a counter for every game.
- **Drawing from an empty library** (rule 104.3b) — hooks the existing
  `PlayerEventHandler::logDrawCards(PlayerLogic*, int number, bool deckIsEmpty)` signal
  (already emitted for the message log at every draw, `deckIsEmpty` computed from
  `_deck->getCards().size() == 0` after the draw resolves) rather than adding any new
  signal or server logic. Fires when a draw was attempted (`number == 0`) and the library
  was already empty. Commander games only.
- **≥10 poison counters** (rule 104.3c) — needed one small server-side addition, since
  poison (unlike life) isn't among Cockatrice's default per-player counters:
  `Server_Player::setupZones()` now creates an 8th counter, `poison` (green, starting at
  0), gated on `game->isCommanderGame()` — same gating and same manually-incremented
  pattern as the existing tax/damage counters. New constants added to
  `commander_counter_names.h`: `poisonCounterName()` and `LETHAL_POISON_COUNTERS = 10`.
  Client-side warning wired the same way as the life-total one, watching for the
  `< 10 → ≥ 10` crossing. No dedicated icon exists in the default theme for a counter
  named `poison` (`cockatrice/resources/counters/` only has `w`/`u`/`b`/`r`/`g`/`storm`/
  `general`), so it renders via the existing `general.svg` fallback already used by other
  unthemed counters (`x`, `storm`) — cosmetic only, not a functional gap.
- All three follow the same dedup approach as the pre-existing commander-damage warning:
  the check is on the *crossing* (`oldValue`/`newValue` compared against the threshold),
  not "is currently past it", so re-opening a game state or receiving a redundant
  `CounterState` update doesn't re-fire the dialog.
- **Verified live**, not just compiled: local servatrice + a solo Commander game with a
  1-card deck (the commander only, so the library starts empty in the same session).
  Confirmed via screenshot for all three, in order: setting `life` to 0 via the counter's
  "Set counter..." dialog produced "testuser's life total has reached 0 and they have
  lost the game (rule 104.3a)."; setting the new `poison` counter (found via its
  "Set counter..." dialog title, since it has no distinct icon — see above) to 10
  produced "testuser has 10 poison counters and has lost the game (rule 104.3c).";
  clicking the Draw phase button with an already-empty library (the 1-card deck's only
  card, the commander, starts in the command zone) produced "testuser attempted to draw
  from an empty library and has lost the game (rule 104.3b)." No crashes, no spurious
  re-fires, and both the client debug log and servatrice log were clean of anything
  related to the new code (the only log noise present — a benign zero-byte parse at
  initial connect, one `RespContextError` from an unrelated duplicate UI click, and
  expected `type=none`-config "driver not loaded" database lines — all pre-existed this
  change and were cross-checked as unrelated).
- **Deliberately not done**: no other SBAs from rule 704 (e.g. the legend rule, 0-toughness
  creatures, auras attached illegally) — those require either card-state modeling this
  fork doesn't have (legend rule needs to know which permanents share a name) or a
  continuous-checking loop that doesn't fit the manual-simulator model. This phase only
  picked the SBAs that were pure counter/zone-threshold checks fitting the exact pattern
  already proven for commander damage.

## Implementation status

| # | Task | Status |
|---|------|--------|
| 1 | Clone Cockatrice repo into `~/projects/cockatrice-commander` | done |
| 2 | Research official Commander rules (mtgcommander.net) + design doc | done |
| 3 | Map Cockatrice architecture (zones, counters, deck model, protocol) | done |
| 4 | Commander deck construction validation (100-card singleton, color identity, banned list) | done |
| 5 | Command zone + commander tax (+2 per prior cast, rule 903.9) | done |
| 6 | Commander damage tracking + 21-damage loss warning (rule 704.5g) | done |
| 7 | Commander game defaults (40 life, 4-player multiplayer) | done |
| 8 | Rebrand fork (README/attribution) + **build verification** | **done — see Build status below** |

### Files added/changed

New files:
- `libcockatrice_card/libcockatrice/card/format/commander_rules.{h,cpp}` — color
  identity computation (regex over mana cost + rules text, reminder text stripped),
  `canBeCommander`, `isWithinColorIdentity`, `formatUsesColorIdentity` (gates the
  Commander-family format list: commander, duel, brawl, standardbrawl, oathbreaker,
  paupercommander, predh).
- `libcockatrice_models/libcockatrice/models/deck_list/commander_deck_validator.{h,cpp}` —
  `CommanderDeckValidator::validate(const DeckListModel&)` → `Result{isValid, errors}`;
  checks commander legality, 100-card total, per-card format legality, per-card color identity.
- `libcockatrice_utility/libcockatrice/utility/commander_counter_names.h` — shared
  counter-naming helpers (`tax()`, `damage()`, `isDamageCounter()`,
  `commanderNameFromDamageCounter()`, `LETHAL_COMMANDER_DAMAGE = 21`).

Modified (see `git diff` for full detail):
- `libcockatrice_utility/.../zone_names.h` — added `COMMAND = "command"`.
- `libcockatrice_network/.../server_player.cpp` — command zone setup/redirect,
  commander tax counter creation + increment in `onCardBeingMoved()`.
- `libcockatrice_network/.../server_game.cpp` — cross-player commander-damage
  counter creation in `doStartGameIfReady()`.
- `cockatrice/src/game/player/player_logic.{h,cpp}` — client-side COMMAND zone.
- `cockatrice/src/game_graphics/player/player_graphics_item.{h,cpp}` — command
  zone visual + lethal-commander-damage `QMessageBox::warning` hook.
- `cockatrice/src/game/zones/card_zone_logic.cpp` — "Command Zone" display name.
- `cockatrice/.../dlg_create_game.cpp` — 40 life / 4 players default for Commander game type.
- `cockatrice/.../deck_editor_deck_dock_widget.{h,cpp}` — live red/green deck-legality
  status label wired to `CommanderDeckValidator` (added after review caught that the
  validator existed but nothing called it).
- `libcockatrice_models/.../deck_list_model.cpp` — `isCardNodeLegalForFormat()` /
  `refreshCardFormatLegalities()` now also enforce color identity, gated by
  `CommanderRules::formatUsesColorIdentity()` (NOT by "banner card is set" alone —
  `bannerCard` can be a cosmetic cover card on non-Commander decks too; gating on
  that alone was an initial bug, caught before shipping).
- `*/CMakeLists.txt` — register new source files.
- `README.md` — fork documentation + attribution + known limitations.

New files (command zone visibility fix, see below):
- `cockatrice/src/game_graphics/zones/command_zone.{h,cpp}` — `CommandZone`, a
  `PileZone` subclass giving the commander a gold-tinted, gold-bordered,
  "CMD"-labeled pile distinct from deck/graveyard/exile.

Modified (command zone visibility fix + small UX/compliance items):
- `cockatrice/src/game_graphics/player/player_graphics_item.cpp` — instantiate
  `CommandZone` instead of a plain `PileZone` for the command zone slot.
- `cockatrice/src/game/player/player_logic.cpp` — **real bug fix**: added
  `ZoneNames::COMMAND` to `eventGameStateChanged()`'s `builtinZones` allowlist.
  Without it, every game-state refresh deleted the command zone's
  `PileZoneLogic` and silently recreated an invisible, disconnected one for
  the server's zone data — so the visible command-zone widget was
  permanently bound to a stale, always-empty object and showed 0 cards even
  once the commander had genuinely moved there. Root-caused and confirmed via
  the client's own debug log (`Event_GameStateChanged`'s `zone_list { name:
  "command" ... card_count: 1 }`) contradicting the on-screen "0" — not
  discoverable by reading `command_zone.cpp` in isolation, since that file's
  logic was already correct.
- `cockatrice/src/game/game_meta_info.h` — `GameMetaInfo::isCommanderGame()`,
  solving the previously-documented client-side Commander-detection gap
  (mirrors `Server_Game::isCommanderGame()`).
- `cockatrice/src/game/game_event_handler.cpp` — Commander-only advisory
  `QMessageBox::warning` at Cleanup phase when hand size > 7 (rule 514.1),
  using `isCommanderGame()` above. A pure warning, no auto-discard, matching
  this fork's Assisted-Mode philosophy (discarding is a real choice, unlike
  untap/draw).
- `libcockatrice_models/.../commander_deck_validator.cpp` — CONTRIBUTING.md
  translation-guideline fix: all 8 user-facing error strings were plain
  `QStringLiteral(...)` with no translation context. Added a local `tr()`
  free function (`QCoreApplication::translate("CommanderDeckValidator", ...)`,
  since this is a free function, not a `QObject`, so plain `tr()` isn't
  available) and wrapped every error string in it.

Standalone verification aid (not part of the shipped diff):
`/tmp/color_identity_algo_test.cpp` — non-Qt regex mirror of the color-identity
algorithm, compiled with plain g++ and passed, to validate the trickiest new logic
before writing the Qt-dependent version (which can't be unit-compiled without Qt).
Recreate it if useful; it does not need to ship.

### Known limitations (already documented in README)

Single designated commander only (no Partner/Background pairing). Color identity
doesn't yet pull in a double-faced card's back face. Command zone has no dedicated
context menu (graveyard/exile do). No automated stack/priority/combat engine —
out of scope per the design doc's own phasing.

## Build status — where things stand and how to resume

**Latest: Phase 4 (turn structure automation) changes rebuilt and verified clean
end-to-end** — `libcockatrice_network_server_remote` (with its new `libcockatrice_card`
link dependency), `servatrice`, and `cockatrice` all build with zero errors. Full
`ctest` suite (17/17 test executables) passes.

**Sandbox constraints that matter here:** 2 vCPUs, ~1.9 GiB RAM, **no swap**,
Amazon Linux 2023. `/tmp` is **tmpfs** (RAM-backed, ~955 MiB cap) — writing large
things there (like a Qt6 SDK) eats directly into system RAM. This combo is the
likely root cause of the OOM kill that ended the prior session mid-build.

**What's been proven so far:**
- AL2023's dnf repos don't ship Qt6, so a prebuilt Qt6 was fetched via `aqtinstall`
  (`pip3 install --user aqtinstall`), Qt 6.7.0 `linux_gcc_64`, plus the
  `qtwebsockets` and `qtmultimedia` addon modules (Cockatrice's CMake needs these
  beyond the base install; `qtbase` itself is the implicit base, not a `-m` module).
- `cmake -DCMAKE_PREFIX_PATH=<qt6 prefix> -DCMAKE_BUILD_TYPE=Release -DWITH_SERVER=ON -DWITH_CLIENT=ON -DWITH_ORACLE=OFF -DTEST=OFF ..`
  configured successfully once all Qt6 modules were present.
- Built successfully with `make -j2 <target>`, confirming **no compile errors** in
  any new/modified file, for:
  `libcockatrice_card`, `libcockatrice_utility`, `libcockatrice_models_deck_list`
  (pulls in `commander_deck_validator.cpp` + `deck_list_model.cpp` changes), plus
  their dependencies (`libcockatrice_protocol`, `libcockatrice_deck_list`,
  `libcockatrice_rng`, `libcockatrice_interfaces`).
- `libcockatrice_network` (all 5 targets: `server_remote`, `server_local`,
  `client_remote`, `client_local`, `client_abstract` — covers `server_player.cpp` /
  `server_game.cpp`, the least "copy an existing pattern" / most novel code) now
  **built successfully with zero errors/warnings**, confirmed in the follow-up
  session after fixing the OOM cause (see below).
- `cockatrice` client binary (the heaviest target — full GUI, MOC-heavy) now
  **built successfully with zero errors**, `-j1`, no OOM. Binary at
  `build/cockatrice/cockatrice`. **Task 8 is complete: this fork is fully
  compile-verified**, not just manually reviewed.
- The Qt6 install directory under `/tmp/qt6install` did **not** survive the sandbox
  restart after the OOM (tmpfs) — re-fetched via aqtinstall to a disk-backed path instead.

**Root cause of the OOM (diagnosed in the follow-up session) and fix applied:**
`/tmp` in this sandbox is tmpfs (RAM-backed, ~955 MiB cap) — installing the ~1.4 GiB
Qt6 SDK there directly consumed system RAM, on top of `make -j2` running two
`cc1plus`/MOC processes, with **no swap configured**, on a 2 vCPU / 1.9 GiB box.
Fix: Qt6 now installed to `~/qt6install` (disk-backed, not tmpfs); added a 1.5 GiB
swap file (`/swapfile`, `sudo swapon`); building with `-j1` instead of `-j2`, one
target at a time, checking `free -h` between steps.

**Fork/remote note:** `origin` is the real upstream `Cockatrice/Cockatrice` repo —
not writable by this user. Work is pushed instead to a fork at
`github.com/lukellis/Cockatrice`, branch `commander-rules` (remote name `fork`).

**Recommended approach to resume without OOMing again:**
1. Install Qt6 to a **disk-backed** path (e.g. `~/qt6install`), not `/tmp`, so it
   doesn't compete with build processes for RAM.
2. Add a swap file for headroom (no swap currently exists) — protobuf-generated
   code and Qt MOC-heavy GUI translation units are the memory-hungry outliers here.
3. Build with `-j1` rather than `-j2` — 2 parallel `cc1plus` processes on ~1.9 GiB
   with no swap is tight, especially once past small library targets into the
   network/protocol and full client build.
4. Keep building **one target/library at a time** (as the prior session was doing)
   rather than a bare `make` of everything, and check `free -h` between steps.
5. Order: finish `libcockatrice_network` → `servatrice` (server binary, no GUI) →
   `cockatrice` (client binary, heaviest due to GUI/MOC).

**Remaining checklist for task #8:**
- [x] Re-provision Qt6 (disk-backed path) + swap file
- [x] Finish `libcockatrice_network` build, confirm clean
- [x] Build `servatrice` (server), confirm clean — required `sudo dnf install -y
      pulseaudio-libs fontconfig freetype` first (Qt6's prebuilt `libQt6Multimedia`/
      `libQt6Gui` shared libs need these system runtime libs at link time; AL2023
      doesn't have them by default). Binary at `build/servatrice/servatrice`.
- [x] Build `cockatrice` (client), confirm clean — binary at `build/cockatrice/cockatrice`.

**Task 8 is done.** All new/modified code is compile-verified end to end
(libraries, server, and client), not just manually reviewed.

## Testing

The repo has a real GTest suite under `tests/` (CI runs it via
`.ci/compile.sh --test` → `ctest`), which the original implementation work did
**not** use — it only had a standalone non-Qt algorithm mirror
(`/tmp/color_identity_algo_test.cpp`, not part of the shipped diff). Closing that
gap:

- `./format.sh --cmake --branch master` run and clean (see Lint below) — not
  testing per se, but part of this repo's CI checks.
- New test suite added at `tests/commander/` (registered in `tests/CMakeLists.txt`):
  - `commander_rules_test.cpp` — pure algorithmic tests for `CommanderRules::*`
    (color identity from colors/mana-cost/rules-text, reminder-text exclusion,
    hybrid symbols, `canBeCommander`, `formatUsesColorIdentity`). No card database
    needed; constructs `CardInfo` directly via `CardInfo::newInstance()`.
  - `commander_deck_validator_test.cpp` — integration test exercising the real
    `CommanderDeckValidator::validate()` + `DeckListModel` + `CardDatabaseManager`
    pipeline (not mocked), using a dedicated fixture at `tests/commander/data/cards.xml`
    with its own `<formats>` block (100-card, singleton, banned-list, basic-land
    exception) — isolated from `tests/carddatabase/data/cards.xml` so it doesn't
    perturb that fixture's hardcoded card/set counts. Covers: valid 100-card deck,
    missing/illegal/unknown commander, text-granted commander eligibility, wrong
    card count, color identity violations (both directions), banned cards, singleton
    violations, and the basic-land exception.
- Both new test binaries build clean and **all 28 cases pass**:
  `commander_rules_test` (17/17) and `commander_deck_validator_test` (11/11),
  run directly (`build/tests/commander/commander_rules_test`,
  `.../commander_deck_validator_test`). One compile fix needed along the way:
  `CardDatabase::loadCardDatabases()` returns `void` (status is a separate
  `getLoadStatus()` call), not the `LoadStatus` the test initially assumed.
- Full suite (`cmake -DTEST=ON` + `make -j1` all test targets + `ctest --output-on-failure`
  from `build/`): **16/16 tests pass, 0 failed.** All pre-existing tests pass
  unmodified — the Commander changes to `deck_list_model.cpp` didn't need any
  existing test updated. Full list: `dummy_test`, `expression_test`,
  `clamped_arithmetic_test`, `test_age_formatting`, `password_hash_test`,
  `server_card_counter_test`, `server_counter_test`, `deck_hash_performance_test`,
  `card_zone_algorithms_test`, `carddatabase_test`, `filter_string_test`,
  `commander_rules_test`, `commander_deck_validator_test`,
  `loading_from_clipboard_test`, `reverse_card_move_test`, `parse_cipt_test`.
- **Testing is done and green.** This closes the gap identified earlier: the fork
  now has real GTest coverage for the new Commander logic, verified against both
  its own tests and the full pre-existing suite, matching this repo's actual CI
  practice (`.ci/compile.sh --test` → `ctest`) instead of the original
  standalone-non-Qt-mirror approach.

## Lint

`./format.sh --cmake --branch master` was run and applied (clang-format +
cmake-format) to all changed files — purely cosmetic line-wrap/whitespace, no
semantic changes. AL2023's dnf-provided clang-format is v15, which doesn't support
this repo's `.clang-format` `RemoveSemicolon` key (needs 16+); installed
clang-format 22 and cmake-format via `pip3 install --user clang-format cmake-format`
(lands in `~/.local/bin`, ahead of dnf's `/usr/bin/clang-format` on `PATH`) to match
what CI's lint workflow actually checks for.

**Note:** `format.sh`'s default `--branch` is `origin/master` (the real upstream
remote). Since this fork's local `master` may not exactly match `origin/master`,
running `format.sh` without `--branch master` can pick up unrelated pre-existing
files that merely differ from upstream — happened once with root `CMakeLists.txt`
(a pre-existing GCC16 workaround comment, reformatted and reverted, not part of
this fork's diff). Always pass `--branch master` explicitly in this repo to scope
to just this fork's actual changes.

## UI testing capability (new)

This sandbox can now actually run and screenshot the real `cockatrice` GUI
headlessly (Xvfb + Qt xcb platform + XTest input simulation via
`python-xlib`), not just build it. Full recipe, missing-library fixes, and
the reusable driver script (`.uitest/uitest.py`, gitignored) are documented
in `CLAUDE.md`. This immediately paid off: driving the real deck editor
(create a Commander deck, add a commander, read the live validation tooltip
off a screenshot) caught a real double-counting bug in
`CommanderDeckValidator::validate()` that every GTest case had missed,
because the tests set the banner card via the API directly without also
adding it to the main deck list — not how the real UI actually builds a
Commander deck (the Banner Card picker is populated *from* the main deck
list). Fixed and reverified visually; see the commit for detail. Prefer this
kind of live verification for any further client-visible Commander feature
work, not just unit tests.

**Full live end-to-end game verification (beyond the deck editor):** also ran
an actual local `servatrice` (config at `.uitest/servatrice_local.ini`,
gitignored — `type=none` database, `method=none` auth, room configured with
"Commander" as a game type), connected the client to it, created and started
a real 1-player Commander game, and read the exact wire protocol
(`Event_GameStateChanged`, `Event_SetCardAttr`, `Event_DrawCards`,
`Event_PriorityChanged`) straight out of the client's debug log
(`/tmp/cockatrice_gui.log`, generated automatically since the client is a
debug build — far more reliable than reading pixels off a cramped 1280×800
screenshot for this level of detail). Confirmed, byte-for-byte, all in one
session:
- The commander (Atraxa, Praetors' Voice) correctly starts in the `command`
  zone, not the deck (`zone_list { name: "command" ... card_count: 1 }`).
- A `Commander Tax: Atraxa, Praetors' Voice` counter is created at game start,
  starting at 0, with the exact expected name/color.
- Starting life is 40, matching the create-game dialog default (also
  confirmed visually: selecting the "Commander" radio button in the
  create-game dialog live-updates Players to 4 and Starting life to 40).
- Entering the Untap phase fires the auto-untap (`Event_SetCardAttr` on the
  `table` zone) and entering Draw fires auto-draw (`Event_DrawCards { number:
  0 }` — correctly handled the empty-library case gracefully, no crash).
- Every phase/turn change correctly re-broadcasts `Event_PriorityChanged`.
- All of the above is genuinely gated to Commander games — this room also had
  a "Standard" game type configured, and none of this fires for it.
- Also confirmed what was already known to be *not* done: the client's
  existing "Pass" toolbar button sends `Command_NextTurn`, not the new
  `Command_PassPriority` — there is no UI trigger for priority-passing yet,
  exactly as documented in the Phase 5 section above.

This is about as strong a confirmation as this sandbox can produce without a
second real player: every server-side Commander mechanic implemented tonight
(Phases 2–5) has now been exercised in an actual running game, not just unit
tests, with zero discrepancies found beyond the one bug already fixed above.

## Where this stands / next steps

As of this writing: design doc §3 Phases 2–3 done, Phase 4 (turn structure)
partially done (auto-untap/auto-draw), Phase 5 (Priority & Stack System)
partially done in simplified form (real priority-passing, no stack
resolution — see "Phase 5" section above), Phase 6 (Mana System) scoped but
not implemented (auto-empty-pool slice documented, awaiting a build/no-build
decision — see "Phase 6" section above), Phase 7 (Card Ability System)
Increments 1–2 (evergreen keyword recognition/display, and simple fixed-color
mana ability recognition + one-click tap-and-add — see "Phase 7" section
above), Phase 9 (State-Based Actions) partially done (life/empty-library/poison
advisory warnings — see "Phase 9" section above). Everything compile- and
test-verified: `servatrice` + `cockatrice` build clean, 19 test executables
pass via `ctest`, and the Phase 9 warnings, the Phase 7 keyword display, and
the Phase 7 mana-ability action were all additionally confirmed live
(screenshot + debug log, or screenshot against real card data) against a real
running client, not just compiled. All pushed to `fork/commander-rules`.

Work initially stopped before Phase 5 pending explicit user sign-off, since it
was the first phase needing actual `.proto` changes (breaking the
zero-protocol-changes streak of Phases 2–4). The user then authorized
continuing, asked for research into existing codified rules engines to borrow
from, and to document simplifications/assumptions — which is what the "Phase
5" section above records (XMage's `GameImpl.playPriority()` as the borrowed
architectural reference, with the stack-resolution part explicitly scoped out
as needing a card-rules engine this fork doesn't have).

**Deliberately not attempted, and why:**
- **Real stack resolution / card ability execution** (design doc Phases 6–8:
  mana system, card ability parsing, combat). These need an actual card-rules
  engine (parse rules text, execute effects, targeting, replacement effects)
  — the same conclusion Forge/XMage's own scale of effort confirms (community
  projects, years of work). Not attempted at any level here; would need a
  real scoping/design conversation, not a unilateral implementation.
**Done since the above was written (this session, continued):**
- **Client-side UI for priority-passing** — done. A new "Pass Priority"
  `PhaseButton` (gold double-chevron icon, `cockatrice/resources/phases/pass_priority.svg`,
  registered in `cockatrice.qrc`) was added to `PhasesToolbar`, positioned below
  the existing next-turn button. Gated on `GameMetaInfo::isCommanderGame()` via
  a new `PhasesToolbar::setCommanderGame(bool)` (called once from
  `TabGame::createPlayAreaWidget()`, which adjusts `buttonCount`/layout to
  reserve/release its vertical slot rather than always showing an empty gap in
  non-Commander games). Clicking it sends the existing `Command_PassPriority`
  (unchanged from Phase 5). A new `GameEventHandler::eventPriorityChanged()` /
  `priorityChanged(int)` signal wires the previously-unconsumed
  `Event_PriorityChanged` into `TabGame::setPriorityPlayer()`, which calls
  `PhasesToolbar::setPriorityHolder(bool)` to pulse the button (reusing
  `PhaseButton`'s existing active-phase highlight animation, no new visual
  code needed) whenever the local player holds priority. Verified live: local
  servatrice + screenshot showing the button rendered and its (220,220,220)
  active-highlight margin present, plus the client debug log showing the full
  round trip (`Command_PassPriority` out → `Event_PriorityChanged` in →
  phase auto-advances once the lone player in a 1-player test game passes
  priority to themselves, exhausting the round — confirming this exercises
  the real `Server_Game::advancePriority()` logic from Phase 5, not just the
  new UI in isolation).
- **Client-side Commander-game detection** — `GameMetaInfo::isCommanderGame()`
  (`cockatrice/src/game/game_meta_info.h`), mirroring the server's own
  `isCommanderGame()`. Unblocks any future client-only Commander UI.
- **Discard-to-hand-size at end step** — implemented per the sketch below,
  using `isCommanderGame()` above. Advisory `QMessageBox::warning` only, no
  auto-discard (discarding is a real choice, unlike untap/draw).
- **Commander visible on the board** — direct user-reported gap: the
  commander card had no distinct, visible location in the play area (it lived
  in the same generic pile-zone slot as deck/graveyard/exile, no different in
  appearance). Added `CommandZone` (gold border/tint, "CMD" label). Along the
  way, found and fixed a real bug where the visible command-zone widget was
  permanently stuck at "0 cards" regardless of the actual game state — see
  the "Files added/changed" entry above for `player_logic.cpp`. Verified via
  live servatrice + screenshot + client debug log, not just compilation.
- **CONTRIBUTING.md translation-guideline gap** — `commander_deck_validator.cpp`'s
  error strings now use `tr()`; see "Files added/changed" above.
- **Casting the commander from the command zone, live-confirmed end to end** —
  the user directly questioned whether this even worked, after an earlier
  session turn had reported being unable to verify it (a UI-automation
  coordinate problem, not a real bug). Set up a genuine 2-player game across
  two client instances plus a simpler solo fallback and confirmed via the
  client debug log: dragging the commander out sends `Command_MoveCard` with
  `start_zone: "command"`, and the `Commander Tax` counter increments 0→1 in
  the same event. Commander Tax already started at 0 in code
  (`Server_Counter(..., 0)` in `Server_Player::setupZones()`) — no fix
  needed. Also confirmed live that moving *any* card (not just the
  commander) onto the Stack zone correctly starts a fresh priority round at
  the mover (see "Correction: priority model" above) — the maroon
  battlefield-adjacent strip is genuinely the Stack zone, drag-and-drop
  works fine via this sandbox's synthetic input once coordinates are right.
- **Commander portrait/untapped display, reordered above library/graveyard**
  — two follow-up display changes requested alongside the above.
  `CommandZone` now resets its item transform to identity (overriding
  `PileZone`'s persistent 90° rotation) and has a fully custom `paint()`
  that draws the card image, count badge, border, and "CMD" label with no
  rotation compensation, so the commander renders right-side-up/readable
  instead of sideways like the other compact piles. `player_graphics_item.cpp`
  now creates `CommandZone` first in the pile stack (ahead of
  deck/graveyard/exile) with a taller reserved step (full card height, since
  it's no longer rotated to the narrower compact-pile footprint). Verified
  live via screenshot (upright text, correct stacking order, no overlap) and
  by re-running the drag-to-cast test from the new position (move + tax
  increment both still fire correctly).

**Reasonable next increments, roughly in order of size/risk:**
1. ~~Client-side UI for priority-passing~~ — **done**, see above.
2. ~~Anything from Phase 9 (State-Based Actions) that fits the existing
   counter/warning pattern~~ — **done** (life ≤ 0, empty-library draw, ≥10
   poison, all advisory warnings). See "Phase 9" section above. Remaining
   Phase 9 SBAs (legend rule, 0-toughness, illegal auras) need card-state
   modeling this fork doesn't have — not a reasonable next increment at this
   scope.
3. ~~A broader CONTRIBUTING.md compliance pass~~ — **done.** Audited the
   fork's full diff vs upstream master (a dedicated agent pass, since
   `format.sh` already covers everything clang-format enforces). Found and
   fixed: 6 UpperCamelCase local constants renamed to the codebase's
   ALL_CAPS convention (`CleanupPhase`/`MaxHandSize`/`CommandZoneAccent`/
   `UntapPhase`/`DrawPhase`/`CommanderPhaseCount`), and 4 more untranslated
   user-facing strings in `commander_deck_validator.cpp` (missed by the
   earlier partial `tr()` fix, which only covered 3 of 7 error paths).
   Header guards, `nullptr` usage, single-declaration-per-line, Doxygen
   comment style, and memory-management guidance were all already clean.
   Pure renames — full 17-test GTest suite still passes.
4. ~~Phase 6 (mana system) narrow slice~~ — **done (2026-07-16).** Auto-empty
   mana pools at every phase/step end (rule 500.4), for every player. See
   "Phase 6" section above. Cost validation/auto-tap/payment remain out of
   scope, needing Phase 7's card-ability engine.
5. Phase 7 Increment 1 (evergreen keyword recognition/display) — **done**,
   see "Phase 7" section above. Increments 2–4 (mana abilities, triggered
   abilities, community ability data) and Phase 8 (combat) still need a real
   card-rules engine and real design discussion before implementation
   starts; not a reasonable unilateral next step at any scope.

## Design & Implementation Review — 2026-07-16

A cross-cutting review of the fork as it stands (after Phase 7 Increment 2 and the
basic-land/multi-select mana follow-ups). Records issues, an architecture decision
on whether to pursue a bigger rules engine, a token-efficiency plan for iterating
in this sandbox, and an EC2 hosting runbook — so future sessions inherit the
conclusions instead of re-deriving them.

### Verdict

The fork is in good shape for its intended scope. The scoped-down "advisory,
non-blocking, reuse-existing-mechanisms" approach is sound and well-tested. The
conservative text parsers, the priority "stop at -1" model, and the SBA/lethal
warnings are all correct for what they claim to do. No correctness bug was found in
the shipped Commander logic in scope. The recommendations below are about
maintainability, iteration cost, and enabling real multiplayer play-testing — not
about fixing broken behavior.

### Issues found (prioritized)

1. **Disk pressure (operational, act first).** The 8 GiB root fs runs near full
   (observed 96% used, ~348 MiB free). UI-test PNGs, a `-DTEST=ON` build tree, and
   Qt object files can exhaust it and hard-fail a build mid-link — a plausible way
   for a session to get wedged (distinct from the historical *RAM* OOM; swap and RAM
   are currently healthy). Mitigations: prune stale screenshots/build artifacts,
   keep only one build tree hot at a time, and check `df -h /` before any build or
   UI-test batch.
2. **No `ccache` (build efficiency).** Every rebuild pays full `cc1plus`/MOC cost on
   2 vCPUs. The design doc itself recommends ccache (§5.1). This is the biggest lever
   on both wall-clock build time and the *token* cost of iterating (fewer "still
   building" round-trips). Set it up (`sudo dnf install -y ccache` or pip is n/a;
   configure `CMAKE_CXX_COMPILER_LAUNCHER=ccache`) with a cache dir on the disk-backed
   home, not tmpfs.
3. **Commander logic is interleaved into core hot-path files (organizational).**
   `if (isCommanderGame())` guards and Commander state live directly inside
   `Server_Game::setActivePhase()`, `Server_Player::onCardBeingMoved()`,
   `Server_Player::setupZones()`, and priority fields on `Server_Game`. It is
   *correct*, but (a) it scatters the fork's behavior across the files most likely to
   conflict on an upstream merge, and (b) it is the direct reason the test suite
   **can't** write true end-to-end tests of the phase automation —
   `Server_Game::addPlayer()` needs a live `Server_AbstractUserInterface`, so the
   automation path is only tested via the extracted pure helper `phaseAutomationFor()`.
   See the architecture decision below for the right-sized remedy.
4. **`isCommanderGame()` is uncached on hot paths (micro).** It re-scans the room's
   game-type `QStringList` with a substring match on every phase change and every
   card move. Trivially cacheable (compute once at game start / first call). Low
   severity, easy win.
5. **Pre-existing upstream UB: `~Server_Game()` calls `deleteLater()` on itself.**
   Not this fork's code, but it bites the test suite (worked around by
   heap-allocating and never freeing `FakeServer`/`Server_Room`/`Server_Game`). Worth
   filing upstream against Cockatrice with the repro already documented in the Phase 4
   section, so the workaround doesn't silently rot.

### Architecture decision: reorganize, don't re-architect

**Do not build the design doc's full `libcockatrice_rules/` + `RulesEngine`/
`GameState` engine (§3 Phase 1, §4).** That engine only pays for itself once there is
real **card-effect execution** — genuine stack resolution, mana payment, combat —
i.e. the 8–12-week Phase 7 card-ability engine that is deliberately out of scope.
Everything actually built here is advisory counters + warnings + display; wrapping
that in a `RulesEngine`/`GameState` abstraction is premature abstraction that adds a
library and merge surface without adding capability. A large refactor whose payoff is
a feature set we've explicitly excluded is the wrong trade.

**Instead, a lightweight *organizational* extraction is the right-sized move** for
issue #3 above: pull the scattered `isCommanderGame()` hooks into one cohesive seam —
e.g. a `CommanderController` (or a `commander/` module of free functions) that the
core calls at named points: `onPhaseChanged(phase, turn, players)`,
`onCardMoved(card, fromZone, toZone)`, `onZonesSetup(player)`. This is a refactor of
*organization*, not *capability*:

- Closes the end-to-end testing gap — the controller takes plain data, so the real
  automation path becomes unit-testable without a live UI.
- One place to read all Commander behavior instead of six `grep isCommanderGame` hits.
- Smaller, more legible upstream diff.

If real enforcement (the full engine) is ever pursued, that is a separate, explicit,
multi-month decision — and this seam is a reasonable stepping stone toward it.

**Decision (2026-07-16): proceeding with the seam, and going further than "just
reorganize."** The user directed building the `libcockatrice_rules/` + `RulesEngine`
foundation now (so all future Commander work has one home), *and* making the fork
**wholly Commander-only** — every `isCommanderGame()` gate removed, engine always
active, non-Commander game types/format UI stripped from the client. This is
foundation-first: the library, the always-on seam, and migration of today's logic land
first; real rule enforcement grows inside the engine incrementally later (still gated
behind an explicit design pass, per the Assisted-Mode philosophy). Sequenced as
Increment 0 (build setup: ccache + disk — **done**), Increment 1 (server `RulesEngine`
foundation — **done**, see below), Increment 2 (Commander-only client + module
migration — next), Increment 3+ (enforcement, future). Each increment
builds/tests/pushes independently.

#### Increment 1 — server RulesEngine foundation (done, 2026-07-16)

- **New library `libcockatrice_rules/`** (`libcockatrice/rules/rules_engine.{h,cpp}`),
  linking only `libcockatrice_utility` + Qt Core, linked into
  `libcockatrice_network_server_remote`. Registered in the top-level CMake.
- **`Rules::RulesEngine`** — the pure decision core: `phaseAutomationFor()` and
  `nextPriorityPlayer()` migrated verbatim from `Server_Game`, plus the priority-round
  state machine (`startPriorityRound()` / `passPriority()` returning a
  `PriorityPassResult{changed, holder}` / `clearPriority()` / `priorityHolder()`). No
  I/O — the server performs side effects from the engine's decisions. The
  `CommanderPhaseAutomation` enum became `Rules::PhaseAutomation`; the dead
  `COMMANDER_PHASE_COUNT` constant was removed.
- **`Server_Game` now holds a `RulesEngine`** and delegates: `setActivePhase()`,
  `advancePriority()`, `resetPriorityTo()`, `broadcastPriorityChange()` all route
  through it and just broadcast `Event_PriorityChanged` with the engine's result.
- **All server-side `isCommanderGame()` gates removed** — the engine is always active.
  `Server_Game::isCommanderGame()` deleted; `Server_Player` de-gated at all three sites
  (poison-counter creation, command-zone routing of the commander, `cmdPassPriority`).
  Command-zone/tax/counter *decisions* still live inline in `server_player.cpp` for now
  (a clean follow-up can migrate them into the engine).
- **Tests:** new lightweight `tests/rules/rules_engine_test.cpp` (21 cases, links only
  `libcockatrice_rules` — no server/DB), covering phase automation, `nextPriorityPlayer`,
  and the priority-round state machine (start → pass → next/exhaust/no-op/clear,
  including the solo auto-pass-loop case). The old
  `commander_turn_structure_test.cpp` was trimmed to just its server-integration cases
  (untap/draw mechanisms + `cmdPassPriority` gating); its pure-logic and now-removed
  `isCommanderGame` cases are superseded by the new suite.
- **Verified:** `servatrice` builds+links clean with the new lib; full `ctest`
  **20/20 pass** (up from 19 executables); `format.sh --cmake --branch master` clean.
  **Live** (local servatrice + client + Xvfb, debug-log/screenshot): a real solo
  Commander game starts the commander (Atraxa) in the command zone (count 1), 40 life,
  auto-draws on entering the Draw step (deck 2→1, card to hand), and resets+broadcasts
  priority on phase change ("testuser has priority" logged) — identical behavior to
  before, now routed through `RulesEngine`.
- **Client untouched** this increment (still builds/runs as-is); one dangling doc
  comment referencing `Server_Game::isCommanderGame()` remains in the client's
  `game_meta_info.h` and is addressed in Increment 2.

#### Increment 2 — Commander-only client + module migration (done, 2026-07-16)

Followed [`doc/design-docs/rules-engine-increment2-plan.md`](doc/design-docs/rules-engine-increment2-plan.md)
in a later session, continuing from Increment 1's resume point.

- **Step 1, de-gating (always-on client behavior)**: every client-side
  `isCommanderGame()` gate removed — `player_graphics_item.cpp` (empty-library-draw,
  life ≤ 0, poison SBA warnings), `game_event_handler.cpp` (Cleanup discard-to-hand
  warning), the Pass Priority button (`PhasesToolbar::setCommanderGame()` deleted
  entirely, button now always constructed visible and its vertical slot always
  reserved in `buttonCount`), and `GameMetaInfo::isCommanderGame()` itself deleted
  (with its now-dangling doc-comment references in `player_logic.h` and
  `message_log_widget.h` cleaned up).
- **Step 2, UI strip**: `dlg_create_game.cpp`'s game-type radio-button selector
  (`gameTypeCheckBoxes`, `QRadioButton` includes, the whole "Game type" group box)
  removed entirely — the dialog now always submits every Commander-family game type
  id from the room's config (via `CommanderRules::gameTypeLabelIsCommander()`, no
  UI choice) and defaults/resets to 40 life / 4 players unconditionally. The deck
  editor's format `QComboBox`/`initializeFormats()` removed the same way;
  `DeckListModel::refreshCardFormatLegalities()` now hardcodes the format string to
  `"commander"` (previously read `deckList->getGameFormat()`, which would have been
  silently empty with no picker to set it — hardcoding was necessary, not just
  dropping the gate, since `isCardQuantityLegalForFormat()` trivially returns `true`
  for an empty format string) and always computes color identity when a banner card
  is set; `updateCommanderValidation()` in the deck editor dropped its
  `formatUsesColorIdentity()` gate and always runs.
- **Step 3, module migration — narrower than originally planned, and why**:
  `commander_rules.{h,cpp}` (card → rules) and `commander_counter_names.h`
  (utility → rules) both moved into `libcockatrice_rules` as planned — both are
  unconditionally-built libraries (`libcockatrice_card`/`libcockatrice_rules`
  always compile in every `WITH_SERVER`/`WITH_CLIENT`/`WITH_ORACLE` combination,
  confirmed against `.ci/compile.sh`'s server-only CI path), so no cycle or
  conditional-availability problem. **`commander_deck_validator.{h,cpp}` was
  deliberately left in `libcockatrice_models_deck_list`**, diverging from the plan
  doc's step 3 list: it needs `DeckListModel`, and `libcockatrice_models` is only
  `add_subdirectory`'d under `if(WITH_ORACLE OR WITH_CLIENT)` — moving the
  validator into the always-built `libcockatrice_rules` would have made a
  server-only build (`MAKE_SERVER=1 MAKE_NO_CLIENT=1`, a real supported CI
  configuration) fail to configure, since `libcockatrice_rules` would then
  unconditionally require a target that doesn't exist in that configuration. This
  is exactly the kind of obstacle the plan doc's acyclicity caveat anticipated,
  just from a different angle (build-configuration availability, not a literal
  `target_link_libraries` cycle) — same escape hatch invoked: leave the
  tightly-coupled-to-`DeckListModel` piece where it already was.
  `libcockatrice_models_deck_list` gained `libcockatrice_rules` as a new `PUBLIC`
  dependency instead (one-directional: rules ← models_deck_list, confirmed acyclic
  by a clean `cmake` reconfigure with zero errors). `cockatrice`'s own
  `CMakeLists.txt` gained a direct `libcockatrice_rules` link (needed for
  `commander_counter_names.h` in `player_graphics_item.cpp` and
  `commander_rules.h` in four deck-editor/create-game/EDHRec client files).
- **A real staleness trap, caught and fixed**: the first `make -j1 cockatrice` after
  the Step 3 file moves + `CMakeLists.txt` edits reported **zero errors** — but this
  was a false pass. `CMakeCache.txt`'s mtime proved `cmake` never actually
  reconfigured during that build (a plain `make <target>` invocation, it turns out,
  doesn't reliably re-trigger `cmake_check_build_system` here the way a bare `make`
  does), so the link succeeded only because the *old* `liblibcockatrice_card.a` /
  `liblibcockatrice_utility.a` archives — built days earlier, before the file
  moves — still physically contained the moved-away `.o` members (`ar` archives
  aren't stripped of members that fall off a `CMakeLists.txt` source list; only a
  full archive rebuild does that). The real, moved `commander_rules.cpp` had
  **not** actually been compiled into the new `libcockatrice_rules` at all yet.
  Caught by explicitly checking `build/libcockatrice_rules/` for the expected new
  `.o` files and finding them absent. Fixed by forcing `cmake .` (confirmed a clean
  reconfigure with no dependency-graph errors — validating the acyclicity design
  above), deleting the stale `.a` archives for the three affected libraries to force
  a genuine relink, and rebuilding `libcockatrice_utility` → `libcockatrice_card` →
  `libcockatrice_rules` → `libcockatrice_models_deck_list` → `cockatrice` →
  `servatrice` in dependency order, confirming `commander_rules.cpp.o` now actually
  appears under `build/libcockatrice_rules/` and `liblibcockatrice_card.a` no
  longer contains it. **Lesson for future sessions doing source-file moves across
  `CMakeLists.txt` targets in this sandbox: don't trust a clean `make <target>`
  exit code alone after changing which files belong to which library — verify the
  actual object files landed in the new target's build directory, or force an
  explicit `cmake .` first.**
- **Tests**: `commander_rules_test` relinked against `libcockatrice_rules` instead
  of `libcockatrice_card` (its `CMakeLists.txt` target and the test file's
  `#include` both updated); `commander_deck_validator_test` needed no `CMakeLists.txt`
  change (already links `libcockatrice_models`, which now transitively pulls in
  `libcockatrice_rules` via `libcockatrice_models_deck_list`'s new dependency).
  Full suite: **20/20 pass**, zero regressions. `format.sh --cmake --branch master`
  run clean (cosmetic include-reordering only); full rebuild + retest after
  formatting also 20/20.
- **Verified live** (local servatrice + Xvfb + real client, screenshot + debug-log
  cross-reference), covering every item the plan doc's Step 4 checklist called for:
  - Create-game dialog: no game-type selector at all; "Clear" confirms the true
    defaults are exactly 40 life / 4 players (a "Remember settings"-enabled test
    client showed a remembered `Players: 1` first, which is correct — remembered
    settings still override built-in defaults by design, not a bug).
  - Deck editor: no Format combo box/label; the Commander-legality red/green label
    is unconditionally active (previously invisible without a format selected) and
    correctly flagged a 3-card test deck ("Commander decks must contain exactly 100
    cards including the commander (found 3)"), proving the hardcoded `"commander"`
    format string in `refreshCardFormatLegalities()` resolves real singleton/banned
    rules from the card database.
  - Priority button: always visible with no gate; clicking it in a solo game sent
    `Command_PassPriority` and got back `Event_PriorityChanged { priority_player_id:
    -1 }` plus the "Everyone has passed. No one has priority." log line — the
    corrected Phase 5 model, now ungated, still doesn't loop.
  - Command zone / commander tax: dragging Atraxa from the command zone to the
    battlefield still incremented `Commander Tax: Atraxa, Praetors' Voice` 0→1,
    confirming `Server_Player`'s command-zone/tax logic (already de-gated in
    Increment 1) is unaffected by the client-side changes.
  - Phase 7 Increment 1 (keyword display) unaffected: Atraxa's card-info panel
    still showed a "Keywords: Deathtouch, Flying, Lifelink, Vigilance" row (this
    code path wasn't touched by Increment 2 at all — confirmed as a
    no-regression check, not a new feature).
  - Phase 9 SBA warning still fires ungated: manually decrementing life to 0
    produced "testuser's life total has reached 0 and they have lost the game
    (rule 104.3a)."

**Status: implemented, tested, and live-verified.** Pushed to `fork/commander-rules`.

#### ▶ Resume point (as of 2026-07-16)

Increments 0–2 of the rules-engine reorganization are done: `libcockatrice_rules`
exists, holds `RulesEngine` + `CommanderRules` + `CommanderCounterNames`, every
server- and client-side `isCommanderGame()` gate is gone (engine always active),
and the client no longer offers a non-Commander game-type or deck-format choice.
`CommanderDeckValidator` stayed in `libcockatrice_models_deck_list` by deliberate,
documented exception (see Increment 2's Step 3 writeup above) — a future session
revisiting library boundaries should treat that as a scoped decision, not an
oversight. **Increment 3+ (real rule enforcement growing inside `RulesEngine`)
is future work, not yet scoped** — the plan doc's own "Increment 3+" section
sketches the direction (a resolvable stack-object model, mana `canPay()`/`pay()`,
then combat) but explicitly defers it pending its own design pass, per this fork's
standing rule that Phases 6–8 need explicit sign-off before implementation.

**Update (2026-07-16, later session):** the user picked the Phase 6 mana auto-empty
slice as the next increment (from a choice of three offered — the others were
migrating command-zone/tax logic into `RulesEngine`, or adding more
`.uitest/scenario.py` scenarios). Implemented, tested, and live-verified — see the
"Phase 6" section above for full detail. `Rules::RulesEngine::manaCounterNames()` +
`Server_Player::emptyManaPool()` now zero every player's w/u/b/r/g/x counters on every
phase/step transition (rule 500.4). This is still just the counter-zeroing slice —
cost validation/`canPay()`/auto-tap remain out of scope pending Phase 7's card-ability
engine, unchanged from the original scoping decision.

### Token-efficiency plan for build & test iteration

The dominant token cost in the current loop is **reading screenshots** — each `Read`
of a 1280×800 PNG is a large image-token hit. Plan, highest-value first:

1. **Log-assertion-first testing.** The debug log is already established as ground
   truth (it caught the command-zone card-count bug that pixels only hinted at). Lean
   into it: verify wire/game state by `grep`-ing `/tmp/cockatrice_gui.log` for the
   expected protobuf lines (cheap text) and reserve screenshot+`Read` for genuinely
   *visual* properties (layout, color, rotation). Do not `Read` a PNG to confirm
   something the log states exactly.
2. **A one-shot scenario runner** (`.uitest/scenario.py <name>`) that encapsulates the
   currently-manual dance (start Xvfb if needed → launch servatrice+client → wait →
   drive a scripted input sequence → assert-on-log → print PASS/FAIL as text →
   teardown). Turns a ~15-round-trip manual sequence into one Bash call with text
   output and near-zero image tokens.
3. **ccache** (issue #2) — fewer/faster rebuilds means fewer polling round-trips.
4. **Keep the `-DTEST=ON` and client build trees separate** so running the GTest suite
   never forces a client relink and vice-versa.

**Status (2026-07-16, follow-up session): item 2 (scenario runner) built and
live-verified; item 3 (ccache) was already done as part of Increment 0 (see
above); items 1 and 4 still apply as ongoing practice, not one-time setup.**

`.uitest/scenario.py` (gitignored, alongside `uitest.py`) now exists:
`setup` (idempotent Xvfb/servatrice/client bring-up), `teardown` [`--all`
to also kill Xvfb], `list`, and `run <name> [<name> ...]` which drives a
named scenario function and asserts against `/tmp/cockatrice_gui.log` /
`/tmp/servatrice.log` via regex polling (`Ctx.assert_log`), printing a plain
PASS/FAIL instead of requiring a screenshot `Read` to confirm state. Scenario
functions get a small `Ctx` wrapping `uitest.py`'s input primitives
(`click`/`move`/`drag`/`key`/`type_text`/`shot`) plus the assertion helper.
Also added `uitest.py key_combo(modifier, key)` (e.g. `ctrl+a` to select-all
in a text field before retyping it), which didn't exist before — the input
primitives only covered single keysyms.

One scenario shipped as a proof this actually works end-to-end, not just
compiles: `connect` launches a fresh client, drives the Connect-to-Server
dialog (explicitly filling New Host name/host/port and player name, rather
than depending on a "Known Hosts" entry that only exists if a prior manual
session saved one — found and fixed after the first run hit a "You need to
name your new connection profile" blocking dialog), clicks Connect, and
asserts `Event_ServerIdentification` appears in the client log. Two real bugs
in the new tool were caught and fixed by actually running it, not just
reading it back: `key_combo`'s modifier-name mapping (`"ctrl".capitalize()`
produces the keysym name `"Ctrl_L"`, which doesn't exist — the real X11
keysym is `"Control_L"` — sending keycode 0 crashed python-xlib's own error
handler) and a stray no-op `ctx.assert_log` statement left in from drafting.
Verified live: a full cold-client run reached the room-joined "Play" home
screen with `Event_ServerIdentification` correctly captured via log grep,
confirmed with one final screenshot (not needed for the assertion itself,
just to double check post-hoc).

Not built yet: more scenarios beyond `connect` (e.g. a full solo-game-start
scenario asserting command zone/tax counter/life-40 the way the manual
process in this file's "Full live end-to-end game verification" section
already does by hand) — left for whenever that specific verification is
next needed, following this same pattern.

### EC2 hosting runbook (play-test from a local client)

Facts for this box: public IPv4 is **ephemeral** (changes on stop/start — observed
`18.144.25.26`, us-west-1), private `172.31.3.100`. `servatrice` binds
**0.0.0.0:4747** (TCP) and **:4748** (websocket). `.uitest/servatrice_local.ini`
(`type=none` DB, `method=none` auth, registration off) works as-is for casual play —
no MySQL needed.

1. **Open the instance's AWS Security Group**: inbound **TCP 4747** (and 4748 for
   websocket/browser clients) from your/your friends' home IPs. Done in the AWS
   console/CLI, not on the box. Prefer scoping to known IPs (see security note).
2. **Stable address (optional):** allocate an **Elastic IP** if you want the address
   to survive a stop/start; otherwise re-check the public IPv4 each session.
3. **Run a persistent `servatrice`** under `tmux`/`systemd`/`nohup` so it outlives the
   SSH/agent session, e.g. `nohup ./build/servatrice/servatrice --config
   .uitest/servatrice_local.ini --log-to-console > /tmp/servatrice.log 2>&1 & disown`.
4. **Local client:** Connect → New Host → `<public-ip> : 4747`, any username, no
   password.
5. **Play-test caveats:**
   - **Friends must run *this fork's* client build, not stock Cockatrice**, to see any
     client-side Commander features (command-zone UI, keyword row, one-click mana tap,
     priority button, SBA/lethal warnings). The server-side bits (commander starts in
     command zone, tax counter, auto-untap/draw, priority events) fire regardless of
     client; the *UI* lives in the fork.
   - **Card database must match.** The 12-card `.uitest/sample_cards.xml` is all this
     box has. For real decks, each client needs a real MTGJSON-derived `cards.xml`;
     the server does not strictly need the full DB for play, but clients do. Disk
     pressure (issue #1) makes hosting a full card DB on the box impractical.
   - **Security:** `method=none` means anyone reaching the port can join. Fine behind a
     locked-down Security Group; never pair it with `0.0.0.0/0`.

### Deliberate non-fix: priority passing ignores `turnOrderReversed`

`Server_Game::nextPriorityPlayer()` always steps in ascending player-id order and does
**not** consult the `turnOrderReversed` flag, whereas `Server_Game::nextTurn()` does.
So in a game where "Reverse Turn" has been used, priority would pass in the opposite
direction from the turn. **This is intentionally left unfixed:** turn reversal is a
Cockatrice table convenience, not a real MTG mechanic, so the non-reversed case (which
is correct) is sufficient for Commander play. Documented here so a future session
doesn't mistake it for an oversight — it is a scoped decision, not a bug to silently
"fix." (The header comment on `nextPriorityPlayer()` already says "ascending-id turn
order," making the assumption explicit at the source.)
