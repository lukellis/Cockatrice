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
| §3 Phase 4: Turn Structure Enforcement | Not started | Out of current scope — see design philosophy above. |
| §3 Phase 5: Priority & Stack System | Not started | Out of current scope. |
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
- [ ] Final summary to user: what's implemented, how to build/run, known limitations
