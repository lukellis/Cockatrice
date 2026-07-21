# Phase 6: Mana System

**Status: pool auto-empty (rule 500.4) done; Phase 7 Stage 4 later added real cost
payment for activated abilities; this doc's own addendum below closes the original
design-doc gap for real spells cast from hand; a second addendum further down extends
that gate to hybrid, Phyrexian, and variable ({X}) costs.**

Design doc §3 Phase 6 (originally estimated 4–5 weeks) specs a full `ManaPool`/
`ManaCost` pair that parses a printed cost and validates/auto-pays it. That was out of
reach standalone at first — cost validation needs a parsed mana cost per card, which
needed Phase 7's card-ability engine as a prerequisite. Phase 7 Stage 4 built that
engine's payment core but scoped it to activated abilities only; the addendum below
extends the same engine to real printed spell costs when a card is cast from hand.

## What's already there for free

Cockatrice's existing `Server_Player::setupZones()` already creates per-player
`w`/`u`/`b`/`r`/`g`/`x` counters (ids 1–6) for every game — vanilla Cockatrice's
existing answer to "mana pool" under its manual-simulator model, just unlabeled and
with no phase-boundary behavior. No new counters, UI, or protocol were needed to have
*a* mana pool.

## What's implemented

Rule 500.4 — a mana pool empties at the end of every step and phase.
`Rules::RulesEngine::manaCounterNames()` returns the fixed 6-name list;
`Server_Player::emptyManaPool()` zeroes any of the calling player's counters whose name
is in that list, enqueuing `Event_SetCounter` only for ones that actually changed
(dedup via the existing `setCount()`/`didChange` convention).
`Server_Game::setActivePhase()` calls it for **every** player (not just the active one
— rule 500.4 empties everyone's pool) on every phase/step transition.

## Deliberately still excluded

Cost parsing, `canPay()` validation, auto-tap suggestions, and mana-ability activation
from context menus were out of scope for this slice specifically — those needs are
each addressed separately: mana-ability *production* recognition/one-click tap is Phase
7 Increments 1–2, and cost *payment* is Phase 7 Stage 4. This file only ever covers
zeroing counters at phase boundaries, never reading or validating a cost.

## Testing

`tests/rules/rules_engine_test.cpp`'s `ManaCounterNamesCoversTheFiveColorsPlusColorless`.
Live-verified: a real solo Commander game, manually setting a color counter to a
nonzero value and confirming it zeroes automatically on the next phase transition, with
no spurious events for already-zero counters and no effect on unrelated counters
(life, tax, poison).

## Addendum (2026-07-21): real mana payment for casting spells from hand

Closes this file's own originally-deferred goal — a card's printed mana cost is now
parsed and enforced when it's actually cast from hand, reusing Phase 7 Stage 4's
`ManaCost`/`RulesEngine::planManaPayment()` engine rather than rebuilding it.

- **New parser**: `SpellManaCost::parse(const CardInfo&)`
  (`libcockatrice_card/.../card/ability/spell_mana_cost.{h,cpp}`) reads
  `CardInfo::getManaCost()` (e.g. `"{2}{R}{R}"`) into a `ManaCost`. Same "exact shape or
  skip" conservatism as every other parser in this fork: the whole string must be
  covered by concatenated `{digit}`/`{W|U|B|R|G|C}` tokens, or the whole cost is
  unparseable (`std::nullopt`) — a hybrid (`{R/G}`), Phyrexian (`{R/P}`), variable
  (`{X}`), snow (`{S}`), or split-cost (`"3U // 4UU"`) symbol anywhere means that card is
  simply never gated, not partially/incorrectly gated. Deliberately independent of
  `ActivatedAbilities`'s own private cost-prefix tokenizer (same small-per-parser-copy
  precedent as `withoutReminderText()`), since that one only ever sees a substring
  already guaranteed safe by its containing line regex.
- **New pure `RulesEngine` helpers**, alongside (not replacing) `planManaPayment()`:
  `remainingPoolAfterColoredPips()`, `isGenericPaymentAmbiguous()`, and
  `planManaPaymentWithGenericChoice()` — together they let a caller detect when paying a
  spell's generic cost has a genuine color choice (at least two colors have a nonzero
  remaining balance *and* real slack — using every one of them isn't forced) rather
  than silently deferring to `planManaPayment()`'s own fixed w→u→b→r→g→x order.
- **New dialog**: `DlgChooseGenericManaPayment`
  (`cockatrice/.../game_graphics/dialogs/dlg_choose_generic_mana_payment.{h,cpp}`) — a
  spinbox per candidate color, pre-filled with the deterministic split so accepting
  immediately reproduces old behavior, shown only when `isGenericPaymentAmbiguous()` is
  true. Deliberately instantiated and `exec()`'d directly by its caller rather than
  routed through the usual `PlayerActions → PlayerDialogs` signal indirection — see the
  dialog's own header comment for why (it has three structurally different call sites,
  each needing the chosen split back synchronously).
