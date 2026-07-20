# Phase 7: Card Ability System

**Status: Increments 1–2 (display-only parsing) done; a real execution engine, Stages
1–6, done at this fork's scope.** This is the single biggest scope item in the fork —
the design doc estimates it at 8–12 weeks, and it's the actual prerequisite Phases 5
(no general stack resolution), 6 (no real mana payment), and 8 (combat) were all built
around. Per this fork's standing rule, every stage below went through an explicit
plan-mode design pass and user sign-off before implementation — never assume the next
stage is authorized just because the previous one shipped.

Architecture precedent: Forge (GPL-3.0) and XMage (MIT) were checked for reusable
architecture — this fork's GPLv2-or-later license is compatible with both. Consistent
with Phase 5's precedent, later stages borrowed *algorithmic shape* from XMage where
relevant, never literal code. Community ability-data reuse (e.g. Forge's card-script
corpus) was evaluated as a separate, later decision, never pursued — their script
format needs its own interpreter before any of that data is usable here.

## Increments 1–2: display-only recognition (done)

Deliberately read-only — nothing here gates, automates, or enforces anything; it only
lets a player see what a card does without reading full rules text.

- **Increment 1 — evergreen keywords**: `CardKeywords::parse(const CardInfo&)`
  (`libcockatrice_card/.../card/ability/card_keywords.{h,cpp}`) recognizes the classic
  costless evergreen keyword set (Deathtouch, Flying, Vigilance, etc.) from rules text,
  shown as a "Keywords:" row in the card-info panel. Conservative: only lines that
  reduce entirely to a comma/"and"-separated list of recognized keywords match — this
  avoids false positives on granted/conditional abilities ("gains flying until end of
  turn") or abilities that merely mention a keyword.
- **Increment 2 — mana abilities**: `ManaAbilities::parse(const CardInfo&)`
  (`mana_abilities.{h,cpp}`) recognizes `{T}: Add <mana>.` lines — fixed-color
  (including repeated symbols, e.g. Sol Ring), comma/"or"-separated choice lines, and
  the "any color"/Command-Tower/Reflecting-Pool phrasings (simplified to "any of five
  colors, let the player pick," since real battlefield/commander-color-identity
  inspection is out of scope). A basic land's whole-line reminder text (e.g. Forest's
  `({T}: Add {G}.)`) is specifically unwrapped and matched, not stripped — the initial
  version's blanket reminder-text strip had missed every basic land, the single most
  common mana source in any real game.
  - **Client integration**: folded into the existing generic Tap/Untap context-menu
    action, not a separate menu item. Tapping a multi-selection with unambiguous mana
    abilities (e.g. five Forests) sends one batched command with no dialog; a selection
    containing a choice ability (e.g. Command Tower) opens a small picker dialog first,
    atomic on Cancel (no partial tap/mana state).

## Stages 1–6: a real execution engine

Each stage adds a genuinely new capability, listed roughly in dependency order. All
are zero-new-protocol except where noted, and all use the same "exact shape or skip"
conservative parsing philosophy as Increments 1–2 — a card whose ability doesn't
exactly match a recognized pattern is simply not recognized, never guessed at.

**Stage 1 — effect IR + narrow self-targeted activated abilities.** `EffectKind`/
`CardEffect`/`ActivatedAbility` (`card_effects.h`) — a flat-enum IR.
`ActivatedAbilities::parse()` recognizes three tap-only, self-targeted shapes:
`"{T}: Draw a card."` → `DrawCards`, `"{T}: You gain N life."` → `GainLife`,
`"{T}: You lose N life."` → `LoseLife`. Wired through the existing generic-Tap
interception (no new menu item): tapping a card with a recognized ability appends the
matching already-existing command (`Command_DrawCards`, or `Command_IncCounter` on the
`"life"` counter) to the same batch as the tap toggle. No targeting, no stack, no cost
beyond `{T}`.

