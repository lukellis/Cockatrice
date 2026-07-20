# Phase 2: Commander Deck Validation

**Status: done.**

Design doc §3 Phase 2. `CommanderDeckValidator::validate(const DeckListModel&)` →
`Result{isValid, errors}`
(`libcockatrice_models/libcockatrice/models/deck_list/commander_deck_validator.{h,cpp}`),
checking:

- Commander legality (`CommanderRules::canBeCommander`).
- Exactly 100 cards including the commander.
- Per-card format legality (banned list, basic-land singleton exception).
- Per-card color identity vs. the commander's (`CommanderRules::colorIdentity`,
  computed from mana cost + rules text via regex, reminder text stripped, hybrid
  symbols handled).

Wired into both server-side game start and a live client-side deck-editor status label
(`deck_editor_deck_dock_widget.{h,cpp}`) — red/green, unconditionally active since the
fork is Commander-only (Phase 1).

## Known limitations

- Single designated commander only — no Partner/Background pairing.
- Color identity doesn't pull in a double-faced card's back face.
- Both are real, un-scoped next candidates, not oversights — see the root tracker doc.

## Bugs found and fixed along the way

- **Deck-editor double-counting**: `updateCommanderValidation()` originally gated color
  identity on `formatUsesColorIdentity()` alone; caught by live UI testing (not GTest)
  that the deck editor's Banner Card picker double-counted the commander against the
  100-card total, since the real UI populates that picker *from* the main deck list,
  unlike how the original tests constructed the scenario via the API directly. Fixed;
  now covered by an integration test that goes through the same
  `DeckListModel`/`CardDatabaseManager` pipeline the real UI uses.
- **Banner card is not always the commander**: `deck_list_model.cpp`'s
  `isCardNodeLegalForFormat()`/`refreshCardFormatLegalities()` initially gated color
  identity enforcement on "a banner card is set," but `DeckList::bannerCard` can also be
  a purely cosmetic cover card. Fixed to gate on
  `CommanderRules::formatUsesColorIdentity()` instead (moot now that the fork is
  Commander-only, but the underlying `bannerCard` ambiguity is still real and worth
  knowing if this code is touched again).

## Testing

- `tests/commander/commander_rules_test.cpp` — pure algorithmic tests for
  `CommanderRules::*` (color identity from colors/mana-cost/rules-text, reminder-text
  exclusion, hybrid symbols, `canBeCommander`). No card database needed.
- `tests/commander/commander_deck_validator_test.cpp` — integration test against the
  real `DeckListModel`/`CardDatabaseManager` pipeline, using a dedicated fixture at
  `tests/commander/data/cards.xml` (isolated from the pre-existing carddatabase test
  fixture). Covers valid decks, missing/illegal/unknown commander, wrong card count,
  color identity violations both directions, banned cards, singleton violations, the
  basic-land exception.

## Live verification

Confirmed via the deck editor's red/green legality label and a real 100-card Commander
deck — see `CLAUDE.md`'s UI-testing recipe for how to reproduce this kind of check.
