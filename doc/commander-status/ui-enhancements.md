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
