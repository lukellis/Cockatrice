# §12: UI Design & Enhancements

**Status: partial**, growing incrementally alongside whichever phase needs a visible
surface. Not started: a 4-player grid layout, a dedicated mana pool widget. Below is
what exists.

## Command zone visual (§12.6)

See `phase3-command-zone.md` — a gold-bordered, upright `CommandZone` pile distinct
from deck/graveyard/exile, not the full commander-tax/partner display the design doc
sketches.

## Pending-ability / priority visualization panel (2026-07-18)

Phase 7 Stage 3 built a real, server-authoritative pending-ability stack and Phase 5
built real priority-passing — but neither was ever visible to players as *state*, only
as transient message-log lines. This closes that gap.

**Key finding that kept it small**: the server already broadcasts everything needed
(`Event_AbilityActivated`/`Event_AbilityResolved` with full payload, plus
`Event_PriorityChanged`) to every client unconditionally — this is purely a
**client-side visualization of state the wire protocol already carries, zero new
protocol messages**. The only real gap was that `GameEventHandler` discarded the
effect/target payload before it reached a signal, resolving only enough for the
existing dataless log lines.

**LIFO correctness without a unique ability id**: neither event carries a stable
identifier for "which pending entry." This is fine — the server's pending-ability
store is a genuine LIFO stack that always resolves exactly the top entry, so a
client-side mirror that pushes on `Event_AbilityActivated` (append) and unconditionally
pops its own top entry on any `Event_AbilityResolved` stays correctly in sync by
construction, no id needed.

**What's implemented**: a new `PendingAbilityWidget`
(`cockatrice/src/game_graphics/stack/pending_ability_widget.{h,cpp}`) — a header
("Priority: `<Player>`" / "Priority: no one", driven by the existing `priorityChanged`
signal) above a list (newest-on-top = top-of-stack), registered as a new dock in
`tab_game.{h,cpp}` alongside Card Info/Messages/Player List, including in
`actResetLayout()`'s default layout. A `describeEffect()` helper turns
`DrawCards`/`GainLife`/`LoseLife`/`DealDamage` into readable text — deliberately
generic for a card-zone target ("a permanent") rather than resolving the exact card
name, since that isn't reliably possible for a hidden/opponent's card.

**Deliberately excluded**: resolving/displaying the exact target card name; any
interaction affordances on the list (e.g. clicking an entry) — this is a read-only
visualization, not a new control surface.

## Testing

No dedicated GTest file — pure UI wiring (display text, dock registration, signal
plumbing) follows this fork's established "UI wiring gets live-verified, not unit
tested" precedent. Live-verified: activating two abilities without passing priority
produces two list entries in the correct LIFO order with correct human-readable text,
persists unchanged across an unrelated phase transition, and correctly drains as
priority passes resolve each entry. See `CLAUDE.md`'s UI-testing recipe.

## Counter UI cleanup + Storm counter (2026-07-21)

