# Phase 8 extension: static/continuous abilities

**Status: first slice done — P/T-boost and keyword-grant anthem/lord effects, consumed by combat
math only.**

Through everything built so far (Phase 7's activated/triggered ability engine, Phase 8's combat
automation), every card-ability mechanic in this fork acted on a single card in isolation:
activated/triggered abilities affect their own controller or a chosen target, and combat math
reads each creature's own P/T and keywords. **No permanent had ever been able to affect another
permanent's characteristics.** This closes that gap for the two most common static-ability shapes
in real Magic — anthem effects ("Other creatures you control get +1/+1.") and keyword-granting
lords ("Creatures you control have trample.") — restricted to wherever P/T and keywords are
already consumed server-side today (combat), not as a general characteristic-modification layer.

## What's built

- **`StaticAbilities::parse(const CardInfo &)`**
  (`libcockatrice_card/.../card/ability/static_abilities.{h,cpp}`) — new shared IR, following the
  same "exact shape or skip" conservatism as `CardKeywords::parse()`. Recognizes two independent,
  whole-line phrasings, each scoped to `StaticScope::AllYours` ("Creatures you control") or
  `StaticScope::OthersYours` ("Other creatures you control"):
  - `"<scope> get +X/+Y."` → a `powerBonus`/`toughnessBonus` pair.
  - `"<scope> have <keyword list>."` → tokenized/canonicalized against
    `CardKeywords::evergreenKeywords()` the same conservative way `CardKeywords::parse()` already
    does (every comma/"and"-separated token must canonicalize, or the whole line is skipped).
  A card with multiple qualifying lines produces one `StaticAbility` per line.
  `StaticAbilities::serialize()`/`deserialize()` round-trip the result for the wire (one ability
  per `;`-joined entry, fields `|`-joined).
- **Protocol**: `AttrStaticAbilities` (`card_attributes.proto`, ext value 11) round-trips exactly
  like Stage C's `AttrKeywords` — a `static_abilities` sibling field next to each existing
  `keywords` field on `CardToMove` (`command_move_card.proto`), `Command_CreateToken`
  (`command_create_token.proto`), `Command_FlipCard` (`command_flip_card.proto`), and
  `ServerInfo_Card` (`serverinfo_card.proto`). All six client call sites that already populate
  `keywords` from a `CardInfo` (`playCard`, `playCardToTable`, related-card token creation,
  `cmMoveToTable`, `cmClone`, `cmFlip` — all in `player_actions.cpp`) get a sibling
  `set_static_abilities(StaticAbilities::serialize(StaticAbilities::parse(info))...)` call.
  `Server_Card` gained `staticAbilitiesString`/`getStaticAbilities()`/`setStaticAbilities()`
  mirroring `keywordsString`, plus `getKeywordSet()` (the full parsed set behind `hasKeyword()`,
  needed once a caller must fold in granted keywords rather than check one fixed name). As with
  `AttrKeywords`, there's no client-side `Event_SetCardAttr` receive handler — server-only
  consumer today, no client display change.
