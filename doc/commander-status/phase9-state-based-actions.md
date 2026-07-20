# Phase 9: State-Based Actions (advisory warnings)

**Status: partial, by design — three of the most common loss conditions, all
advisory.**

Design doc §3 Phase 9 calls for full continuous state-based-action checking (rule
704.3). Real Magic re-checks SBAs continuously, which would need to interrupt/replace
this fork's manual, non-blocking model to do properly — out of scope for the same
reason turn-structure/priority enforcement is only ever advisory (see `CLAUDE.md`'s
design principles). What's implemented instead follows the exact pattern already
established for lethal commander damage (Phase 3): a client-side
`QMessageBox::warning` fires the moment a counter/zone crosses its lethal threshold —
informational only, doesn't end the game, remove the player, or block further action.

## What's implemented

- **Life ≤ 0** (rule 104.3a) — hooks the existing `life` counter's
  `CounterState::valueChanged` signal, firing on the `> 0 → ≤ 0` crossing. No server
  changes needed.
- **Drawing from an empty library** (rule 104.3b) — hooks the existing
  `logDrawCards(..., deckIsEmpty)` signal, firing when a draw was attempted and the
  library was already empty.
- **≥10 poison counters** (rule 104.3c) — needed one server-side addition, since
  poison isn't among Cockatrice's default per-player counters:
  `Server_Player::setupZones()` creates an 8th counter, `poison` (green, starting at
  0). No dedicated theme icon exists for it, so it renders via the existing `general`
  fallback icon — cosmetic only. Client warning watches the `< 10 → ≥ 10` crossing.

All three dedup on the *crossing* (old value vs. new value against the threshold), not
"is currently past it" — so re-opening a game state or a redundant counter update
doesn't re-fire the dialog.

## Deliberately not done

No other SBAs from rule 704 — the legend rule (needs to know which permanents share a
name), 0-toughness creatures, illegally-attached auras. These need either card-state
modeling this fork doesn't have, or a continuous-checking loop that doesn't fit the
manual-simulator model. Only pure counter/zone-threshold checks fitting the exact
pattern already proven for commander damage were picked.

## Testing

Live-verified: a solo game forced into each of the three threshold crossings (setting
life to 0, poison to 10, drawing with an empty library) produces the correct warning
text and rule citation, with no crashes or spurious re-fires. See `CLAUDE.md`'s
UI-testing recipe.