**Stage 2 — targeting.** New `TargetKind` (`None`/`AnyTarget`) and `EffectKind::DealDamage`.
Parses `"{T}: Deal N damage to any target."` only (older `"target creature"`/`"target
player"` phrasings aren't matched). **New protocol**: `Command_ActivateTargetedEffect`
(`GameCommand` ext 1036 — **retired in Stage 3, never reuse this ext number**). Client
targeting is a new interaction mode, `AbilityTargetPicker`
(`cockatrice/src/game_graphics/board/ability_target_picker.{h,cpp}`), subclassing the
existing `ArrowItem` and reusing its hover/highlight visuals: right-click an ability,
drag to a permanent or player avatar to resolve, right-click/Escape/an invalid release
to cancel (sends nothing). Card targets need an explicit `(target_player_id,
target_zone, target_card_id)` triple server-side, since `card_id` is only unique within
one player's own zone. Per-card marked damage uses a new, semantics-free convention
constant, `DAMAGE_CARD_COUNTER_ID = 0` — Cockatrice's per-card counters have no name
field the way per-player counters do. No write-permission gate on cross-player
targeting (matches `Command_CreateArrow`'s existing leniency — affecting an opponent's
state via a legitimate ability is intended MTG behavior). Single-card selection only;
multi-select targeted-ability activation is unhandled.

**Stage 3 — a real resolvable pending-ability stack.** The single biggest behavioral
change of any stage: activating an ability now *defers* its effect until a priority
round exhausts with it still pending, instead of applying instantly. This is a new,
engine-level `Rules::PendingAbility` LIFO stack living inside `RulesEngine` (see the
Phase 1 doc) — **not** the pre-existing, purely-visual per-player `stack` zone, which
stays completely untouched. **`Command_ActivateTargetedEffect` (1036) is retired**,
replaced by one unified `Command_ActivateAbility` (`GameCommand` ext 1037) covering all
four `EffectKind` values — Stage 1's previously-instant `DrawCards`/`GainLife`/
`LoseLife` now defer the same way `DealDamage` always did. Two new events:
`Event_AbilityActivated` (`GameEvent` ext 2024, broadcast to every client for logging)
and `Event_AbilityResolved` (ext 2025, fired immediately before the real effect-applying
event). The server resolves autonomously — no round-trip to any client needed, since
the activating client already fully resolves `(kind, amount, target)` into primitives
at activation time (a finding that simplified the original roadmap's worry about
needing client-driven resolution). Real Magic's mana abilities (rule 605.3) correctly
stay untouched — they don't use the stack and still resolve immediately via the
separate `ManaAbilities` path. No fizzle/validity re-check at resolution (e.g. is the
target still there) — a known, accepted limitation. Activating an ability stays
ungated on holding priority; only *resolution timing* becomes priority-driven.

**Stage 4 — real mana cost payment.** The one deliberate exception to this fork's
usual non-blocking philosophy: **this actually gates and blocks an action**, not just
warns after the fact. Parses an optional mana-cost prefix on Stage 1–3's activated
abilities (`{2}{R}, {T}: ...` — plain digits and `W`/`U`/`B`/`R`/`G`/`C` symbols only;
`{X}`, hybrid, and Phyrexian symbols simply don't match the token shape and are
skipped). `Rules::RulesEngine::planManaPayment(cost, pool)` — pure, no I/O — pays
colored pips exactly first, then drains the generic remainder across
`manaCounterNames()`'s fixed w→u→b→r→g→x order (a deterministic simplification of real
Magic's player-choice-driven generic payment, same spirit as Command Tower's
production-side simplification). If the plan fails, the whole activation — tap
included — never happens, plus an explanatory dialog. **Zero new protocol**: the client
resolves the cost and sends ordinary negative-delta `Command_IncCounter`s batched
alongside the existing tap/activate commands; the server needs no new logic, exactly as
trusting of this payment batch as it already is of every other client-computed command.
Mana-*ability* costs (as opposed to activated-ability costs) stay free-only — real
costed mana abilities are rare enough to be out of scope. Single-card activation only,
same boundary as Stage 2.