- **`Rules::RulesEngine::applyStaticEffects(targetCardId, basePower, baseToughness, baseKeywords,
  controllerBattlefield)`** (`rules_engine.{h,cpp}`) — the pure function that actually folds static
  abilities into a target's characteristics. `controllerBattlefield` is `(cardId,
  staticAbilitiesString)` for every card the *same controller* as the target controls; scoping the
  list to one controller is the caller's job (mirrors how `calculateCombatDamage()` takes
  pre-scoped `CombatAttack`s rather than zone objects), which is what makes "you control" mean the
  right thing without this function needing any live `Server_Card`. An `OthersYours` ability is
  skipped for its own source card (an anthem never buffs itself); an `AllYours` ability applies
  even to its own source.
- **Consumption**: `Server_Game::staticAbilitySourcesFor(Server_CardZone *)` gathers
  `(cardId, staticAbilitiesString)` for one table zone. Wired into the *only three* places
  `parseNumericPT`/`hasKeyword` were read server-side before this change (confirmed by grep, not
  guessed): both halves of `gatherCombatAttacks()`'s `CombatCreature` construction (attacker and
  blocker), and `applyCombatDamageResult()`'s post-damage lethal-check loop. That third site
  matters as much as the first two — an anthem-boosted creature's *effective* toughness (not its
  base toughness) has to be what's compared against marked damage, or a 3/3-boosted-to-4/4 would
  incorrectly die to 3 damage.

## Client display extension (2026-07-22)

The "no client-side display" exclusion below was lifted by explicit user request: a creature
currently boosted by an anthem/lord or by +1/+1 counters now shows its *boosted* P/T on the board,
in green, instead of silently staying at its base/printed number the way it did through the first
slice above. Keyword grants are still not displayed (only P/T) — narrower than the original
exclusion's wording, a deliberate smaller cut.

- **New `AttrEffectivePT` card attribute** (`card_attributes.proto` ext 12, mirrored as
  `effective_pt` on `ServerInfo_Card`) — server-originated only, no client ever sends it via
  `Command_SetCardAttr`. Holds a plain "power/toughness" string equal to `AttrPT`'s current value
  whenever nothing is boosting the creature, so the client can tell "boosted" from "not" by a
  simple string comparison against its own `AttrPT` without needing an empty/null sentinel.
- **`Server_Game::recomputeEffectivePT(controller, ges)`** (`server_game.{h,cpp}`) folds
  `applyStaticEffects()` (unchanged from the slice above) together with a new dedicated
  `PLUS_ONE_ONE_COUNTER_ID` card counter (`card_effects.h`, id 6) into each of `controller`'s
  battlefield creatures' effective P/T, and pushes any that changed via the existing
  `setCardAttrHelper()`/`AttrEffectivePT` plumbing — no bespoke event type needed. Called from two
  trigger points, matching the "full live recompute" scope actually requested: whenever a card
  enters or leaves `controller`'s own table zone (`Server_Player::onCardBeingMoved()` — covers a
  creature or an anthem source itself showing up or leaving), and whenever a
  `PLUS_ONE_ONE_COUNTER_ID` counter changes there (`Server_AbstractPlayer::cmdSetCardCounter()`/
  `cmdIncCardCounter()`). Only `controller`'s own battlefield is ever recomputed — static abilities
  never affect anyone else's creatures (see `applyStaticEffects()`'s "you control" scoping), so a
  card entering/leaving *another* player's board never needs this. Combat math itself is
  unaffected: it still calls `applyStaticEffects()` fresh at its own three read sites rather than
  trusting this display-only broadcast value.
- **Dedicated +1/+1 counter, not one of the six generic lettered counter slots**: reusing a
  generic slot (`card_menu.cpp`'s `aAddCounter`/`aRemoveCounter` ids 0-5) would let a player's own
  generic-counter bookkeeping for some *other* purpose silently feed into the P/T-boost display —
  exactly the collision `DAMAGE_CARD_COUNTER_ID` (id 0) already has with generic slot "A" (a
  pre-existing wart this change doesn't attempt to fix). `PLUS_ONE_ONE_COUNTER_ID = 6` is its own
  id instead, with its own dedicated "Add/Remove +1/+1 counter" menu actions (fixed green icon, not
  a lettered/settings-configurable color) sitting outside the generic "Card counters" submenu.
- **Paint coloring** (`CardItem::paint()`, `card_item.cpp`): white/orange (unmodified/manually-set)
  is unchanged for a non-boosted creature; a boosted creature shows green if the boost is a clean
  buff (power and toughness both ≥ base, strictly greater in at least one) or falls back to the
  existing orange otherwise (a debuff, or a mixed +X/-Y static ability this codebase doesn't
  currently parse anyway).
- **Not done**: keyword-grant display (still server-only), and no recompute hook for a static
  ability changing on a card that's already on the battlefield without a zone move (e.g. `cmClone`/
  `cmFlip` on an already-in-play card) — only entering/leaving the battlefield and +1/+1-counter
  changes trigger a recompute, matching exactly what was scoped, not a general
  "watch everything" invalidation.

## Explicitly out of scope

Named deliberately, matching this fork's practice of documenting exclusions rather than leaving
silent gaps:

- **No creature-type/tribal restriction** ("Elves you control get +1/+1") — no creature-type
  modeling exists anywhere in this codebase.
- **No combined single-line phrasing** ("get +1/+1 and have vigilance") — the parser recognizes
  two independent line shapes only; a line combining both is simply not recognized.
- **No effects on opponents' creatures**, no conditional/until-end-of-turn static abilities, no
  cost-modifying static abilities (e.g. "Creatures you control cost {1} less").
- Outside of combat math, nothing else currently reads a creature's P/T or keywords server-side,
  so this closes the gap everywhere it currently matters — but a *future* mechanic that reads P/T
  or keywords elsewhere will need its own `applyStaticEffects()` call, same as combat's three
  sites; it isn't automatically covered.

Any further static-ability depth (creature types, combined phrasing, keyword-grant client display,
non-combat consumers) is a new, separate, explicit design decision — not a gap left over from this
slice. (Boosted-P/T client display itself is no longer in this list — see the dedicated section
above.)

## Testing

- `tests/card_ability/static_abilities_test.cpp`: parser conservative-shape coverage (both
  phrasings × both shapes, wrong-shape/extra-word lines correctly unrecognized, case-insensitivity,
  reminder-text stripping, the combined "and have" line and tribal-restricted line both correctly
  *not* matching, multiple qualifying lines on one card) plus `serialize`/`deserialize` round-trip
  cases including malformed-entry handling.
- `tests/rules/rules_engine_test.cpp`: `applyStaticEffects` cases — an `OthersYours` anthem boosts
  another creature but not itself; an `AllYours` anthem boosts its own source too; multiple anthems
  stack; a keyword grant extends the effective keyword set; an `OthersYours` keyword grant doesn't
  apply to its own source; no sources leaves base characteristics unchanged (regression safety —
  every pre-existing Stage B/C/D combat test still passes byte-for-byte, since none of them pass
  any static-ability sources).
- Full suite: `ctest --output-on-failure` — 23/23 test binaries pass, including the full existing
  Phase 7/8 suite unmodified. `./format.sh --cmake --branch master` clean (one unrelated
  cross-version reformat of `pending_ability_widget.{cpp,h}` — a file this change never touched —
  reverted per this fork's documented `format.sh` caveat in `CLAUDE.md`).
- **As of 2026-07-22, live-verified** via `.uitest/scenario.py run combat_gate` (see
  `phase8-combat.md`'s "Testing" section for the full scenario description — it was built to
  close this gap and the Stage C/D 2-player-combat gap in the same pass). Glorious Anthem
  ("Creatures you control get +1/+1.") and Darksteel Myr (base 1/1) both resolve onto the
  battlefield, Myr attacks unblocked, and the defending player's life drops by exactly 2, not 1 —
  proving `applyStaticEffects()`'s `AllYours` anthem case (which also boosts its own source, per
  this doc's "What's built" section) actually reaches `gatherCombatAttacks()`'s effective-power
  calculation over the real client/server wire, not just in the pure-function unit tests above.
  The original 2026-07-21 attempt's "did not complete" finding is superseded — CLAUDE.md's
  UI-testing section already explained the underlying cause (a version-notification dialog
  swallowing scripted clicks) was fixed after that attempt; this session's re-attempt against the
  same `cockatrice-build` container confirms it.
- **Client display extension, live-verified 2026-07-22** via the same `combat_gate` scenario plus
  manual follow-up clicks in the same running game: the client log shows
  `Event_SetCardAttr.ext { ... card_id: 1 attribute: AttrEffectivePT attr_value: "2/2" }` the
  instant Myr resolves onto a battlefield Glorious Anthem already occupies — proving
  `recomputeEffectivePT()`'s zone-move hook fires over the real wire, not just in a pure-function
  test. A screenshot at that point shows Myr's board box reading **2/2 in green**. Manually
  triggering the new dedicated "Add +1/+1 counter" menu action on the same Myr produced
  `Event_SetCardCounter.ext { ... counter_id: 6 counter_value: 1 }` immediately followed by
  `AttrEffectivePT attr_value: "3/3"`, and a follow-up screenshot confirms the board shows a green
  counter badge and **3/3** in green (base 1/1 + anthem's +1/+1 + one +1/+1 counter) — proving the
  counter-change recompute hook and the two boost sources compose correctly. This same menu
  addition grew the card context menu by one row, which shifted `combat_gate`'s own hardcoded
  right-click submenu coordinates (calibrated to the old menu height) by ~22px; `scenario.py`'s
  coordinates were updated to match and the scenario re-verified passing.
