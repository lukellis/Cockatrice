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
[jeffyche/fun-stuff's mtg-commander-rules-engine doc](https://github.com/jeffyche/fun-stuff/blob/master/design-docs/mtg-commander-rules-engine.md)
(that doc proposes a much larger 9–13 month roadmap; what's implemented here is a
real, scoped-down subset — deck validation + command zone + tax/damage counters +
life default — not the full stack/priority/combat engine).

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
| 8 | Rebrand fork (README/attribution) + **build verification** | **in progress — see Build status below** |

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
- Full `cockatrice` client binary and `servatrice` server binary: **not yet attempted**.
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
- [ ] Build `cockatrice` (client), confirm clean
- [ ] If full build proves infeasible in sandbox resource budget, honestly report
      to the user which parts are compile-verified vs. only manually reviewed
      against existing working patterns
- [ ] Final summary to user: what's implemented, how to build/run, known limitations
