# Phase 8: Combat System

**Status: declare-attacker (Phase 7 Stage 6) done; Stages A/B/C (attack targeting,
blocker declaration, automated combat damage/creature death, deathtouch/trample/
indestructible) done at this fork's current scope.**

Design doc §3 Phase 8, the design doc's own largest single-phase estimate. This fork's
combat work started as Phase 7 Stage 6 (the roadmap's two efforts converged there,
since finishing Phase 7's card-ability engine is what let a keyword like Vigilance
matter mechanically) — see `phase7-card-abilities.md`'s "Stage 6" section for that
slice's own research/scope/writeup. This doc covers everything since.

## Design-principle context

Through Stage 6, this fork held to "advisory/manual-only, never hard automation" (see
CLAUDE.md's old Design Principles text, still visible in git history). **As of
2026-07-20, by explicit user direction, that restriction is lifted** — see CLAUDE.md's
current Design Principles section. Stages A and B below are this fork's first
Commander mechanic built as genuine automated enforcement rather than an advisory
counter/warning, and are the reason the restriction was lifted when it was: Stage 6
alone (a bare "attacking: yes/no" toggle) couldn't get any further without real
target-tracking, a real P/T model, and real damage math — all three would have stayed
permanently out of reach under the old advisory-only framing.

## Stage 6 recap: declare-attacker

A real "declare as attacker" toggle (rule 508.1a/508.1f: tap unless vigilance), a
visual indicator, and auto-clearing when combat ends — built almost entirely from
pre-existing-but-unused infrastructure (`Server_Card::attacking` was already fully
wired through the protocol but had no producer or consumer), zero new protocol
messages.

## Stage A: attack-target selection + blocker declaration

Closes the two structural gaps Stage 6's own writeup named as blockers to going
further:

- **No attack-target-player tracking existed at all.** `attacking` was a bare bool —
  unlike the `attach_player_id`/`attach_card_id` relationship `serverinfo_card.proto`
  already modeled for a different relationship, attacking a specific opponent (out of
  Commander's 2–6 possible opponents) was never recorded server-side. New:
  `AttrAttackTarget` (a `CardAttribute` enum value) + `attack_target_player_id`
  (`ServerInfo_Card`) + a `Server_Card`/`CardState`/`CardItem` int member. Declaring an
  attacker via the new "Declare as attacker, targeting..." context-menu submenu (reuses
  the existing opponent-picker pattern from "Reveal to...") sets both `AttrAttacking`
  and `AttrAttackTarget` in one batched command. The original bare `aAttacking` toggle
  (Stage 6, no target) is kept unchanged for backward compatibility with its keyboard
  shortcut — it just means that specific attacker won't participate in Stage B's
  automated damage (no target to hit), same graceful-degradation treatment as a
  non-numeric P/T creature.
- **No attacker↔blocker link existed.** Arrows are 100% untyped/cosmetic (confirmed by
  reading `server_arrow.h`/the three arrow `.proto` files) — never fit for this. New:
  `AttrBlocking` (attr_value packs `"attackerPlayerId:attackerCardId"` as a single
  string, the same idiom `AttrPT` already uses for `"power/toughness"`) +
  `blocked_player_id`/`blocked_card_id` (`ServerInfo_Card`) + matching `Server_Card`
  members. Declared via a new "Declare as blocker..." submenu listing every
  currently-attacking opponent creature.
- **Real server-side legality, for the first time in this fork's combat code.**
  `Rules::RulesEngine::canDeclareBlocker()` (phase == Declare Blockers, blocker
  untapped, blocker not itself attacking) is enforced in
  `Server_AbstractPlayer::cmdSetCardAttr()`, along with ownership/target-exists checks
  for both new attributes. Stage 6's original `AttrAttacking` toggle is deliberately
  left unrestricted (unchanged shipped behavior).
- **Zero new protocol commands or events.** Both new attributes reuse the existing
  generic `Command_SetCardAttr`/`Event_SetCardAttr`/`ServerInfo_Card` mechanism already
  used for `attacking`/`tapped`/`pt` — just two new `CardAttribute` enum values and
  matching `ServerInfo_Card` fields.

## Stage B: numeric P/T, automated combat damage, automated creature death

- **`Rules::RulesEngine::parseNumericPT()`**: parses `Server_Card::getPT()` (already
  "effective" — the client's existing +1/+1-counter menu bakes counters directly into
  this string, there's no separate counter to add in) into `(power, toughness)`, or
  `std::nullopt` if either half isn't a plain integer. A characteristic-defining `*` or
  `X/X` creature is left **entirely out** of automatic combat-damage calculation (for
  manual resolution) rather than silently mis-calculated as 0/0.
- **`Rules::RulesEngine::calculateCombatDamage()`**: pure, unit-tested (see
  `tests/rules/rules_engine_test.cpp`) function implementing rule 510 simplified —
  unblocked attacker with a target deals its power to that player; a blocked attacker
  splits its power across its blockers in declaration order, each blocker capped at its
  own toughness (no trample — leftover power is wasted, not passed on), before all of
  an attacker's blockers deal their combined power back to it. An attacker with no
  target (Stage 6's original bare toggle) deals no damage even unblocked.
- **`Server_Game::resolveCombatDamage()`**: the server-side glue, called from
  `setActivePhase()` on entering the Combat Damage step. Gathers every attacking
  creature + declared blockers across all players (declaration order approximated as
  ascending card id — real Magic's damage-assignment order is an attacking-player
  choice this fork doesn't model), runs `calculateCombatDamage()`, then applies the
  result:
  - Life loss via the same "life"-named `Server_Counter` lookup `applyPendingAbility()`
    (Phase 7 Stage 3) already uses.
  - Damage marked via the **pre-existing `DAMAGE_CARD_COUNTER_ID`** card counter
    (`card_effects.h`) — Phase 7 Stage 5's targeted damage-dealing trigger already
    established this convention, so no new protocol surface was needed for damage
    marking at all.
  - A state-based check: any creature whose marked damage ≥ its (numerically parsed)
    toughness is moved to its owner's graveyard via the existing
    `Server_AbstractPlayer::moveCard()`, called directly server-side (no client command
    in flight) the same way the pre-existing attach-card repositioning code already
    does.
- **Damage clears at the End/Cleanup step** (rule 514.2), not when combat itself ends —
  a damaged survivor is still vulnerable through Second Main. This is why the clear is
  gated on `RulesEngine::CLEANUP_PHASE` specifically, not `isCombatPhase()`'s range like
  `AttrAttacking`'s own auto-clear.

## Stage C: deathtouch, trample, indestructible

Closes three of Stage B's named exclusions — the three that resolve entirely within the
existing single Combat Damage step, unlike first/double strike (needs a whole new
turn-structure step) or protection (also touches blocking/targeting legality), which
stay excluded (see below).

- **The real gap: the server has zero card-database/oracle-text access.** `Server_Card`
  only exposed `getName()`. The only existing keyword parser,
  `CardKeywords::parse(const CardInfo&)` (already recognizes Deathtouch/Trample/
  Indestructible, used since Stage 6 for the client-side Vigilance tap-on-attack
  decision), had only ever been called from client code — the server never saw the
  result, and combat-damage math is rightly server-authoritative.
- **The fix reuses the `AttrPT` idiom rather than adding card-DB access to the server.**
  A new `AttrKeywords` card attribute (`CardAttribute` enum, `card_attributes.proto`) +
  `keywords` field on `ServerInfo_Card` round-trip the same way `pt`/`AttrPT` already
  do. On the *sending* side, a new `keywords` field on `CardToMove` (`command_move_card.proto`),
  `Command_CreateToken` (`command_create_token.proto`), and `Command_FlipCard`
  (`command_flip_card.proto`) — siblings to each message's existing `pt` field — carry
  `CardKeywords::parse()`'s comma-joined result wherever the client already populates
  `pt` from a `CardInfo` (`playCard`, `playCardToTable`, `cmMoveToTable`, related-card
  token creation, `cmClone`, and revealing a face-down card via `cmFlip`). The server
  trusts this exactly as much as it already trusts client-sent `pt` (no new trust
  model) and stores it on `Server_Card` as `keywordsString`, exposed via
  `hasKeyword(name)`.
- **`RulesEngine::CombatCreature`** gained `hasDeathtouch`/`hasTrample` bools, populated
  in `Server_Game::resolveCombatDamage()` from `Server_Card::hasKeyword()`.
- **`RulesEngine::calculateCombatDamage()`** changed in two ways: the amount needed to
  be "lethal" when assigning damage to a blocker is now 1 (not the blocker's full
  toughness) when the attacker has deathtouch (rule 702.2b); and any power left over
  after all blockers have been assigned lethal damage now goes to the defending player
  if the attacker has trample (rule 702.19b), instead of being wasted. The result also
  now tracks `deathtouchDamaged`: which damaged cards had at least one point of that
  damage come from a deathtouch source (needed because a deathtouch source makes even 1
  marked damage lethal, regardless of toughness).
- **New pure helper `RulesEngine::isLethallyDamaged()`** replaces the inline
  `markedDamage >= toughness` check in `resolveCombatDamage()`'s state-based death
  check: true if `markedDamage >= toughness`, *or* any of that damage came from a
  deathtouch source and `markedDamage > 0`; indestructible short-circuits straight to
  false regardless (rule 702.12b — never destroyed by damage).
- Damage-assignment order is still declaration order (ascending card id), same
  deterministic stand-in as Stage B — deathtouch/trample change *how much* damage is
  needed per blocker, not the order blockers are assigned to.

## Explicitly out of scope

Named deliberately, matching this fork's practice of documenting exclusions rather than
leaving silent gaps — each of these is a real, separate follow-up, not an oversight:

- First strike, double strike (no separate combat-damage steps — a real turn-structure
  change, not just a math change), protection, damage prevention/replacement effects.
- Planeswalker/battle damage — only players can currently be attacked or take combat
  damage.
- Player-chosen damage-assignment order among multiple blockers — declaration order
  (ascending card id) stands in.
- Non-numeric P/T creatures (`*`, `X/X`) — left out of automatic damage calculation
  entirely, not defaulted to 0.
- Retroactively converting Phase 9's advisory life ≤ 0 / poison / commander-damage
  warnings into automatic loss — the restriction is lifted so this *could* happen, but
  it wasn't part of Stages A/B/C and needs its own explicit sign-off.

Any further combat depth beyond the above is a new, separate, explicit design decision
— not a gap left over from these stages.

## Testing

- `tests/rules/rules_engine_test.cpp`: `canDeclareBlocker`, `parseNumericPT`, and
  `calculateCombatDamage` (unblocked/blocked/multi-blocker/no-target/non-numeric-P/T
  cases) — pure logic, no server dependency. Stage C added cases for deathtouch's
  minimal-lethal-assignment, trample's leftover-to-player, the two combined, and
  `isLethallyDamaged`'s deathtouch/indestructible branches.
- `tests/movecard_tests/commander_turn_structure_test.cpp`: `Server_Card::setAttribute`
  round-tripping for `AttrAttackTarget`/`AttrBlocking`, and the same
  before-game-starts gating precedent `cmdPassPriority`/`cmdActivateAbility` already
  have.
- `Server_Game::resolveCombatDamage()`'s own phase-transition wiring is **not**
  integration-tested (same documented limitation as Stage 6's own
  attacking-clear-on-phase-exit and Phase 4's untap/draw automation: registering a real
  participant needs a live `Server_AbstractUserInterface`, not lightweight in this
  harness) — verify live per this fork's standard Xvfb + local-`servatrice` recipe
  (CLAUDE.md) before calling a change like this done.
- **Stage C's own live-verification attempt found a real, separate infrastructure gap,
  not a bug in this stage's code**: this fork's built-in local-hotseat "Local Game"
  feature (`debug.ini`'s `[localgame]` auto-start + per-player `deck\Player N=` auto-load,
  `MainWindow::startLocalGame()`/`TabGame::addLocalPlayer()`) is the only way to get two
  players' creatures onto one board without a second real human (the EC2 runbook's own
  documented requirement) — game creation, per-player deck auto-load, and card drawing
  all worked correctly over the real client/server wire protocol in this fork's Docker
  container. But clicking or dragging a hand card to play it never registered — traced
  as far as confirming `CardItem::playCard()`'s `owner->getPlayerInfo()->getLocalOrJudge()`
  gate (or something upstream of it) silently no-ops in this specific headless,
  window-manager-less Xvfb setup, for reasons still unconfirmed (menu/keyboard input
  *does* work once the window has been given focus by an initial click — this is
  specifically about `QGraphicsScene` card-item clicks/drags). Not chased further since
  it's orthogonal to Stage C's actual logic (already covered above by unit tests) and
  would be a genuinely separate test-infrastructure investigation. `.uitest/sample_cards.xml`
  now has a Trample creature (Ghor-Clan Rampager) and an Indestructible creature
  (Darksteel Myr) alongside the pre-existing Deathtouch one (Baleful Strix) ready for
  whenever this gap gets resolved.
