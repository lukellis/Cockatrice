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

## Explicitly out of scope

Named deliberately, matching this fork's practice of documenting exclusions rather than leaving
silent gaps:

- **No client-side display of boosted P/T or granted keywords** — a card's shown P/T/keyword row
  stays base-only. This slice is combat-math-only, the same kind of bounded first cut as Phase 8
  Stage 6's declare-only attacker toggle.
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

Any further static-ability depth (creature types, combined phrasing, client display, non-combat
consumers) is a new, separate, explicit design decision — not a gap left over from this slice.

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
