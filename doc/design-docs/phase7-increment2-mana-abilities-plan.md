# Phase 7 Increment 2: Mana Ability Recognition + One-Click Activation

**Status: planned, not yet implemented.** This is this fork's own actionable
implementation plan (unlike `mtg-commander-rules-engine.md`, which is a mirrored
external reference doc) — written so a fresh session can execute it directly
from this file plus `COMMANDER_IMPLEMENTATION_STATUS.md`, without needing to
re-derive the scoping conversation that produced it. Read
`COMMANDER_IMPLEMENTATION_STATUS.md`'s "Phase 7" section first for why
Increment 1 (evergreen keywords) was built the way it was — this increment
reuses the same conventions and file layout.

## Goal

Recognize a card's simplest printed mana abilities ("{T}: Add {G}.") from its
rules text, and offer a one-click context-menu action that does what a player
would otherwise do manually in two steps: tap the permanent, then increment
the matching color counter.

## Why this is a bigger step than Increment 1, and what that implies

Increment 1 (keywords) only ever *displays* what it parses — a wrong parse is
cosmetic. This increment's parse result, once a player clicks the new menu
action, *mutates real shared game state* (tap flag + counter value) via the
same two commands (`Command_SetCardAttr`, `Command_IncCounter`) every other
manual action in this fork already uses. That means a parsing mistake here
has a different failure mode than Increment 1: it could offer a mana-looking
action on a card that isn't actually a simple mana source, or add the wrong
amount/color. This plan is deliberately narrow specifically to keep that risk
low and recoverable (any resulting counter/tap state is trivially
hand-correctable via the existing right-click menu / "Set counter..."
dialog, same as any other manual mistake in this fork already is).

## Scope boundary (read this before writing the parser)

**In scope for this increment — the "simple fixed mana ability" pattern:**
- An ability that is its own line (after stripping reminder text, same
  convention as `CardKeywords::parse()`), of the exact shape
  `{T}: Add {X}{X}...{X}.` where every `{X}` is the *same* single mana
  symbol: one of `{W}`, `{U}`, `{B}`, `{R}`, `{G}`, `{C}`.
- One or more repeated symbols is fine (`{T}: Add {C}{C}.` — e.g. Sol Ring —
  produces 2 colorless), since that's still a fixed, unambiguous amount with
  no player choice involved.
- A permanent can have more than one such line (e.g. a card with two
  separate fixed-color tap abilities); each qualifying line becomes its own
  offered action.
- Gate the offered action on: card is in the `TABLE` zone (mana abilities
  from other zones are a real but rare exception, out of scope), and the
  card is currently untapped (`!card->getTapped()`).

