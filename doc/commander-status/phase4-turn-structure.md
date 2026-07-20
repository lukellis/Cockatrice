# Phase 4: Turn Structure Automation

**Status: partial, by design.**

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

## Deliberately not implemented

- **Phase-order enforcement**: a player can still freely jump to any phase in any
  order. Assisted Mode is about warnings, not blocking; enforcing strict order would be
  a much bigger UX change affecting all play styles, not a narrow automation.
- **Discard-to-hand-size at the end step is NOT auto-applied** — it's an advisory
  `QMessageBox::warning` only (Cleanup phase, hand size > 7, rule 514.1), no
  auto-discard, since discarding is a real player choice unlike untap/draw.

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
