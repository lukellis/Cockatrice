# Phase 4: Turn Structure Automation

**Status: automation done; as of 2026-07-21, phase-order and casting/attack-timing are
also enforced (not just advisory) — see the new "Turn-structure and timing
enforcement" section below.**

Design doc §3 Phase 4. This was the first genuinely *automated* gameplay behavior in
the fork — everything before it was either passive counters (manually incremented,
like life) or client-side advisory warnings.

## What's implemented

`Server_Game::setActivePhase()` triggers, on every phase/step transition:

- **Untap step**: untaps everything the active player controls, reusing the same
  `Server_AbstractPlayer::setCardAttrHelper()` call the manual "untap all" action uses
  — including its existing respect for `AttrDoesntUntap`-flagged permanents.
- **Draw step**: draws 1 card for the active player, **except** the very first draw
  step of a strict two-player game's starting player (rule 103.8a). Verified against
  the actual rule text rather than the design doc's own phrasing — rule 103.8c says
  multiplayer games never skip the first draw step, only 2-player and Two-Headed Giant
  do. Since this fork's default is 4-player free-for-all, the skip in practice only
  matters for Duel Commander.

The decision of *what* to do is `Rules::RulesEngine::phaseAutomationFor(phase,
turnNumber, playerCount)` — a pure static function, unit-testable without a fully
constructed game (see the Phase 1 doc). Phase index 0 (untap) / 2 (draw) is coupled to
the client's phase ordering (`cockatrice/src/game/phase.cpp`) — there's no shared
server/client phase enum, a pre-existing architectural gap this fork works around
rather than fixes.

## Turn-structure and timing enforcement (2026-07-21)

By explicit user direction (see `CLAUDE.md`'s Design Principles — the same lifted
"advisory/manual-only" restriction Phase 8 Stages A–D already exercised for combat),
this fork now enforces real turn structure instead of only automating untap/draw
inside it. Three previously-"deliberately not implemented" gaps are closed:

- **Phase-order enforcement**: `Command_SetActivePhase` used to accept a jump to any
  phase from any phase. `Server_Player::cmdSetActivePhase()` now additionally requires
  `Rules::RulesEngine::canAdvanceToPhase(currentPhase, requestedPhase)` for non-judges
  — true only for exactly one step forward (`requestedPhase == currentPhase + 1`, rule
  500.1). Reaching phase 0 again is a new turn, not a phase advance, and is never valid
  through this command — see the Next Turn gate below. Every existing way to change
  phase (the toolbar's per-phase buttons, the Phases menu, and the pre-existing "Next
  Phase"/"Next Phase Action" actions — `Player/aNextPhase` default `Ctrl+Space`/`Tab`,
  `Player/aNextPhaseAction` default `Shift+Tab`, already shipped in vanilla Cockatrice
  and unchanged by this work) funnels through the same command, so this one server-side
  check covers all of them uniformly. An illegal jump is rejected silently
  (`RespContextError`), matching this fork's existing precedent for an illegal
  blocker/attack-target declare — no new client-side greyed-out-button UI.
- **Next Turn gate**: `Command_NextTurn` previously had **no active-player check at
  all** — any player could end any player's turn from any phase. `cmdNextTurn()` now
  requires (non-judges) both `game->getActivePlayer() == playerId` and
  `Rules::RulesEngine::canEndTurn(activePhase)` (true only on `CLEANUP_PHASE`, rule
  500.3/514) — you must actually step through to your own End/Cleanup step before
  ending your turn. The pre-existing "Next Turn" action (`Player/aNextTurn`, default
  `Ctrl+Return`) and "Next Phase Action"'s own wrap-to-phase-0 case already send this
  same command, so both benefit automatically. **UX wrinkle worth knowing**: plain
  "Next Phase" (`Player/aNextPhase`, `Ctrl+Space`/`Tab`) does *not* make this
  distinction — `TabGame::actNextPhase()` always sends `Command_SetActivePhase`, even
  when wrapping past Cleanup back to phase 0, which the new enforcement now always
  rejects (phase 0 is never a valid `canAdvanceToPhase` target — see above). In
  practice this means Tab stops doing anything once you reach Cleanup; "Next Phase
  Action" (Shift+Tab) or "Next Turn" (Ctrl+Return) are what actually end the turn.
  Not a bug in the new enforcement — vanilla `actNextPhase()`'s own wrap-around was
  already a bare `phaseChanged(0)` with no turn-advance distinction — just a real
  consequence worth knowing rather than a silent trap.