- **The gate**: `PlayerActions::gateManaCostForHandPlay(card, faceDown, extraCommands)`
  — a no-op (returns `true`) unless the card is actually leaving the `HAND` zone
  face-up, isn't a land, and has a parseable, non-free cost. Otherwise: unaffordable →
  blocking message + abort (rule 601.2h, "pay in full or never start" — the Stage 4
  precedent); affordable-and-ambiguous → the dialog, cancel aborts atomically; otherwise
  proceeds with zero extra clicks. On success it appends the deduction plan as ordinary
  `Command_IncCounter`s (same zero-new-protocol idiom as Stage 4) for the caller to send
  batched with its own move command.
- **Wired into every real "play a card from hand" path**: `PlayerActions::playCard()`
  (click-to-play, double-click-to-play, and arrow-drag-from-hand, which already funnels
  through it), plus direct drag-and-drop via `TableZone::handleDropEventByGrid()` and
  `StackZone::handleDropEvent()` — gating only click-to-play would have made enforcement
  trivially bypassable by the most common way to actually play a card.

### Deliberately excluded

- **Lands** — free by rule 305.1, never gated regardless of cost string.
- **Monocolored hybrid, snow, and split-cost spells** — unparseable, so never gated; same
  conservative fallback as every other unrecognized shape in this fork. Plain two-color hybrid
  (`{R/G}`), Phyrexian (`{R/P}`), and variable (`{X}`) costs *are* gated — see the addendum
  below.
- **Face-down plays** (morph-style) — card identity is intentionally hidden, so no cost
  is even known to gate against.
- **"Casting" from graveyard/exile/library-view via the click-to-play convenience** —
  the gate only fires when the source zone is `HAND`; this fork doesn't model
  flashback/alternate-cast-source costs.
- **Multi-card drags** — the gate only applies to a single dragged card, same boundary
  Phase 7 Stage 4 already drew for costed-ability activation.
- **Casting an Aura/Equipment via a drag-attach arrow directly onto a target**
  (`ArrowAttachItem::attachCards()`), and the card-relation/token-creation
  auto-move-to-table convenience (`onRelatedCardCreated()`) — both go through
  `PlayerActions::playCardToTable()`, which is deliberately not gated; rarer
  interaction shapes than click-play and plain drag-to-table/stack, left as a
  separate follow-up rather than folded in here.

### Testing

