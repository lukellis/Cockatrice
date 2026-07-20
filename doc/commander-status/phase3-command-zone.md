# Phase 3: Command Zone & Commander Tracking

**Status: done.**

Design doc §3 Phase 3. Matches the doc's proposed `CommanderState` fields (cast count,
damage-dealt-to map) conceptually, implemented as ordinary Cockatrice counters rather
than a dedicated struct — consistent with how Cockatrice already tracks every other
piece of numeric game state (life, poison, etc.) via the generic `Server_Counter`
mechanism.

## What's implemented

- **Command zone**: a real `ZoneNames::COMMAND` zone
  (`Server_Player::setupZones()`). The deck's designated commander (`DeckList`'s
  banner card) starts here instead of in the deck.
- **Commander tax**: a per-commander counter (`CommanderCounterNames::tax()`,
  `"Commander Tax: <name>"`) starting at 0, incremented by 1 each time that commander
  is cast from the command zone (`Server_Player::onCardBeingMoved()` — a move straight
  back to the command zone, e.g. an undo, doesn't count). Displayed count × 2 = the
  additional generic mana cost per rule 903.9. The decision of *which* moves count is a
  pure `Rules::RulesEngine::commanderTaxCounterNameForMove()` call — see the Phase 1
  doc.
- **Commander damage**: every player gets a counter
  (`CommanderCounterNames::damage()`, `"Commander Damage: <name>"`) tracking damage
  received from each other player's commander(s), created in
  `Server_Game::doStartGameIfReady()` once all command zones are populated. A
  client-side advisory `QMessageBox::warning` fires the moment any single one reaches
  21 (rule 704.5g) — informational only, doesn't end the game or block anything,
  matching this fork's Assisted-Mode philosophy.
- **Game defaults**: 40 starting life, 4-player multiplayer for Commander games
  (`dlg_create_game.cpp`).
- **Command-zone UI**: `CommandZone` (`cockatrice/src/game_graphics/zones/command_zone.{h,cpp}`),
  a `PileZone` subclass with a gold border/tint and a "CMD" label, rendered upright
  (identity transform, not `PileZone`'s default 90° rotation) and reordered ahead of
  deck/graveyard/exile in the pile stack so the commander has a visibly distinct,
  readable location.

## Real bug found and fixed

`Server_Player::onCardBeingMoved()`/`Server_Game::doStartGameIfReady()`'s command
zone/tax/damage hooks were originally **not gated by game format at all** — they ran
unconditionally, only "self-gating" by coincidence of whether a deck happened to have a
banner card set. Fixed by adding an explicit Commander-game check before the fork
became wholly Commander-only in Phase 1 (at which point the gate was removed again,
since it was no longer needed).

A separate real bug: `player_logic.cpp`'s `eventGameStateChanged()` didn't include
`ZoneNames::COMMAND` in its `builtinZones` allowlist, so every game-state refresh
deleted and silently recreated the command zone's `PileZoneLogic`, disconnected from
the visible widget — the command-zone UI was permanently stuck showing "0 cards"
regardless of actual state. Root-caused by cross-referencing a screenshot against the
client debug log's `Event_GameStateChanged` (`card_count: 1` on-wire vs. "0" on
screen) — not discoverable from `command_zone.cpp` alone, since that file's own logic
was already correct. This is the canonical example (referenced from `CLAUDE.md`) of why
live UI testing plus debug-log cross-referencing catches bugs GTest alone misses.

## Known limitations

- Command zone has no dedicated context menu (graveyard/exile do).
- No Partner/Background pairing (see Phase 2 doc) — a single commander only.

## Testing

Covered by `tests/commander/` (deck-validator integration tests exercise commander
placement indirectly) and live verification: local servatrice + client, confirming the
commander starts in the command zone, the tax counter exists at 0, tax increments to 1
on casting, and starting life/players match the Commander defaults. See `CLAUDE.md`'s
UI-testing recipe.
