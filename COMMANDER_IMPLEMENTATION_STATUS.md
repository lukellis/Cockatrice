# Commander Rules Fork — Implementation & Build Status

Working notes for the `cockatrice-commander` fork (adds MTG Commander/EDH rules
enforcement to Cockatrice). This file tracks implementation + build/test progress
across sessions so work can resume cleanly if a session is interrupted (e.g. OOM
in the sandbox). See the "Commander-Rules Fork of Cockatrice" section of
[README.md](README.md) for the user-facing feature description.

## Origin / goal

Fork of [Cockatrice/Cockatrice](https://github.com/Cockatrice/Cockatrice), adding
enforcement of the official [Commander/EDH rules](https://mtgcommander.net/index.php/rules/),
loosely following the phased design in
[jeffyche/fun-stuff's mtg-commander-rules-engine doc](https://github.com/jeffyche/fun-stuff/blob/master/design-docs/mtg-commander-rules-engine.md),
mirrored in-repo at
[`doc/design-docs/mtg-commander-rules-engine.md`](doc/design-docs/mtg-commander-rules-engine.md)
(that doc proposes a much larger 9–13 month roadmap; what's implemented here is a
real, scoped-down subset — deck validation + command zone + tax/damage counters +
life default — not the full stack/priority/combat engine).

### Tracking against the design doc's phases (§3)

| Design doc phase | Status here | Notes |
|---|---|---|
| §3 Phase 1: Foundation & Build Setup | **Diverged** | No separate `libcockatrice_rules/` library was created (doc §3 Phase 1); new logic instead lives directly in existing `libcockatrice_card`, `libcockatrice_models`, `libcockatrice_network` per Cockatrice's existing structure. Also stayed on Cockatrice's original branding/protocol rather than forking to an independent ecosystem (doc §0) — this fork intentionally stays protocol-compatible with upstream (zero `.proto` changes) rather than diverging, since that was assessed as lower-risk for a fork this size. Commander is a selectable game type (doc §3 Phase 1 item 4: done). |
| §3 Phase 2: Commander Deck Validation | **Done** | `CommanderDeckValidator` (100-card count, singleton, color identity, legality), wired into both server-side game-start and a live client-side deck-editor status label. |
| §3 Phase 3: Command Zone & Commander Tracking | **Done** | Command zone, commander tax counter, per-opponent commander-damage counters, client-side lethal-damage warning. Matches doc's proposed `CommanderState` fields (cast count, damage-dealt-to map) conceptually, implemented as counters rather than a dedicated struct, consistent with how Cockatrice already tracks all other numeric game state. |
| §3 Phase 4: Turn Structure Enforcement | **Partial** | Automatic untap-all and automatic draw at the untap/draw steps, gated to Commander games only (`Server_Game::isCommanderGame()`). Deliberately **not** implemented: phase-order enforcement (doc's "phase advancement requires explicit action or timer" — players can still freely jump phases, matching Assisted Mode's non-blocking philosophy), discard-to-hand-size at end step. See "Phase 4" section below for full detail. |
| §3 Phase 5: Priority & Stack System | **Partial (simplified)** | Real priority-passing (round-robin, protocol messages added) researched against XMage's `GameImpl.playPriority()`; no real stack (LIFO resolution of card effects) since that needs a card-rules engine this fork doesn't have. "Everyone passes" advances the phase instead of resolving a stack object. See "Phase 5" section below. Server-side + tested; client UI not yet wired up. |
| §3 Phase 6: Mana System | Not started | Out of current scope. |
| §3 Phase 7: Card Ability System | Not started | Out of current scope. |
| §3 Phase 8: Combat System | Not started | Out of current scope. |
| §3 Phase 9: State-Based Actions | Not started | Only the "21 commander damage" SBA is covered, as an advisory warning rather than automatic loss. |
| §12: UI Design & Enhancements | Not started | No 4-player grid layout, stack/priority visualization, mana pool widget, or combat UI. Command zone has a basic panel (§12.6) but not the full commander-tax/partner display proposed. |

This fork's scope corresponds almost exactly to the design doc's own **§8
"Recommended Starting Point"** (deck validation, commander damage tracking via the
existing counter system, 40 starting life) — i.e. it deliberately validates the
architecture with the doc's own lowest-risk/highest-value slice before attempting
anything from Phases 4–9, rather than partially implementing the harder phases.

### Design philosophy (why it's built this way)

- Cockatrice is a manual/self-officiated "physical simulator" — no automated mana
  payment, combat damage resolution, turn structure, or state-based-action loss
  detection (not even life ≤ 0 today). So Commander tax/damage are implemented as
  **auto-created but manually-incremented counters** (same pattern as existing life
  totals), with an **advisory, non-blocking** client-side warning at lethal
  commander damage — not a hard auto-loss. This matches the design doc's own
  "Assisted Mode" philosophy.
- Reused Cockatrice's existing generic **Zone** (`Server_CardZone`/`CardZoneLogic`)
  and **Counter** (`Server_Counter`/`CounterState`) abstractions wherever possible
  instead of inventing new protocol messages — the entire diff is **protocol-compatible,
  zero `.proto` file changes**, which keeps risk low.
- Deck-construction rules (100-card singleton, color identity, legality) are
  enforced via the existing per-card legality-flag infrastructure in
  `DeckListModel`, extended rather than replaced.

## Phase 4: Turn Structure Automation

Implemented in a follow-up session after tasks 1–8 (which cover deck validation +
command zone/tax/damage, i.e. design doc §3 Phases 2–3) were complete and fully
tested. This is the first genuinely *automated* gameplay behavior in this fork —
everything before this was either passive counters (manually incremented, like
life totals) or client-side advisory warnings. Scoped narrowly given that higher
risk profile:

- **What it does:** `Server_Game::setActivePhase()` now triggers, Commander games
  only:
  - Entering the **Untap step** (phase index 0): untaps everything the active
    player controls, reusing the exact same `Server_AbstractPlayer::setCardAttrHelper()`
    call the existing manual "untap all" action already uses — including its
    existing respect for `AttrDoesntUntap`-flagged permanents.
  - Entering the **Draw step** (phase index 2): draws 1 card for the active
    player via the existing `Server_Player::drawCards()`, **except** the very
    first draw step of a strict **two-player** game's starting player (rule
    103.8a). Verified against the actual rules text rather than trusting the
    design doc's own phrasing ("skip on turn 1 in multiplayer") — rule 103.8c
    says multiplayer games never skip the first draw step; only 2-player and
    Two-Headed Giant games do. This fork's Commander default is 4-player
    free-for-all, so in practice this skip essentially only matters for Duel
    Commander (2-player).
  - The decision of *what* to do (`CommanderPhaseAutomation::{None,UntapActivePlayer,DrawForActivePlayer}`)
    is factored into a pure static method, `Server_Game::phaseAutomationFor(phase, turnNumber, playerCount)`,
    kept separate from the mechanical/side-effecting part specifically so the
    rules logic (including the skip-first-draw arithmetic) is unit-testable
    without needing a fully constructed, participant-registered game.
  - Phase index 0/2 meaning is only assumed *coupled to the client's phase
    ordering* (`cockatrice/src/game/phase.cpp`) — there's no shared server/client
    phase enum today (a pre-existing architectural gap, not something this
    fork introduces). Documented in a code comment at the point of use.
- **Correctness gap found and fixed along the way:** the existing command
  zone/tax/damage hooks (`Server_Player::setupZones()`, `onCardBeingMoved()`,
  `Server_Game::doStartGameIfReady()`) were **not actually gated by game format
  at all** — they ran for every game unconditionally, only "self-gating" by
  coincidence of deck content (a card happened to be set as the deck's banner
  card). Added `Server_Game::isCommanderGame()` (checks the room's selected
  game-type labels for a "commander" substring, mirroring the existing
  client-side check in `dlg_create_game.cpp`, which was refactored to share the
  same `CommanderRules::gameTypeLabelIsCommander()` helper) and gated the
  command-zone redirect in `setupZones()` on it. This matters because
  `DeckList::bannerCard` can be set as a purely cosmetic "cover card" on
  non-Commander decks too — before this fix, such a deck's cover card would
  have been incorrectly redirected into a command zone instead of the library.
- **Deliberately not done:** phase-order enforcement (a player can still freely
  jump to any phase in any order — Assisted Mode is about warnings, not
  blocking, and enforcing strict order is a bigger UX change affecting all
  play styles) and discard-to-hand-size at the end step (design doc's third
  Phase 4 bullet; left for a future increment).
- **Testing:** `tests/movecard_tests/commander_turn_structure_test.cpp` (12
  cases) — pure logic tests for `phaseAutomationFor()` (all phase/turn/player-count
  combinations, including the 103.8a/103.8c skip-first-draw distinction),
  integration tests for `isCommanderGame()` against real `Server_Game`/`Server_Room`
  objects (including an out-of-range game-type-index robustness case), and
  direct verification of the reused `setCardAttrHelper`/`drawCards` mechanisms
  (untap respects `AttrDoesntUntap`, draw moves the correct card). Plus 2 new
  cases in `commander_rules_test.cpp` for `gameTypeLabelIsCommander()`.
  **Not covered:** a true end-to-end test that `setActivePhase()` fires
  automation for a *registered* game participant — `Server_Game::addPlayer()`
  requires a live `Server_AbstractUserInterface`, and there's no lighter-weight
  seam to inject a participant for testing. Compensated for by testing the
  decision logic and the reused mechanisms separately, both directly.
- **Pre-existing bug found (not part of this fork's diff, but worth knowing for
  future sessions):** constructing and destructing multiple `Server_Game`/
  `Server_Room` instances as stack locals in sequence within one test process
  segfaulted deterministically (reproduced 3/3 plain runs, 0/5 under gdb — a
  classic timing/memory-layout-dependent signature). **Root cause found:**
  `Server_Game::~Server_Game()` (`server_game.cpp`) calls `deleteLater()` on
  itself at the very end of its own destructor body — undefined behavior
  (posting a deferred self-deletion event for an object that's already being
  destructed). Every other existing test using these classes
  (`reverse_card_move_test.cpp`) only ever constructs **one** instance per
  process, so this had never been hit before. Out of scope to fix as part of
  this Commander-focused diff (touches core game-lifecycle code with no
  Commander-specific angle). Workaround used in tests: heap-allocate
  `FakeServer`/`Server_Room`/`Server_Game` and deliberately never free them
  (the test process is short-lived) rather than use stack locals, which avoids
  exercising the buggy destructor path.

## Phase 5: Priority Passing (simplified stand-in for the Stack System)

Design doc §3 Phase 5 ("Priority & Stack System") calls for a real `Server_Stack`
(LIFO resolution) and `Server_PriorityManager`, with new protocol messages for
casting spells and resolving stack objects — a genuine card-rules engine
(parsing and executing arbitrary card effects), which is squarely out of reach
for this fork (no card-scripting engine exists here, and building one is its
own multi-month project — see design doc §3 Phase 7's own 8–12 week estimate
for "Card Ability System", a prerequisite for real stack resolution).

**Researched before implementing:** looked at how existing open-source MTG
rules engines structure this, specifically
[Forge](https://github.com/Card-Forge/forge) and
[XMage](https://github.com/magefree/mage) (both GPL-licensed, community-built,
full rules-enforcing engines — see
[cgomesu.com's comparison](https://cgomesu.com/blog/forge-xmage-mtg/)). Fetched
and read XMage's `GameImpl.playPriority()` (`Mage/src/main/java/mage/game/GameImpl.java`)
directly from `github.com/magefree/mage` for the canonical algorithm shape:
round-robin priority passing starting from the active player; if a player acts,
passed-flags reset and the round restarts; once everyone passes in succession,
resolve the top stack object if the stack is non-empty (then reset and
continue), otherwise end the priority round (advance to the next step/phase).
This is the real CR 117.3b/117.4/405.5 priority algorithm, implemented in Java
against a full card-effects engine neither of which exist in this C++/Qt fork.
**Borrowed the algorithmic shape, not the code** (different language, and
Cockatrice's architecture — server-authoritative state with fully-manual client
actions — is fundamentally different from XMage's simulate-and-execute model).

**What's implemented — deliberately simplified from the above:**
- New protocol messages (first time this fork has touched `.proto` files —
  every prior phase stayed protocol-compatible; this genuinely needs a new
  command/event pair): `Command_PassPriority` (`GameCommand` ext 1035) and
  `Event_PriorityChanged` (`GameEvent` ext 2023, `priority_player_id` field).
  Both purely additive — no existing message changed.
- `Server_Game` tracks `priorityPlayerId` and `priorityPassedBy`, Commander
  games only (`isCommanderGame()`). Resets to the active player at the start
  of every phase (in `setActivePhase()`, alongside the existing untap/draw
  automation). `Server_Player::cmdPassPriority()` validates the caller
  currently holds priority (or is a judge), then calls
  `Server_Game::advancePriority()`.
- `advancePriority()` finds the next non-passed, non-conceded player in turn
  order via the pure, unit-tested `Server_Game::nextPriorityPlayer()`. If
  everyone eligible has passed, **this is where the simplification from real
  Magic happens**: rule 117.4 would resolve the top of the stack here; instead,
  since this fork's `STACK` zone (already existed, per-player, purely a manual
  visual aid with no resolvable objects — see the original architecture
  research) has nothing this fork can execute, "everyone passed" simply
  **advances to the next phase** (wrapping past the last phase into the next
  turn via the existing `nextTurn()`). Players who want to represent casting/
  resolving something via the Stack zone still do so manually, exactly as
  before this feature existed — priority-passing doesn't block or require that.
- **Priority is advisory, not enforcement**: holding or not holding priority
  does **not** gate any other existing command (moving cards, tapping,
  drawing, etc. all remain available to any player at any time, as today).
  This mirrors the Assisted Mode philosophy used throughout this fork and
  avoids a much bigger, riskier behavioral change (blocking actions based on
  priority) that couldn't be verified without live multiplayer play-testing.
- Tests: `Server_Game::nextPriorityPlayer()` pure logic (8 cases: advance,
  wrap-around, skip-passed, skip-conceded, all-ineligible, solo player, empty
  order, never-returns-self) plus a `cmdPassPriority` gating test
  (game-not-started rejection — see the file for why deeper integration
  testing isn't lightweight here, same limitation as Phase 4's tests).
- **Not implemented:** client-side UI (a "Pass Priority" button, priority
  indicator). The feature is protocol- and server-complete and tested, but not
  yet exposed to players in the GUI — see design doc §12.3's "Priority
  Indicator" mockup for what that would eventually look like. Deliberately
  scoped out of this increment: new Qt widget/interaction work is harder to
  verify without live play-testing than the server logic (which has real
  automated test coverage), and the priority command is fully inert/unused by
  existing clients until wired up, so leaving it server-only is safe.

## Implementation status

| # | Task | Status |
|---|------|--------|
| 1 | Clone Cockatrice repo into `~/projects/cockatrice-commander` | done |
| 2 | Research official Commander rules (mtgcommander.net) + design doc | done |
| 3 | Map Cockatrice architecture (zones, counters, deck model, protocol) | done |
| 4 | Commander deck construction validation (100-card singleton, color identity, banned list) | done |
| 5 | Command zone + commander tax (+2 per prior cast, rule 903.9) | done |
| 6 | Commander damage tracking + 21-damage loss warning (rule 704.5g) | done |
| 7 | Commander game defaults (40 life, 4-player multiplayer) | done |
| 8 | Rebrand fork (README/attribution) + **build verification** | **done — see Build status below** |

### Files added/changed

New files:
- `libcockatrice_card/libcockatrice/card/format/commander_rules.{h,cpp}` — color
  identity computation (regex over mana cost + rules text, reminder text stripped),
  `canBeCommander`, `isWithinColorIdentity`, `formatUsesColorIdentity` (gates the
  Commander-family format list: commander, duel, brawl, standardbrawl, oathbreaker,
  paupercommander, predh).
- `libcockatrice_models/libcockatrice/models/deck_list/commander_deck_validator.{h,cpp}` —
  `CommanderDeckValidator::validate(const DeckListModel&)` → `Result{isValid, errors}`;
  checks commander legality, 100-card total, per-card format legality, per-card color identity.
- `libcockatrice_utility/libcockatrice/utility/commander_counter_names.h` — shared
  counter-naming helpers (`tax()`, `damage()`, `isDamageCounter()`,
  `commanderNameFromDamageCounter()`, `LETHAL_COMMANDER_DAMAGE = 21`).

Modified (see `git diff` for full detail):
- `libcockatrice_utility/.../zone_names.h` — added `COMMAND = "command"`.
- `libcockatrice_network/.../server_player.cpp` — command zone setup/redirect,
  commander tax counter creation + increment in `onCardBeingMoved()`.
- `libcockatrice_network/.../server_game.cpp` — cross-player commander-damage
  counter creation in `doStartGameIfReady()`.
- `cockatrice/src/game/player/player_logic.{h,cpp}` — client-side COMMAND zone.
- `cockatrice/src/game_graphics/player/player_graphics_item.{h,cpp}` — command
  zone visual + lethal-commander-damage `QMessageBox::warning` hook.
- `cockatrice/src/game/zones/card_zone_logic.cpp` — "Command Zone" display name.
- `cockatrice/.../dlg_create_game.cpp` — 40 life / 4 players default for Commander game type.
- `cockatrice/.../deck_editor_deck_dock_widget.{h,cpp}` — live red/green deck-legality
  status label wired to `CommanderDeckValidator` (added after review caught that the
  validator existed but nothing called it).
- `libcockatrice_models/.../deck_list_model.cpp` — `isCardNodeLegalForFormat()` /
  `refreshCardFormatLegalities()` now also enforce color identity, gated by
  `CommanderRules::formatUsesColorIdentity()` (NOT by "banner card is set" alone —
  `bannerCard` can be a cosmetic cover card on non-Commander decks too; gating on
  that alone was an initial bug, caught before shipping).
- `*/CMakeLists.txt` — register new source files.
- `README.md` — fork documentation + attribution + known limitations.

Standalone verification aid (not part of the shipped diff):
`/tmp/color_identity_algo_test.cpp` — non-Qt regex mirror of the color-identity
algorithm, compiled with plain g++ and passed, to validate the trickiest new logic
before writing the Qt-dependent version (which can't be unit-compiled without Qt).
Recreate it if useful; it does not need to ship.

### Known limitations (already documented in README)

Single designated commander only (no Partner/Background pairing). Color identity
doesn't yet pull in a double-faced card's back face. Command zone has no dedicated
context menu (graveyard/exile do). No automated stack/priority/combat engine —
out of scope per the design doc's own phasing.

## Build status — where things stand and how to resume

**Latest: Phase 4 (turn structure automation) changes rebuilt and verified clean
end-to-end** — `libcockatrice_network_server_remote` (with its new `libcockatrice_card`
link dependency), `servatrice`, and `cockatrice` all build with zero errors. Full
`ctest` suite (17/17 test executables) passes.

**Sandbox constraints that matter here:** 2 vCPUs, ~1.9 GiB RAM, **no swap**,
Amazon Linux 2023. `/tmp` is **tmpfs** (RAM-backed, ~955 MiB cap) — writing large
things there (like a Qt6 SDK) eats directly into system RAM. This combo is the
likely root cause of the OOM kill that ended the prior session mid-build.

**What's been proven so far:**
- AL2023's dnf repos don't ship Qt6, so a prebuilt Qt6 was fetched via `aqtinstall`
  (`pip3 install --user aqtinstall`), Qt 6.7.0 `linux_gcc_64`, plus the
  `qtwebsockets` and `qtmultimedia` addon modules (Cockatrice's CMake needs these
  beyond the base install; `qtbase` itself is the implicit base, not a `-m` module).
- `cmake -DCMAKE_PREFIX_PATH=<qt6 prefix> -DCMAKE_BUILD_TYPE=Release -DWITH_SERVER=ON -DWITH_CLIENT=ON -DWITH_ORACLE=OFF -DTEST=OFF ..`
  configured successfully once all Qt6 modules were present.
- Built successfully with `make -j2 <target>`, confirming **no compile errors** in
  any new/modified file, for:
  `libcockatrice_card`, `libcockatrice_utility`, `libcockatrice_models_deck_list`
  (pulls in `commander_deck_validator.cpp` + `deck_list_model.cpp` changes), plus
  their dependencies (`libcockatrice_protocol`, `libcockatrice_deck_list`,
  `libcockatrice_rng`, `libcockatrice_interfaces`).
- `libcockatrice_network` (all 5 targets: `server_remote`, `server_local`,
  `client_remote`, `client_local`, `client_abstract` — covers `server_player.cpp` /
  `server_game.cpp`, the least "copy an existing pattern" / most novel code) now
  **built successfully with zero errors/warnings**, confirmed in the follow-up
  session after fixing the OOM cause (see below).
- `cockatrice` client binary (the heaviest target — full GUI, MOC-heavy) now
  **built successfully with zero errors**, `-j1`, no OOM. Binary at
  `build/cockatrice/cockatrice`. **Task 8 is complete: this fork is fully
  compile-verified**, not just manually reviewed.
- The Qt6 install directory under `/tmp/qt6install` did **not** survive the sandbox
  restart after the OOM (tmpfs) — re-fetched via aqtinstall to a disk-backed path instead.

**Root cause of the OOM (diagnosed in the follow-up session) and fix applied:**
`/tmp` in this sandbox is tmpfs (RAM-backed, ~955 MiB cap) — installing the ~1.4 GiB
Qt6 SDK there directly consumed system RAM, on top of `make -j2` running two
`cc1plus`/MOC processes, with **no swap configured**, on a 2 vCPU / 1.9 GiB box.
Fix: Qt6 now installed to `~/qt6install` (disk-backed, not tmpfs); added a 1.5 GiB
swap file (`/swapfile`, `sudo swapon`); building with `-j1` instead of `-j2`, one
target at a time, checking `free -h` between steps.

**Fork/remote note:** `origin` is the real upstream `Cockatrice/Cockatrice` repo —
not writable by this user. Work is pushed instead to a fork at
`github.com/lukellis/Cockatrice`, branch `commander-rules` (remote name `fork`).

**Recommended approach to resume without OOMing again:**
1. Install Qt6 to a **disk-backed** path (e.g. `~/qt6install`), not `/tmp`, so it
   doesn't compete with build processes for RAM.
2. Add a swap file for headroom (no swap currently exists) — protobuf-generated
   code and Qt MOC-heavy GUI translation units are the memory-hungry outliers here.
3. Build with `-j1` rather than `-j2` — 2 parallel `cc1plus` processes on ~1.9 GiB
   with no swap is tight, especially once past small library targets into the
   network/protocol and full client build.
4. Keep building **one target/library at a time** (as the prior session was doing)
   rather than a bare `make` of everything, and check `free -h` between steps.
5. Order: finish `libcockatrice_network` → `servatrice` (server binary, no GUI) →
   `cockatrice` (client binary, heaviest due to GUI/MOC).

**Remaining checklist for task #8:**
- [x] Re-provision Qt6 (disk-backed path) + swap file
- [x] Finish `libcockatrice_network` build, confirm clean
- [x] Build `servatrice` (server), confirm clean — required `sudo dnf install -y
      pulseaudio-libs fontconfig freetype` first (Qt6's prebuilt `libQt6Multimedia`/
      `libQt6Gui` shared libs need these system runtime libs at link time; AL2023
      doesn't have them by default). Binary at `build/servatrice/servatrice`.
- [x] Build `cockatrice` (client), confirm clean — binary at `build/cockatrice/cockatrice`.

**Task 8 is done.** All new/modified code is compile-verified end to end
(libraries, server, and client), not just manually reviewed.

## Testing

The repo has a real GTest suite under `tests/` (CI runs it via
`.ci/compile.sh --test` → `ctest`), which the original implementation work did
**not** use — it only had a standalone non-Qt algorithm mirror
(`/tmp/color_identity_algo_test.cpp`, not part of the shipped diff). Closing that
gap:

- `./format.sh --cmake --branch master` run and clean (see Lint below) — not
  testing per se, but part of this repo's CI checks.
- New test suite added at `tests/commander/` (registered in `tests/CMakeLists.txt`):
  - `commander_rules_test.cpp` — pure algorithmic tests for `CommanderRules::*`
    (color identity from colors/mana-cost/rules-text, reminder-text exclusion,
    hybrid symbols, `canBeCommander`, `formatUsesColorIdentity`). No card database
    needed; constructs `CardInfo` directly via `CardInfo::newInstance()`.
  - `commander_deck_validator_test.cpp` — integration test exercising the real
    `CommanderDeckValidator::validate()` + `DeckListModel` + `CardDatabaseManager`
    pipeline (not mocked), using a dedicated fixture at `tests/commander/data/cards.xml`
    with its own `<formats>` block (100-card, singleton, banned-list, basic-land
    exception) — isolated from `tests/carddatabase/data/cards.xml` so it doesn't
    perturb that fixture's hardcoded card/set counts. Covers: valid 100-card deck,
    missing/illegal/unknown commander, text-granted commander eligibility, wrong
    card count, color identity violations (both directions), banned cards, singleton
    violations, and the basic-land exception.
- Both new test binaries build clean and **all 28 cases pass**:
  `commander_rules_test` (17/17) and `commander_deck_validator_test` (11/11),
  run directly (`build/tests/commander/commander_rules_test`,
  `.../commander_deck_validator_test`). One compile fix needed along the way:
  `CardDatabase::loadCardDatabases()` returns `void` (status is a separate
  `getLoadStatus()` call), not the `LoadStatus` the test initially assumed.
- Full suite (`cmake -DTEST=ON` + `make -j1` all test targets + `ctest --output-on-failure`
  from `build/`): **16/16 tests pass, 0 failed.** All pre-existing tests pass
  unmodified — the Commander changes to `deck_list_model.cpp` didn't need any
  existing test updated. Full list: `dummy_test`, `expression_test`,
  `clamped_arithmetic_test`, `test_age_formatting`, `password_hash_test`,
  `server_card_counter_test`, `server_counter_test`, `deck_hash_performance_test`,
  `card_zone_algorithms_test`, `carddatabase_test`, `filter_string_test`,
  `commander_rules_test`, `commander_deck_validator_test`,
  `loading_from_clipboard_test`, `reverse_card_move_test`, `parse_cipt_test`.
- **Testing is done and green.** This closes the gap identified earlier: the fork
  now has real GTest coverage for the new Commander logic, verified against both
  its own tests and the full pre-existing suite, matching this repo's actual CI
  practice (`.ci/compile.sh --test` → `ctest`) instead of the original
  standalone-non-Qt-mirror approach.

## Lint

`./format.sh --cmake --branch master` was run and applied (clang-format +
cmake-format) to all changed files — purely cosmetic line-wrap/whitespace, no
semantic changes. AL2023's dnf-provided clang-format is v15, which doesn't support
this repo's `.clang-format` `RemoveSemicolon` key (needs 16+); installed
clang-format 22 and cmake-format via `pip3 install --user clang-format cmake-format`
(lands in `~/.local/bin`, ahead of dnf's `/usr/bin/clang-format` on `PATH`) to match
what CI's lint workflow actually checks for.

**Note:** `format.sh`'s default `--branch` is `origin/master` (the real upstream
remote). Since this fork's local `master` may not exactly match `origin/master`,
running `format.sh` without `--branch master` can pick up unrelated pre-existing
files that merely differ from upstream — happened once with root `CMakeLists.txt`
(a pre-existing GCC16 workaround comment, reformatted and reverted, not part of
this fork's diff). Always pass `--branch master` explicitly in this repo to scope
to just this fork's actual changes.

## UI testing capability (new)

This sandbox can now actually run and screenshot the real `cockatrice` GUI
headlessly (Xvfb + Qt xcb platform + XTest input simulation via
`python-xlib`), not just build it. Full recipe, missing-library fixes, and
the reusable driver script (`.uitest/uitest.py`, gitignored) are documented
in `CLAUDE.md`. This immediately paid off: driving the real deck editor
(create a Commander deck, add a commander, read the live validation tooltip
off a screenshot) caught a real double-counting bug in
`CommanderDeckValidator::validate()` that every GTest case had missed,
because the tests set the banner card via the API directly without also
adding it to the main deck list — not how the real UI actually builds a
Commander deck (the Banner Card picker is populated *from* the main deck
list). Fixed and reverified visually; see the commit for detail. Prefer this
kind of live verification for any further client-visible Commander feature
work, not just unit tests.

## Where this stands / next steps

As of this writing: design doc §3 Phases 2–3 done, Phase 4 (turn structure)
partially done (auto-untap/auto-draw), Phase 5 (Priority & Stack System)
partially done in simplified form (real priority-passing, no stack
resolution — see "Phase 5" section above). Everything compile- and
test-verified: `servatrice` + `cockatrice` build clean, 17 test executables /
78+ individual test cases pass. All pushed to `fork/commander-rules`.

Work initially stopped before Phase 5 pending explicit user sign-off, since it
was the first phase needing actual `.proto` changes (breaking the
zero-protocol-changes streak of Phases 2–4). The user then authorized
continuing, asked for research into existing codified rules engines to borrow
from, and to document simplifications/assumptions — which is what the "Phase
5" section above records (XMage's `GameImpl.playPriority()` as the borrowed
architectural reference, with the stack-resolution part explicitly scoped out
as needing a card-rules engine this fork doesn't have).

**Deliberately not attempted, and why:**
- **Real stack resolution / card ability execution** (design doc Phases 6–8:
  mana system, card ability parsing, combat). These need an actual card-rules
  engine (parse rules text, execute effects, targeting, replacement effects)
  — the same conclusion Forge/XMage's own scale of effort confirms (community
  projects, years of work). Not attempted at any level here; would need a
  real scoping/design conversation, not a unilateral implementation.
- **Client-side UI for priority-passing.** Protocol and server logic are
  complete and tested; no "Pass Priority" button or priority indicator exists
  in the GUI yet. Scoped out because Qt widget/interaction changes are harder
  to verify without live play-testing than server logic with automated tests,
  and leaving the command unwired is safe (inert until a client sends it).

**Reasonable next increments, roughly in order of size/risk:**
1. Client-side UI to actually use priority-passing (a button + indicator,
   per design doc §12.3's mockup) — makes tonight's Phase 5 server work
   actually playable.
2. Discard-to-hand-size at the end step (remaining piece of design doc
   Phase 4) — similar shape to the untap/draw automation already added.
3. Anything from Phase 9 (State-Based Actions) that fits the existing
   counter/warning pattern, similar to how lethal commander damage is already
   handled as an advisory warning rather than automatic loss.
4. Phase 6+ (mana/abilities/combat) — needs a card-rules engine and real
   design discussion before implementation starts; not a reasonable
   unilateral next step at any scope.