`tests/card_ability/spell_mana_cost_test.cpp` (parser conservative-shape coverage) and
new cases in `tests/rules/rules_engine_test.cpp` (`isGenericPaymentAmbiguous`'s
single-color/forced-sum/genuine-slack/unaffordable cases,
`planManaPaymentWithGenericChoice`'s valid/over-cap/wrong-total cases) — full suite
(24/24 binaries) passes, `format.sh --cmake --branch master` clean (one unrelated
cross-version reformat of `pending_ability_widget.{cpp,h}` reverted, same documented
`CLAUDE.md` caveat as prior sessions).

**Live-verified end to end** via a new `python3 .uitest/scenario.py run mana_gate`
scenario (`.uitest/manatest.cod`: Sol Ring, Kaya's Wrath, Plains — a one-player local
hotseat game, no real second client needed). This also retroactively overturns a
standing belief from three prior sessions (Stage C/D, the static-abilities extension,
and this addendum's own first attempt): "card-item clicks/drags don't register in this
headless, window-manager-less Xvfb setup" was **not a fundamental limitation** — the
real, previously undiagnosed root cause was a `QMessageBox` ("Congratulations on
updating to Cockatrice `<hash>`!") that this fork's git-hash-embedding `VERSION_STRING`
pops on almost every session, sitting modally on top of the whole window and silently
swallowing every subsequent synthetic click. `.uitest/scenario.py`'s
`ensure_test_client_profile()`/`ensure_test_server_profile()`/`ensure_local_game_profile()`
now idempotently fix this (and two independent, smaller sources of flakiness: Qt
double-click timing over XTest, and the "Connect to Server" dialog's fragile
New-Host-field coordinates) on every run — see that file's own module docstring and
`CLAUDE.md`'s updated UI-testing note. The `mana_gate` scenario confirms, via the real
`Event_MoveCard`/`Event_SetCounter` protobuf lines in the client's own debug log (not
just screenshots): Sol Ring correctly blocked with an empty pool; Plains entering the
battlefield for free; Sol Ring auto-paying from a single available color with no
dialog; and Kaya's Wrath casting through the ambiguous-payment dialog's default split,
decrementing exactly the expected counters (`w`→0, `u`→1, `b`→0 for a `{2}{W}{B}` cost
against a `W=2,U=2,B=1` pool). Card disambiguation in hand uses a background-color
sample (Kaya's Wrath's WB colors render a distinct gold placeholder frame; Sol
Ring/Plains share the same plain-gray one, told apart by whether casting them
immediately succeeds) rather than OCR, since no text-recognition tooling is available.

## Addendum (2026-07-21): hybrid, Phyrexian, and variable ({X}) mana costs

Closes the previous addendum's own "Deliberately excluded" gap: a spell cast from hand
with a two-color hybrid (`{R/G}`), Phyrexian (`{R/P}`), or variable (`{X}`) symbol in its
printed cost was previously left entirely ungated (treated as unparseable). All three
are now parsed and gated, chosen as the highest-value remaining gap from
`COMMANDER_IMPLEMENTATION_STATUS.md`'s known-limitations list. Monocolored hybrid
(`{2/W}`), snow (`{S}`), split-cost (`"3U // 4UU"`), and `ActivatedAbilities`' own
separate Stage 4 cost parser remain untouched — each is its own separate scope
boundary, not swept in by this change.

- **Extended parser**: `SpellManaCost::parse()` now accepts `[WUBRG]/[WUBRG]` (hybrid),
  `[WUBRG]/P` (Phyrexian), and a bare `X` token, in addition to the digit/single-letter
  tokens it already recognized — same "whole string or nothing" conservatism as before.
  `ManaCost` (`card_effects.h`) gained three new fields to hold them —
  `hybridPips`/`phyrexianPips`/`xCount` — populated only by this parser;
  `ActivatedAbilities`' own cost parser never touches them, so every activated ability
  stays exactly as costed as before this addendum.
- **New `RulesEngine` helpers**, reusing (not replacing) `planManaPayment()`:
  `planManaCostChoices()` does one deterministic pass over a cost's hybrid/Phyrexian
  pips (same "only ask when a genuine choice exists" spirit as
  `isGenericPaymentAmbiguous()`, same "fixed order, not a solver" simplification as
  `planManaPayment()`'s own generic draining), and `resolveManaCost()` mechanically
  folds a caster's chosen X value/hybrid colors/Phyrexian mana-vs-life picks into a
  plain `ManaCost` (hybrid/Phyrexian/X fields cleared) plus a separate life cost —
  after which every pre-existing function (`planManaPayment`,
  `isGenericPaymentAmbiguous`, `planManaPaymentWithGenericChoice`) runs completely
  unchanged on the result.
- **New dialog**: `DlgChooseVariableManaCost`
  (`cockatrice/.../game_graphics/dialogs/dlg_choose_variable_mana_cost.{h,cpp}`) —
  modeled on `DlgChooseGenericManaPayment`'s "instantiated/exec()'d directly by its
  caller" pattern. One optional row per component: an X spinbox, one radio pair per
  ambiguous hybrid pip, one radio pair per ambiguous Phyrexian pip (defaulting to "pay
  mana," not life). A cost with hybrid/Phyrexian symbols but nothing actually ambiguous
  (and no `{X}`) skips the dialog entirely — zero extra clicks, same precedent as the
  existing generic-payment dialog. A sufficiently exotic cost can pop this dialog and
  then still trigger the pre-existing generic-split dialog afterward — an accepted
  two-dialogs-in-a-row UX tradeoff for keeping each dialog single-purpose.
- **Wired into `gateManaCostForHandPlay()`** ahead of the pre-existing affordability
  check: resolves hybrid/Phyrexian/X first, then falls straight into the unmodified
  existing flow against the resolved cost. Life payment reuses
  `appendManaPaymentCommands()` a second time against the `"life"` counter — no new
  command-building code needed, since life is just another named per-player counter.

### Testing

`tests/card_ability/spell_mana_cost_test.cpp` (hybrid/Phyrexian/multi-X/combined
parsing, monocolored-hybrid/snow/split-cost still rejected) and new cases in
`tests/rules/rules_engine_test.cpp` (`planManaCostChoices`'s ambiguous/forced-default
cases including two pips sharing a color, `resolveManaCost`'s folding into
coloredPips/generic/lifeCost) — full suite (24/24 binaries) passes, `format.sh --cmake
--branch master` clean.

**Live-verified end to end** via a new `python3 .uitest/scenario.py run
variable_mana_gate` scenario (`.uitest/variablemanatest.cod`: Ghor-Clan Rampager
`{2}{R/G}`, Dismember `{1}{B/P}{B/P}`, Fireball `{X}{R}` — added to
`.uitest/sample_cards.xml`), the same one-player local hotseat trick as `mana_gate`.
Confirms, via the client's own debug log: Ghor-Clan Rampager casting through the hybrid
dialog's default color choice; Dismember casting through both Phyrexian pips' default
"pay mana" choice (no life spent); and Fireball casting after actually entering X=2 via
the new spinbox (not just accepting a default) — each decrementing exactly the expected
mana counters. `scenario.py` itself gained multi-deck support
(`LOCAL_GAME_DECKS`/`ensure_local_game_profile(deck_path=...)`) since this scenario
needs a different fixture deck than `mana_gate`'s.