Player feedback: the command zone visually overlapped the avatar box (see
`phase3-command-zone.md`'s own "UI cleanup" section for that fix and the Commander
Tax badge's move onto the command zone), several per-player counters rendered as
identical unlabeled circles, and one ("storm", upstream Cockatrice's generic manually-
adjustable "Other" counter) was confusing and unused in practice.

**Storm count, replacing the old manual counter**: rather than just deleting it, it's
now a genuine (if informal — not a Commander rule) auto-tracked "spells cast this
turn" count, rendered read-only:
- **Client-side increment**: the server has no card-type awareness of its own, so the
  +1 is appended as an ordinary `Command_IncCounter` from
  `PlayerActions::gateManaCostForHandPlay()` (`cockatrice/src/game/player/player_actions.cpp`)
  — the same established point that already piggybacks real-mana-payment
  `Command_IncCounter`s (Phase 6) — for any non-land card leaving hand (rule 305.1
  already excludes lands from "casting" a spell).
- **Server-side reset**: `Server_Player::resetStormCount()`, called once per turn from
  `Server_Game::setActivePhase()` on entering `Rules::RulesEngine::UNTAP_PHASE`,
  mirroring the existing per-phase mana-pool-empty sweep's "loop every player" pattern.
- **Read-only rendering**: a new `AbstractCounter` constructor flag (`interactive`,
  defaulted `true` so pre-existing subclasses need no changes) suppresses both the
  TearOffMenu and click-to-increment. Storm originally rendered as plain text
  (a since-retired `TextCounter` widget) before pass 2 below gave it a real icon.

**Look-alike-counter labels**: `CounterPixmapGenerator::generatePixmap` only has
dedicated icons for w/u/b/r/g (plus, after pass 2 below, storm/poison) — per-commander
tax/damage still fall back to the same generic circle. `GeneralCounter::paint()` draws
a short text label under any such counter (abbreviated for the otherwise-long
per-commander name), and every counter has a hover tooltip with its full display name
(`AbstractCounter`'s constructor). Skipped for counters rendered outside the shared
counter column (`GeneralCounter::needsLabel()`, gated on `getShownInCounterArea()`),
since position alone already disambiguates those (Tax on the command zone, mana in the
pentagon, storm/poison in their own box — see pass 2 below).

Live-verified via `.uitest/scenario.py run counter_ui_gate` — see
`phase3-command-zone.md`'s Testing section for what it covers. No dedicated GTest
beyond `CommanderCounterNames::isTaxCounter()`'s pure-logic cases in
`tests/rules/rules_engine_test.cpp`, matching this doc's established precedent above.

## Counter UI pass 2: pentagon mana layout, icon-based Storm/Poison, damage row, dark theme (2026-07-21)

Further player feedback on the pass above: Storm's text ran into the left edge, several
counters wanted real icons instead of a generic circle, the command zone's card-count
badge collided with the Tax badge, mana couldn't visually go negative but the counters
themselves *could*, per-opponent commander damage deserved its own visible spot, and
the whole app looked too light. Also fixed: Tax badge moved from the command zone's
top-right corner to the bottom-right (the corner it was originally placed in collided
with nothing once the count badge below was removed, but bottom-right was the explicit
ask).

**`ManaPentagonWidget`** (`cockatrice/src/game_graphics/board/mana_pentagon_widget.{h,cpp}`):
the six mana counters (w/u/b/r/g/x) now sit at pentagon vertices in standard color-pie
order (clockwise from the top) with colorless smaller in the center, instead of
stacking individually in `PlayerGraphicsItem::rearrangeCounters()`. **Two corrections
after the first attempt**: an initial version drew each color's real WUBRG cost-pip SVG
(`icons/mana/{W,U,B,R,G}.svg`) oversized as a faint background watermark behind the
counter, and drew a bordered circle around the whole pentagon — both were live-verified
via a screenshot and rejected (the oversized pip bled past the counter's own edge as a
"halo," and the outer circle wasn't wanted at all). Fixed to: no border/background on
the pentagon widget itself (purely a layout container now); each mana counter
(`GeneralCounter::paint()`, `counter_general.cpp`) draws a small hand-drawn pictograph
evoking its color — sun (W), water drop (U), skull (B), flame (R), tree (G) — from new
transparent-background SVGs at `cockatrice/resources/icons/counter_glyphs/`, sized to
~62% of the counter's diameter (fully contained, no halo) and nudged up ~8% of its own
size from dead-center (these pointed-top/rounded-bottom silhouettes have more visual
weight low, so exact geometric centering reads as sitting low). Went through two more
color iterations after that: first each glyph in its own natural contrasting tone, then
(per explicit follow-up feedback) all five recolored to one flat dark gray (`#2a2a2a`,
matching the board's own dark-theme background tone) — including the skull, even though
its counter sphere (`b`, black mana) is itself dark enough that the contrast there is
genuinely weak; that trade-off was requested explicitly ("even skull") after seeing the
alternative, not an oversight. Colorless deliberately gets no glyph. The counters
themselves are still real `CounterState`-backed
`GeneralCounter`s (unchanged click/tooltip/menu behavior), just reparented onto the
pentagon and shrunk (radius 20→12) to fit. `PlayerGraphicsItem::counterAreaWidth` grew
55→90 to fit the ~88px-wide pentagon centered. Also swapped `w`'s sphere color from pale
yellow to true white (`counters/w.svg`) and gave colorless its own dedicated light-gray
sphere (new `counters/x.svg`, no longer redirected to the shared `general.svg` fallback
that per-commander tax/damage badges also use) so the two are visually distinct.

**Numeral readability + position fix**: once the dark-gray glyphs were in place, the
counter's numeral (drawn on top, same spot) turned out to have two real bugs, both
caught live via screenshot: (1) a flat black fill lost contrast against both the dark
glyph underneath and the `b` counter's own dark sphere; (2) it was drawn via
`painter->drawText(mapRect, Qt::AlignCenter, ...)` — `mapRect`, unlike the glyph/sphere
pixmaps drawn at local `(0,0)`, carries a position component from the item's actual
scene placement, so the numeral rendered visibly offset from the glyph it's supposed to
sit on. Fixed by switching to a local `(0,0)`-based rect (matching the pixmap/glyph
frame) and a white-fill-with-black-outline numeral (an 8-direction 1px-offset "shadow"
pass in black, then the fill in white) — readable against any sphere/glyph combination
underneath, not just some of them.

**Glyphs retired; numeral shrink-to-fit added**: with the outlined numeral now bold and
centered on the same spot as the glyph, the two mostly just overlapped -- the glyphs
were dropped entirely (`counter_glyphs/` assets, `cockatrice.qrc` entries, and the
draw call all removed) per explicit follow-up feedback, back to a plain colored sphere
plus numeral. Separately, live-testing what a large mana value actually looks like
(repeatedly clicking a mana counter up to 20, then 100) surfaced a real bug the fixed
font size had always had: at 100 the three digits visibly overflowed both edges of the
circle. Fixed with a shrink-to-fit loop (`QFontMetrics::horizontalAdvance`, stepping the
pixel size down while the text is wider than ~82% of the circle's diameter) plus a
smaller base size than before (`0.62x` the old multiplier) so even the common
single-digit case reads a bit less crowded.

**Icon-based Storm + Poison, grouped in `CounterGroupBox`**
(`cockatrice/src/game_graphics/board/counter_group_box.{h,cpp}`): both now render via
`GeneralCounter` (the old text-only `TextCounter` widget is retired/deleted) pointed at
new themed icons — `cockatrice/resources/counters/storm.svg` (a lightning bolt) and the
new `poison.svg` (an original stylized droplet, deliberately not a reproduction of
Wizards' trademarked Phyrexian-mana symbol), both with `_highlight` hover variants,
registered in `cockatrice.qrc`. `CounterGroupBox` is a small bordered-frame container
that reparents a handful of counter widgets into one horizontal row — generically
reusable, currently holding just these two. Storm stays `interactive=false` (read-only,
auto-tracked); Poison stays fully interactive as before. Both shrunk to radius 14.

**Command zone**: `CommandZone::paint()` no longer draws the card-count badge (a
Commander is either present or not — "CMD" plus the card art already says that, and it
only ever collided with the Tax badge); Tax now sits bottom-right.

**Mana floor clamp**: `Server_Player::cmdIncCounter()`/`cmdSetCounter()`
(`libcockatrice_network/.../server_player.cpp`) now clamp any counter in
`Rules::RulesEngine::manaCounterNames()` to a floor of 0 after mutating, regardless of
which command drove it there — mana has no real negative quantity, unlike life/poison/
damage counters which may legitimately need a manual correction below zero.

**Zone labels**: `PileZone::paint()` and `HandZone::paint()` draw a minimal, low-opacity
(alpha 100) label ("Deck"/"Graveyard"/"Exile"/"Hand") — short forms, not
`CardZoneLogic::getTranslatedName()`'s longer possessive phrasing ("their library").

**Commander-damage row**: `PlayerTarget` gains `addDamageCounter()` and a small nested
`DamageBadge` class (`cockatrice/src/game_graphics/player/player_target.{h,cpp}`) — one
compact numeric badge per opponent-commander-damage counter
(`CommanderCounterNames::isDamageCounter()`), laid out right-to-left along the avatar
box's top edge (above where the life badge sits, bottom-right), re-flowing as each
arrives. Deliberately kept inside the existing fixed 160×64 box rather than growing it
taller, since `PlayerGraphicsItem::initializeZones()` reads
`playerTarget->boundingRect().height()` to position the command zone/piles below it —
growing the box would have needed re-running that layout whenever a damage counter
first arrived, which this avoids entirely. Count is genuinely variable (as few as 1 in
a 2-player game; `Server_Game::doStartGameIfReady()` creates one per opposing commander
actually in play, not a fixed `playerCount - 1`) — never assume 3.

**Dark theme + font**: this fork's built-in `Default` theme
(`cockatrice/themes/Default/theme.cfg`) now defaults to `ColorScheme = Dark` instead of
`Light` — the app already had a fully-built dark palette (`palette-default-dark.toml`)
and a Light/Dark/System picker in Appearance Settings (`ThemeManager`/`ThemeConfig`),
it just wasn't the shipped default. That covers every native Qt widget (dialogs, docks,
menus) automatically. The custom-painted `QGraphicsItem` board bypasses `QPalette`
entirely, so its own fallback zone-background colors
(`HANDZONE_BG_DEFAULT`/`TABLEZONE_BG_DEFAULT`/`PLAYERZONE_BG_DEFAULT`/`STACKZONE_BG_DEFAULT`
in `theme_manager.cpp`) were darkened directly — same hues, not a different palette.
`main.cpp` now sets an app-wide sans-serif font (`QFont::setStyleHint(QFont::SansSerif)`,
no bundled font file needed), and the handful of board widgets that hardcoded
`QFont("Serif")` now just use the default constructor to inherit it.

**As of 2026-07-22, the commander-damage row is live-verified**, closing the gap noted
below at the time this was written — `.uitest/scenario.py run combat_gate` (see
`phase8-combat.md`'s "Testing" section) drives a real 2-player local-hotseat game, the
first scenario in this fork to do so, specifically because populating a damage counter
needs a real opponent (a 1-player game never creates one).

Live-verified (everything else) via `.uitest/scenario.py run counter_ui_gate`,
re-pointed at the pentagon's new mana-counter click coordinates (re-measured live after
the layout change — see the scenario file's own comment).

### CounterGroupBox dangling-pointer crash (found via the first 2-player live-verification, 2026-07-22)

`combat_gate`'s first run crashed the whole client (`pure virtual method called`,
`terminate called without an active exception`) moments after a 2-player local game's
`Event_GameStateChanged` finished processing — before either player had touched a card.
No 1-player scenario had ever hit this, for a simple reason: it only manifests once a
*second* real player exists, and this fork's Storm/Poison `CounterGroupBox` (the
"Counter UI cleanup" section above) had never been exercised with two players' worth of
counter-resync traffic in flight at once.

Root cause, confirmed with `gdb` against a `RelWithDebInfo` rebuild of this same Docker
container (the default `Release` build had no symbols for the crash frame):
`CounterGroupBox::addCounterWidget()` reparents each Storm/Poison `GeneralCounter` widget
onto itself and appends it to a plain `QList<AbstractCounter *> widgets`, but never
tracked when one of those widgets got destroyed independently — which happens routinely:
`PlayerLogic::processPlayerInfo()` (fired by every `Event_GameStateChanged`, and a game
start fires more than one) calls `clearCounters()` before rebuilding, and
`AbstractCounter::delCounter()` tears the old widget down via `deleteLater()`, an
*asynchronous* deletion. By the time that deferred delete actually ran, `widgets` still
held the now-dangling pointer, and `CounterGroupBox::boundingRect()` (called during Qt's
own scene bookkeeping as the widget's own destruction completed) dereferenced it —
calling a pure virtual `boundingRect()` on an object whose vtable had already unwound to
`QGraphicsItem`'s abstract base mid-destruction. Two real players' worth of setup traffic
made the race reliably hit; nothing about the logic itself was 2-player-specific.

Fixed in `counter_group_box.{h,cpp}`: `CounterGroupBox` now also inherits `QObject` (it
was previously a bare `QGraphicsItem`) so it can `connect()` to each widget's
`destroyed()` signal and remove it from `widgets` the moment that happens, instead of
only reacting whenever something else later happened to call `boundingRect()`/`paint()`.
A new `~CounterGroupBox()` destructor also explicitly `qDeleteAll()`s any widgets still
in the list *before* returning (mirroring `PlayerTarget::~PlayerTarget()`'s pre-existing
`delete playerCounter` comment) — letting `~QGraphicsItem()` auto-delete them later, after
this destructor's own body has returned and `CounterGroupBox`'s vtable has itself already
unwound, would hit the identical bug one level up. `PlayerTarget::~PlayerTarget()` got
the analogous fix for its own `damageBadges` list (the commander-damage row above), which
has the exact same `destroyed()`-into-`relayoutDamageBadges()`-into-`boundingRect()`
shape — not confirmed as the live trigger for this specific crash (the `gdb` backtrace
pointed at `CounterGroupBox`, not `PlayerTarget`), but the same defect pattern, fixed
proactively rather than waiting to hit it separately.
