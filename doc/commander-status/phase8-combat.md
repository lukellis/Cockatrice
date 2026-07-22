# Phase 8: Combat System

**Status: declare-attacker (Phase 7 Stage 6) done; Stages A/B/C/D (attack targeting,
blocker declaration, automated combat damage/creature death, deathtouch/trample/
indestructible, first strike/double strike) done at this fork's current scope.**

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
  for both new attributes. Stage 6's original `AttrAttacking` toggle was originally left
  deliberately unrestricted (unchanged shipped behavior) — **as of 2026-07-21 it no
  longer is** (see below): this fork's turn-structure enforcement pass
  (`doc/commander-status/phase4-turn-structure.md`) closed that gap alongside
  phase-order and casting-timing enforcement.
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

## Stage D: first strike, double strike

Closes the first named exclusion from Stages A/B/C's list below — the one the previous
writeup called out as needing "a whole new turn-structure change, not just a math
change." It doesn't get one.

- **The design tradeoff**: Cockatrice's phase list (`cockatrice/src/game/phase.cpp`'s
  `Phases::phases[]`) is a single hardcoded array, shared by client and server, with no
  phase enum — `RulesEngine`'s own phase-index constants
  (`DECLARE_BLOCKERS_PHASE`/`COMBAT_DAMAGE_PHASE`/`CLEANUP_PHASE`) are literal integers
  documented as coupled to that array's ordering. A background survey done before this
  stage confirmed inserting a true "First Strike Combat Damage" step between Declare
  Blockers and Combat Damage would reindex every phase from Combat Damage onward across
  both the client and server: `phase.h`'s `phaseTypesCount`, the phase-button toolbar's
  position-indexed button list and `getLongPhaseName()` switch
  (`cockatrice/src/game_graphics/phases_toolbar.cpp`), a *second*, independently
  hardcoded `CLEANUP_PHASE = 10` in `game_event_handler.cpp` (not derived from
  `RulesEngine`'s copy — an easy desync target), and several hardcoded phase-index
  lists in `rules_engine_test.cpp`. Given this fork's standing preference for reusing
  existing generic mechanisms over new structural surface area (see `CLAUDE.md`'s
  design principles), that blast radius was judged not worth it for one keyword
  pairing — the choice (and the tradeoff above) was reviewed and signed off on
  explicitly rather than picked unilaterally.
- **What's built instead**: rule 510.4's two combat-damage sub-passes are modeled as
  two calls to `RulesEngine::calculateCombatDamage()` from *within* the existing
  Combat Damage step — no new phase, no reindexing, no client-visible turn-structure
  change. `Server_Game::resolveCombatDamage()` now: (1) captures which attackers are
  declared blocked *before* either sub-pass runs (see below for why), (2) gathers
  combatants fresh via the new `gatherCombatAttacks()` helper and runs a first-strike
  sub-pass (`RulesEngine::CombatDamageStep::FirstStrike`) through the now-shared
  `applyCombatDamageResult()` helper (life loss, damage marking, lethal-creature
  graveyard moves — the exact logic Stage B/C already had, just extracted so it can run
  twice), then (3) re-gathers combatants (a second, independent
  `gatherCombatAttacks()` call) and runs a regular sub-pass
  (`CombatDamageStep::Regular`). A first-strike kill is already reflected on the board
  (moved to the graveyard) by the time the second gather runs, so it naturally drops out
  — the same "re-derive from live board state" idiom Stage B/C's single pass already
  used, just invoked twice.
- **`RulesEngine::CombatCreature`** gained `hasFirstStrike`/`hasDoubleStrike` bools,
  populated in `gatherCombatAttacks()` from `Server_Card::hasKeyword()` — no protocol
  changes needed at all, since Stage C's `AttrKeywords`/`keywords` round-trip already
  carries every recognized `CardKeywords::evergreenKeywords()` entry, not just
  Deathtouch/Trample/Indestructible; "First strike"/"Double strike" were already
  flowing to the server, just unread until now.
- **`RulesEngine::participatesInStrikeStep(hasFirstStrike, hasDoubleStrike, step)`**: a
  first-strike or double-strike creature acts in the `FirstStrike` step; everything
  else acts in the `Regular` step; double strike acts in both (rule 702.7b/702.4).
  `RulesEngine::calculateCombatDamage()` gates each side of an exchange independently —
  an attacker's assignment across its blockers is gated on the *attacker's* own
  participation this step, and each blocker's contribution to the damage dealt back to
  the attacker is gated on *that blocker's* own participation, so e.g. a first-strike
  attacker can kill a non-first-strike blocker in the first-strike sub-pass without
  taking any damage back that same sub-pass. `step` defaults to `Regular`, so every
  pre-Stage-D caller (no first/double strike creature anywhere in combat) is
  byte-for-byte unaffected — the first sub-pass is simply a no-op in that case, and the
  second sub-pass alone reproduces the old single-pass behavior exactly.
- **Rule 509.1h, the real complication of splitting into two sub-passes**: an attacker
  remains "blocked" even after every creature blocking it has been removed from combat
  (e.g. killed in the first-strike sub-pass) — a double-strike or trample attacker
  whose only blocker just died must *not* be treated as unblocked in the second
  sub-pass (no free hit to the player without trample; with trample, *all* its power
  tramples through, since there's no blocker left to assign lethal damage to first).
  This is a genuine part of implementing first/double strike correctly, not an
  artifact of the sub-pass approach — real Magic has the identical rule between its
  own two steps. Modeled via a new `CombatAttack::blocked` bool, decoupled from
  `blockers.isEmpty()`: `Server_Game::resolveCombatDamage()` computes the declared-
  blocked set once, up front (before any deaths happen), and both `gatherCombatAttacks()`
  calls set `blocked` from that fixed set rather than re-deriving it from the
  (possibly-thinned) live blocker scan. `calculateCombatDamage()` treats an attack as
  blocked if either `blocked` is true or `blockers` is non-empty, so every pre-Stage-D
  caller that never sets `blocked` (defaults false) is unaffected.

## Attack-timing enforcement (2026-07-21)

Part of the same turn-structure enforcement pass documented in
`phase4-turn-structure.md`, closing this doc's own Stage A note above.
`Rules::RulesEngine::canDeclareAttacker(phase, controllerIsActivePlayer)` — rule
508.1a simplified: `phase == DECLARE_ATTACKERS_PHASE && controllerIsActivePlayer` —
mirrors `canDeclareBlocker()`'s shape and is enforced in the same
`Server_AbstractPlayer::cmdSetCardAttr()`:

- **`AttrAttacking`**: declaring an attacker (`attrValue == "1"`) now requires
  `canDeclareAttacker()`; clearing it (`"0"`) stays ungated, matching the server's own
  auto-clear path (which doesn't go through this command at all) and today's shipped
  behavior for un-declaring.
- **`AttrAttackTarget`**: actually setting a target (not clearing to `-1`) now also
  requires `canDeclareAttacker()`, alongside the pre-existing ownership check — so a
  targeted declare (the "Declare as attacker, targeting..." submenu, which batches both
  attributes together) can't half-succeed if the phase/active-player check fails.

Untapped-state and vigilance/summoning-sickness legality are deliberately **not**
checked here (unlike `canDeclareBlocker()`'s tapped check) — this fork doesn't enforce
summoning sickness anywhere, and adding an untapped-only check without it would be a
partial, inconsistent rule; scoped strictly to the phase/active-player timing the user
asked for.

## Explicitly out of scope

Named deliberately, matching this fork's practice of documenting exclusions rather than
leaving silent gaps — each of these is a real, separate follow-up, not an oversight:

- Protection, damage prevention/replacement effects.
- Planeswalker/battle damage — only players can currently be attacked or take combat
  damage.
- Player-chosen damage-assignment order among multiple blockers — declaration order
  (ascending card id) stands in.
- Non-numeric P/T creatures (`*`, `X/X`) — left out of automatic damage calculation
  entirely, not defaulted to 0.
- Retroactively converting Phase 9's advisory life ≤ 0 / poison / commander-damage
  warnings into automatic loss — the restriction is lifted so this *could* happen, but
  it wasn't part of Stages A/B/C/D and needs its own explicit sign-off.

Any further combat depth beyond the above is a new, separate, explicit design decision
— not a gap left over from these stages.

## Testing

- `tests/rules/rules_engine_test.cpp`: `canDeclareAttacker` (Declare-Attackers-only,
  active-player-only) alongside `canDeclareBlocker`, `parseNumericPT`, and
  `calculateCombatDamage` (unblocked/blocked/multi-blocker/no-target/non-numeric-P/T
  cases) — pure logic, no server dependency. Stage C added cases for deathtouch's
  minimal-lethal-assignment, trample's leftover-to-player, the two combined, and
  `isLethallyDamaged`'s deathtouch/indestructible branches. Stage D added
  `participatesInStrikeStep`'s truth table (vanilla/first-strike/double-strike ×
  FirstStrike/Regular step), an unblocked attacker of each keyword combination dealing
  damage in the right step(s), a first-strike attacker killing a non-first-strike
  blocker without taking damage back in the same sub-pass (and the blocker still
  hitting back in the following regular sub-pass, if it survived), and the
  `CombatAttack::blocked`-with-empty-`blockers` "remains blocked" case (rule 509.1h)
  both with and without trample.
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
- **Stage C's own live-verification attempt (predating the version-notification-dialog
  fix noted in CLAUDE.md) found what looked like a real infrastructure gap**: game
  creation, per-player deck auto-load, and card drawing all worked correctly over the
  real client/server wire protocol in this fork's Docker container, but clicking or
  dragging a hand card to play it never registered. Not chased further at the time,
  since it looked orthogonal to Stage C's actual logic (already covered above by unit
  tests).
- **As of 2026-07-22, that gap is closed — 2-player combat is now live-verified end to
  end** via `.uitest/scenario.py run combat_gate` (`.uitest/combat_p1.cod`/
  `combat_p2.cod`, added this session), confirming the original Stage C blocker really
  was the version-dialog issue CLAUDE.md's UI-testing section describes, not a deeper
  2-player-specific limitation. The scenario: casts Glorious Anthem + Darksteel Myr in a
  real local-hotseat 2-player game, declares Myr as an attacker via the
  "Declare as attacker, targeting..." submenu (the plain "Declare as attacker" checkbox
  item is a target-less legacy toggle that never sets `AttrAttackTargetPlayerId` —
  `gatherCombatAttacks()` silently no-ops on it, confirmed live), and asserts the
  defending player's life drops by exactly 2 once Combat Damage auto-resolves — i.e.
  Myr's own 1 power *plus* Anthem's static +1/+1, closing `phase8-static-abilities.md`'s
  matching live-verification gap in the same pass. This covers the core Stage
  A/B (declare + auto-resolve) wiring for one unblocked, indestructible, non-deathtouch/
  trample/first-strike attacker; it does **not** re-verify deathtouch/trample/first-
  strike/double-strike/blocking live (those stay GTest-only, unchanged from before) —
  `.uitest/sample_cards.xml`'s Ghor-Clan Rampager/Baleful Strix/Order of Leitbur/Boros
  Swiftblade fixtures are still there, unused, for whenever that's picked up.
  **A real, previously-latent bug was found and fixed getting here**: this session's
  first attempt crashed the client (`pure virtual method called`) immediately on
  2-player game setup — every prior scenario was 1-player and never exercised a second
  human opponent's commander-damage badge, the one path that hits this. See
  `ui-enhancements.md`'s "CounterGroupBox dangling-pointer crash" section for the fix.
  A second bug specific to *this* scenario, not a latent one: battlefield card
  left/right screen position isn't tied to card identity (it follows whichever order the
  two hand-cast clicks happened to land in, which isn't stable either) — resolved by
  reading each card's server-assigned table grid-x from the client log rather than
  guessing from screen position or rendered color (both were tried and found unreliable
  live; see the scenario's own comments).