**Explicitly out of scope for this increment — do not extend the parser to
cover these without a fresh scoping pass:**
- Any ability requiring a **player choice** at activation time — "Add one
  mana of any color," "Add {W} or {U}" — because offering it means building
  a new choice dialog, which is real new UI surface this increment
  deliberately avoids. (Command Tower and similar color-identity-choice
  rocks are common, valuable Commander cards — but they're a good, contained
  candidate for a *follow-up* increment specifically because the choice-UI
  problem is separable from everything else here. Don't fold it in.)
- Any ability with an **additional or alternative cost** beyond tapping
  ("{T}, Sacrifice a Forest: Add...", "Pay 1 life. {T}: Add...").
- Any **conditional/restricted** ability ("Activate only once each turn,"
  "{T}: Add {G}. Spend this mana only to cast a creature spell").
- Anything that isn't a `{T}: Add ...` **activated** ability — one-shot mana
  effects from spells (Dark Ritual), triggered mana abilities, etc.
- Auto-payment during casting / cost validation — that's Phase 6, and stays
  a separate, still-undecided item (see `COMMANDER_IMPLEMENTATION_STATUS.md`'s
  Phase 6 section). This increment is a manual, player-clicked convenience
  action, never something the client decides to do on its own.

If live testing turns up real cards whose text is a false positive or false
negative against this exact pattern, the correct response is to tighten the
pattern further (drop cases), not loosen it to guess — same principle
`CardKeywords::parse()` already follows.

## Implementation plan

### 1. Parser library

New files, same location convention as Increment 1:
`libcockatrice_card/libcockatrice/card/ability/mana_abilities.{h,cpp}`.

```cpp
// mana_abilities.h
struct ManaAbility {
    QString producedSymbol; // "W", "U", "B", "R", "G", or "C"
    int amount;             // count of repeated symbols, e.g. 2 for Sol Ring
};

namespace ManaAbilities {
QList<ManaAbility> parse(const CardInfo &card);
}
```

Implementation approach (mirrors `card_keywords.cpp`):
- Strip reminder text (reuse the same regex as `CardKeywords`/`CommanderRules`
  — consider whether this third copy is worth factoring into a shared helper
  at this point; if so, put it somewhere both `format/commander_rules.cpp`
  and `ability/*.cpp` can use without introducing a weird include direction).
- Split rules text into lines. For each line, match against a regex requiring
  the *entire* line (after trimming trailing period) to be
  `\{T\}: Add (\{[WUBRGC]\})+\.`-shaped, with every captured symbol group
  identical. Reject the line (contribute nothing) if it doesn't match this
  exactly, or if the symbols aren't all the same letter.
- Register in `libcockatrice_card/CMakeLists.txt` the same way
  `card_keywords.cpp`/`.h` were added.

### 2. Tests

New `tests/card_ability/mana_abilities_test.cpp`, added to
`tests/card_ability/CMakeLists.txt` as a second `add_executable` (same
pattern as the `commander` test directory has two binaries). No card
database needed — construct `CardInfo` directly via `CardInfo::newInstance()`
like the existing tests in this directory.

Cases to cover at minimum:
- Single fixed-color line (`"{T}: Add {G}."`) → one `ManaAbility{"G", 1}`.
- Repeated-symbol line (`"{T}: Add {C}{C}."`) → `ManaAbility{"C", 2}`.
- Reminder text stripped before matching.
- Mixed-symbol line (`"{T}: Add {W} or {U}."`) → **not** matched (this is
  the player-choice case, explicitly excluded — assert it produces nothing).
- Additional-cost line (`"{T}, Sacrifice this artifact: Add {C}{C}{C}."`) →
  not matched.
- Conditional line (`"{T}: Add {G}. Spend this mana only to cast..."`) → not
  matched (extra text after the `Add {X}.` clause breaks the exact-line
  match).
- A card with two separate qualifying lines → both returned.
- A card with no qualifying lines → empty list.
- Case-insensitivity of the literal "Add"/"{T}" tokens if the source data
  varies in casing (check what Cockatrice's card XML actually uses before
  assuming — likely consistent, but verify rather than assume).

### 3. Client wiring

- Add a new `CardMenuActionType` value (`cockatrice/src/game_graphics/player/menu/card_menu_action_type.h`)
  — e.g. `cmActivateManaAbility` — following the existing enum's pattern.
  Note `PlayerActions::cardMenuAction()` currently branches on
  `if (type <= cmClone)` for the first switch block (see
  `player_actions.cpp` around line ~1762) — check whether a new action
  needs its own branch/switch block or fits into that existing one; don't
  assume without reading the surrounding dispatch logic first, since a
  wrong assumption here would silently misroute the command.
- In `card_menu.cpp`'s `CardMenu` constructor: after the existing card
  actions are built, call `ManaAbilities::parse()` against
  `card->getCard().getInfo()` (`CardItem::getCard()` returns `ExactCard`,
  confirmed already used elsewhere in this file's surrounding code, e.g.
  `cmFlip`'s handling). For each returned `ManaAbility`, add a menu action
  (icon: reuse `createCircleIcon()` already defined in this file, colored to
  match the produced symbol — there's already a color-to-QColor mapping
  convention somewhere near the counter colors in `server_player.cpp`
  (`makeColor(255,255,150)` etc. for w/u/b/r/g) — check whether a shared
  color constant exists client-side before inventing a new one). Label
  text: e.g. `tr("Tap: Add %1").arg(symbolDisplay)`.
- Only build these actions when `card->getZone()->getName() == ZoneNames::TABLE`
  and `!card->getTapped()` (see scope boundary above).
- In `PlayerActions::cardMenuAction()`, handle the new action type: build a
  `Command_SetCardAttr` (tap, same shape as the existing `cmTap` case) *and*
  a `Command_IncCounter` for the matching counter, both appended to the same
  `commandList` this function already batches and sends — reuse that
  existing batching, don't invent a new one. Counter lookup: iterate
  `player->getLogic()->getCounters()` (a `QMap<int, CounterState*>`,
  confirmed available) and match `getName()` against the lowercase symbol
  letter (`"w"`, `"u"`, `"b"`, `"r"`, `"g"`), or `"x"` for colorless `{C}`
  (confirmed existing counter naming from `server_player.cpp`'s
  `setupZones()` — colorless is named `"x"` there, not `"c"`; get this
  mapping right, it's a real existing naming quirk, not a free choice).
  Increment by `ManaAbility::amount`, not always by 1.

### 4. Live verification

Per this repo's established practice (`CLAUDE.md`'s UI-testing section):
local `servatrice` + Xvfb + the real client, not just unit tests.

- Check whether `.uitest/sample_cards.xml` already has a card with a
  qualifying mana ability (a basic land's printed text is usually empty in
  Cockatrice's data — basic land mana abilities are often implicit/special-
  cased rather than printed text, so **verify this first**; a Sol-Ring-style
  fixed artifact, e.g. `"{T}: Add {C}{C}."`, is a safer bet for a test
  fixture since it's genuine printed rules text). Add one if missing.
- Start a solo Commander game (same recipe as the Phase 9 live-verification
  session: local servatrice, 1-player deck, force start).
- Right-click the mana-source permanent on the battlefield, confirm the new
  "Tap: Add X" action appears, confirm it does **not** appear on an
  already-tapped copy or on a card without a qualifying ability (a basic
  bear/vanilla creature, already in the sample deck).
- Click it, screenshot before/after, and cross-check the debug log
  (`Event_SetCardAttr` for the tap, `Event_SetCounter` for the counter) the
  same way the Phase 9 session cross-referenced screenshots against
  `/tmp/cockatrice_gui.log` — don't rely on pixels alone for confirming the
  exact counter delta.

### 5. Docs

Update `COMMANDER_IMPLEMENTATION_STATUS.md`'s Phase 7 table row and add an
"Increment 2" subsection under the existing "Phase 7" section, following the
same structure as the Increment 1 write-up (what's implemented, why it's
bounded the way it is, test results, live-verification results). Mark this
plan doc's header as "implemented" once done, or record what changed if the
implementation deviated from this plan.

## Checklist for the implementing session

- [ ] Read `COMMANDER_IMPLEMENTATION_STATUS.md`'s Phase 7 section (context)
- [ ] Confirm sandbox build state (Qt6/swap/build dir) per `CLAUDE.md`
- [ ] Write `mana_abilities.{h,cpp}` + register in `libcockatrice_card/CMakeLists.txt`
- [ ] Write `mana_abilities_test.cpp` + register in `tests/card_ability/CMakeLists.txt`
- [ ] Build + run the new test target in isolation, confirm green
- [ ] Investigate `card_menu_action_type.h` / `PlayerActions::cardMenuAction()`
      dispatch shape (don't assume — read it) before adding the new case
- [ ] Wire the new context-menu action in `card_menu.cpp` + `player_actions.cpp`
- [ ] Build `cockatrice`, confirm clean
- [ ] Run full `ctest` suite, confirm no regressions
- [ ] `./format.sh --cmake --branch master`
- [ ] Live-verify via Xvfb + servatrice + screenshot + debug-log cross-check
- [ ] Update `COMMANDER_IMPLEMENTATION_STATUS.md`, commit, push to `fork/commander-rules`
