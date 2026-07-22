# §12: UI Design & Enhancements

**Status: partial**, growing incrementally alongside whichever phase needs a visible
surface. Not started: a 4-player grid layout, a dedicated mana pool widget. Below is
what exists.

## Command zone visual (§12.6)

See `phase3-command-zone.md` — a gold-bordered, upright `CommandZone` pile distinct
from deck/graveyard/exile, not the full commander-tax/partner display the design doc
sketches.

## Pending-ability / priority visualization panel (2026-07-18)

Phase 7 Stage 3 built a real, server-authoritative pending-ability stack and Phase 5
built real priority-passing — but neither was ever visible to players as *state*, only
as transient message-log lines. This closes that gap.

**Key finding that kept it small**: the server already broadcasts everything needed
(`Event_AbilityActivated`/`Event_AbilityResolved` with full payload, plus
`Event_PriorityChanged`) to every client unconditionally — this is purely a
**client-side visualization of state the wire protocol already carries, zero new
protocol messages**. The only real gap was that `GameEventHandler` discarded the
effect/target payload before it reached a signal, resolving only enough for the
existing dataless log lines.

**LIFO correctness without a unique ability id**: neither event carries a stable
identifier for "which pending entry." This is fine — the server's pending-ability
store is a genuine LIFO stack that always resolves exactly the top entry, so a
client-side mirror that pushes on `Event_AbilityActivated` (append) and unconditionally
pops its own top entry on any `Event_AbilityResolved` stays correctly in sync by
construction, no id needed.

**What's implemented**: a new `PendingAbilityWidget`
(`cockatrice/src/game_graphics/stack/pending_ability_widget.{h,cpp}`) — a header
("Priority: `<Player>`" / "Priority: no one", driven by the existing `priorityChanged`
signal) above a list (newest-on-top = top-of-stack), registered as a new dock in
`tab_game.{h,cpp}` alongside Card Info/Messages/Player List, including in
`actResetLayout()`'s default layout. A `describeEffect()` helper turns
`DrawCards`/`GainLife`/`LoseLife`/`DealDamage` into readable text — deliberately
generic for a card-zone target ("a permanent") rather than resolving the exact card
name, since that isn't reliably possible for a hidden/opponent's card.

**Deliberately excluded**: resolving/displaying the exact target card name; any
interaction affordances on the list (e.g. clicking an entry) — this is a read-only
visualization, not a new control surface.

## Testing

No dedicated GTest file — pure UI wiring (display text, dock registration, signal
plumbing) follows this fork's established "UI wiring gets live-verified, not unit
tested" precedent. Live-verified: activating two abilities without passing priority
produces two list entries in the correct LIFO order with correct human-readable text,
persists unchanged across an unrelated phase transition, and correctly drains as
priority passes resolve each entry. See `CLAUDE.md`'s UI-testing recipe.

## Counter UI cleanup + Storm counter (2026-07-21)

Player feedback: the command zone visually overlapped the avatar box (see
`phase3-command-zone.md`'s own "UI cleanup" section for that fix and the Commander
Tax badge's move onto the command zone), several per-player counters rendered as
identical unlabeled circles, and one ("storm", upstream Cockatrice's generic manually-
adjustable "Other" counter) was confusing and unused in practice.

**Storm count, replacing the old manual counter**: rather than just deleting it, it's
now a genuine (if informal — not a Commander rule) auto-tracked "spells cast this
turn" count, rendered read-only:
- **Client-side increment**: the server has no card-type awareness of its own, so the
  +1 is appended as an ordinary `Command_IncCounter` from
  `PlayerActions::gateManaCostForHandPlay()` (`cockatrice/src/game/player/player_actions.cpp`)
  — the same established point that already piggybacks real-mana-payment
  `Command_IncCounter`s (Phase 6) — for any non-land card leaving hand (rule 305.1
  already excludes lands from "casting" a spell).
- **Server-side reset**: `Server_Player::resetStormCount()`, called once per turn from
  `Server_Game::setActivePhase()` on entering `Rules::RulesEngine::UNTAP_PHASE`,
  mirroring the existing per-phase mana-pool-empty sweep's "loop every player" pattern.
- **Read-only rendering**: a new `AbstractCounter` constructor flag (`interactive`,
  defaulted `true` so the two pre-existing subclasses need no changes) suppresses both
  the TearOffMenu and click-to-increment; `TextCounter`
  (`cockatrice/src/game_graphics/board/counter_text.{h,cpp}`) renders it as plain
  `"Storm: N"` text instead of `GeneralCounter`'s colored-circle-plus-icon, since a
  dynamic per-commander name wouldn't have a themed icon anyway.

**Look-alike-counter labels**: `CounterPixmapGenerator::generatePixmap` only has
dedicated icons for w/u/b/r/g — colorless mana, poison, and per-commander damage all
silently fall back to the same generic circle. `GeneralCounter::paint()` now draws a
short text label under any counter that isn't one of those five (abbreviated for the
per-commander damage counter's otherwise-long name), and every counter now has a
hover tooltip with its full display name (`AbstractCounter`'s constructor). Skipped
for counters rendered outside the shared counter column (currently only the Commander
Tax badge — see `phase3-command-zone.md`), since position alone already disambiguates
those.

Live-verified via `.uitest/scenario.py run counter_ui_gate` — see
`phase3-command-zone.md`'s Testing section for what it covers. No dedicated GTest
beyond `CommanderCounterNames::isTaxCounter()`'s pure-logic cases in
`tests/rules/rules_engine_test.cpp`, matching this doc's established precedent above.