**Stage 5 — triggered abilities.** The first effects that fire with **no player
action** at all. New `TriggerKind` (`EntersBattlefield`/`Dies`) and `TriggeredAbility`
IR. `TriggeredAbilities::parse()` matches a card's *own literal printed name*
self-referentially (real Oracle text isn't templated with a placeholder) against two
shapes: `"When/Whenever <Name> enters the battlefield, <effect>."` and `"When <Name>
dies, <effect>."`, where `<effect>` is one of Stage 1's three untargeted shapes
(`DealDamage`/targeted triggers are explicitly out of scope — auto-popping a targeting
picker mid drag-and-drop is its own unsolved interaction-design problem). The detection
hook is client-side, not server-side (the server has zero access to card rules text at
all — `Server_Card` stores only name/id/counters/tapped/pt): `PlayerEventHandler::eventMoveCard()`,
the one real chokepoint every `Event_MoveCard` broadcast passes through, gated on
`getPlayer()->getPlayerInfo()->getLocal()` so only the controlling player's own client
reacts, regardless of who performed the move. Sends the same untargeted
`Command_ActivateAbility` Stage 3 already built — zero new protocol. No APNAP
ordering logic (moot at this fork's scope — each card move is its own independent
event, so simultaneous cross-player triggers off one event don't arise here).

**Stage 6 — combat: a declare-attacker slice.** Research found real combat-damage
calculation is out of reach for a session-sized effort: arrows are 100% untyped (no
way to distinguish a declared attack from any other pointer), power/toughness is a
free-form display string everywhere with no numeric representation anywhere in this
codebase, and this fork has never automated creature death even for the simpler life ≤
0 case. What *was* already 90%-built and just needed finishing: `Server_Card::attacking`
was fully wired through the protocol/attribute/event pipeline
(`AttrAttacking`) but had **zero producer and zero consumer** — inert infrastructure.
Stage 6 finishes that last mile only: a checkable "Declare as attacker" context-menu
toggle (rule 508.1a/508.1f — tap unless the creature has Vigilance, checked via
Increment 1's `CardKeywords::parse()`), a visual red-orange outline
(`CardItem::paint()`, same pattern as the existing "doesn't untap" magenta outline),
and auto-clearing when leaving the combat-phase range
(`Rules::RulesEngine::isCombatPhase()`, phases 4–8, checked as a *range* rather than
one specific transition since phases can be freely jumped). Zero new protocol —
reuses the existing `Command_SetCardAttr`. **Declaring/removing blockers, any
combat-damage calculation, and any creature-death/graveyard automation remain
explicitly out of scope** — this is the boundary of what's in reach without a real
numeric P/T model and a real attacker↔blocker link, neither of which exist anywhere in
this codebase. Any further combat depth is a new, separate design decision, not a next
stage of this one.

## Deliberately excluded across all of Phase 7

Real spell casting from hand (instants/sorceries have no parser — only the narrow
activated/triggered-ability whitelists above participate in the pending-ability
stack); multi-select targeted/costed-ability activation; a general stack for arbitrary
card effects (only this fork's own recognized ability shapes resolve through it);
mana-ability costs; player choice for which color pays a generic cost; fizzle checks.

## Testing

One GTest file per parser/IR piece under `tests/card_ability/` (keywords, mana
abilities, activated abilities, triggered abilities) plus `tests/rules/
rules_engine_test.cpp` for the pending-ability/mana-payment engine logic — all
conservative-parsing boundary cases (wrong shape, plural phrasing, reminder text,
case-insensitivity, multiple qualifying lines) are covered per parser. UI/menu wiring
is not unit-tested (matches this fork's established precedent: parsing logic gets
GTest coverage, UI wiring gets live verification) — every stage was live-verified via
local servatrice + Xvfb + a real client, using either real fixture cards or small
throwaway cards added to `.uitest/sample_cards.xml` for the session and removed
afterward. See `CLAUDE.md`'s UI-testing recipe for the reproducible process; this file
intentionally omits the session-by-session debug-log transcripts in favor of the
scope/architecture facts above.