- **Sorcery-speed casting timing**: closes the gap `phase6-mana.md` left open (only
  mana *cost* was ever gated, never *timing*). New client-side gate
  `PlayerActions::gateCardTimingForHandPlay()` (`cockatrice/src/game/player/player_actions.{h,cpp}`),
  wired into the same three "a card left hand" call sites `gateManaCostForHandPlay()`
  already uses (`playCard()`, `TableZone::handleDropEventByGrid()`,
  `StackZone::handleDropEvent()`), run *before* the mana gate. No-op for
  `CardInfo::getMainCardType() == "Instant"` (flash isn't modeled — same conservative
  "don't guess" fallback as every other type-based check in this fork); otherwise
  requires `Rules::RulesEngine::canCastSorcerySpeed(currentPhase, isActivePlayer,
  holdsPriority)` — one of your own main phases, active player, holding priority (rule
  505.5a). `holdsPriority` stands in for "the stack is empty and nothing is pending a
  response" the same simplified way it already stands in for that elsewhere in this
  fork (Phase 5) — not a literal stack-empty check, since there's no general spell
  stack to inspect. This deliberately covers **lands too** (rule 305.1 is the same
  sorcery-speed timing restriction, even though a land drop stays free of mana-cost
  gating) — but is client-side only, unlike the two server-side gates above, because
  (same reason Stage C's keyword work gave) the server has no card-database access to
  determine a card's type itself; blocked on failure with a `QMessageBox::information`
  (mirroring the mana gate's "Cannot Cast" message), unlike the silent server-side
  rejections above.
- **Attack-timing enforcement** is covered in `phase8-combat.md`'s Stage A section
  (`AttrAttacking` is no longer unrestricted) rather than duplicated here.

### Deliberately still not implemented

- **Discard-to-hand-size at the end step is NOT auto-applied** — it's an advisory
  `QMessageBox::warning` only (Cleanup phase, hand size > 7, rule 514.1), no
  auto-discard, since discarding is a real player choice unlike untap/draw.
- **Activated abilities are not timing-gated** — this enforcement is scoped to casting
  a card from hand (creatures/sorceries/artifacts/enchantments/planeswalkers/lands) and
  declaring attackers, matching what was asked for; an ability with a real sorcery-speed
  restriction (e.g. "activate only as a sorcery") on a permanent already on the
  battlefield is a separate, not-yet-scoped follow-up.

## Known pre-existing upstream bug (not this fork's code)

Constructing/destructing multiple `Server_Game`/`Server_Room` instances as stack
locals in sequence within one test process segfaults deterministically —
`Server_Game::~Server_Game()` calls `deleteLater()` on itself at the end of its own
destructor body, which is undefined behavior. Every pre-existing test using these
classes only ever constructed one instance per process, so this had never been hit
before this fork's tests needed several. Out of scope to fix (touches core
game-lifecycle code with no Commander-specific angle) — worth filing upstream.
**Workaround used in this fork's tests**: heap-allocate `FakeServer`/`Server_Room`/
`Server_Game` and deliberately never free them (the test process is short-lived)
rather than use stack locals.

## Testing

`tests/movecard_tests/commander_turn_structure_test.cpp` — pure logic tests for
`phaseAutomationFor()` (all phase/turn/player-count combinations, including the
103.8a/103.8c skip-first-draw distinction) plus integration tests for the reused
`setCardAttrHelper`/`drawCards` mechanisms. **Not covered**: a true end-to-end test
that `setActivePhase()` fires automation for a *registered* game participant —
`Server_Game::addPlayer()` requires a live `Server_AbstractUserInterface`, and there's
no lighter-weight seam to inject one for testing. Compensated for by testing the
decision logic and the reused mechanisms separately, both directly, plus live
verification (see `CLAUDE.md`'s UI-testing recipe).

`tests/rules/rules_engine_test.cpp` covers the 2026-07-21 enforcement additions:
`canAdvanceToPhase` (exactly-one-step-forward, rejects skip-ahead/backward/staying
put/wrap-to-zero), `canEndTurn` (Cleanup-only), and `canCastSorcerySpeed`
(main-phase/active-player/holds-priority, each independently required). Full suite
(24/24 binaries) passes, `format.sh --cmake --branch master` clean.

**Live-verified end to end** via a new `python3 .uitest/scenario.py run
turn_structure_gate` scenario (`.uitest/turnstructuretest.cod`: Plains, Lightning
Bolt), the same one-player local-hotseat trick as `phase6-mana.md`'s scenarios.
Confirms, via the client's own debug log: Plains blocked while not in a main phase;
Lightning Bolt (an instant) casting anyway in that same phase; a plain Tab press
("Next Phase") legally advancing exactly one step and landing on First Main; and
Plains then succeeding once actually in a main phase. This run is also what
surfaced (and fixed) two real infrastructure bugs, not just confirmed the new
enforcement: `mana_gate`/`variable_mana_gate`'s own hand-building trick depended on
the free phase-jumping this change removes, and `.uitest/scenario.py`'s
`ensure_client()` never established window focus for a fresh client, so a
keyboard-first scenario silently sent nothing until some click happened to land on
the window — see `COMMANDER_IMPLEMENTATION_STATUS.md`'s "Current status" section
for the full writeup of both.
