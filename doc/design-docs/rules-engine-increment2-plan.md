# Rules Engine — Increment 2 plan (Commander-only client + module migration)

Durable copy of the approved Increment 2 plan so a fresh session can resume without
re-deriving it. Context and the overall architecture decision live in
[`../../COMMANDER_IMPLEMENTATION_STATUS.md`](../../COMMANDER_IMPLEMENTATION_STATUS.md)'s
"Design & Implementation Review — 2026-07-16" section. Increment 0 (build setup) and
Increment 1 (server `RulesEngine` foundation) are **done and pushed** (commit
`2eea118`); this is the next increment.

## Goal

Make the **client** wholly Commander-only: remove every client `isCommanderGame()`
gate (behavior always-on), strip all non-Commander UI, and migrate the Commander
modules into `libcockatrice_rules` so they share the engine's home.

## Decisions already taken (do not re-litigate)

- **Foundation-first**, not enforcement-first (enforcement = Increment 3+, future).
- **Fully Commander-only client** — remove non-Commander game types from create-game
  AND collapse the deck editor to Commander-only (the larger, upstream-heavier option
  was explicitly chosen by the user).

## Step 1 — de-gate client behavior (always-on)

Remove the `isCommanderGame()` gate at each site so the behavior always runs (verify
line numbers, they may drift):
- `cockatrice/src/game_graphics/player/player_graphics_item.cpp` (~lines 38, 230, 247)
  — empty-library-draw / life≤0 / poison SBA + lethal warnings.
- `cockatrice/src/game/game_event_handler.cpp` (~line 568) — Cleanup discard-to-hand
  warning.
- Priority button: `cockatrice/src/game_graphics/phases_toolbar.{h,cpp}`
  (`setCommanderGame()`, ~line 286/118) and its caller
  `cockatrice/src/game/tabs/tab_game.cpp` (~line 1165) — make the Pass Priority button
  always shown; delete `PhasesToolbar::setCommanderGame()`.
- Delete `GameMetaInfo::isCommanderGame()` (`cockatrice/src/game/game_meta_info.h`,
  ~line 94) and fix the now-**dangling doc comment** at ~line 90 that references
  `Server_Game::isCommanderGame()` (that server method was deleted in Increment 1).
- Clean up stale Commander-only doc comments referencing the gate:
  `player_logic.h` (~line 82), `message_log_widget.h` (~line 87).

## Step 2 — strip non-Commander UI (per user decision; biggest/riskiest diff)

Do this as its **own commit** and live-verify the deck editor carefully.
- `cockatrice/src/interface/widgets/dialogs/dlg_create_game.cpp` (~line 113): remove
  game-type selection; always apply Commander defaults (40 life / 4 players). The
  server room config still lists types, but the client no longer offers a choice.
- Deck editor:
  `cockatrice/src/interface/widgets/deck_editor/deck_editor_deck_dock_widget.cpp`
  (~line 382) and `libcockatrice_models/.../deck_list/deck_list_model.cpp` (~line 792):
  collapse to Commander — color-identity + 100-card-singleton legality always enforced;
  remove the format-picker path (`formatUsesColorIdentity()` gating no longer needed
  since it's always Commander).

## Step 3 — module migration (the "one home" payoff)

Move the Commander-specific modules into `libcockatrice_rules` where dependency-clean,
updating includes + CMake:
- `libcockatrice_models/.../deck_list/commander_deck_validator.{h,cpp}` (models →
  rules).
- `libcockatrice_utility/.../utility/commander_counter_names.h` (utility → rules).
- `libcockatrice_card/.../card/format/commander_rules.{h,cpp}` (card → rules).

**Acyclicity caveat:** `commander_rules` (color identity) is consumed by the deck model
and takes `CardInfo`. Before moving it, confirm the dependency direction stays acyclic
(`libcockatrice_rules` → `libcockatrice_card`, never the reverse). If moving it would
create a cycle, **leave `commander_rules` in `libcockatrice_card`** and migrate only the
validator + counter-names. The generic card parsers
`card/ability/{card_keywords,mana_abilities}.*` are card-data functions and **stay** in
`libcockatrice_card`.

Note: `libcockatrice_rules` currently links only `libcockatrice_utility` + Qt Core, so
absorbing the validator (needs `DeckListModel`/`libcockatrice_models`) and
`commander_rules` (needs `libcockatrice_card`) will require adding those as `PUBLIC`
deps of `libcockatrice_rules` — check this doesn't introduce a cycle with the client
libs that link rules.

## Step 4 — build + verify

- Full **client rebuild** (`make -j1 cockatrice`, the heaviest target; check `free -h`
  and `df -h /` between stages — ccache is set up, see below).
- `ctest` green (no regression); `./format.sh --cmake --branch master` clean.
- **Live (Xvfb + screenshot/log):** create-game shows no game-type selector and
  pre-fills 40 life / 4 players; deck editor enforces Commander legality with no format
  picker; priority button, SBA/lethal warnings, keyword row, and one-click mana tap all
  still work.
- Likely **2 commits**: (a) de-gating, (b) UI strip + module migration. Push each to
  `fork/commander-rules`.

## Increment 3+ (future, not this increment)

Real enforcement grows *inside* `RulesEngine`: a resolvable stack-object model, mana
cost `canPay()`/`pay()`, then combat. Each needs a card-effect execution engine and an
explicit design pass first.

## Environment / resume notes (sandbox)

- **ccache** is installed as a **static binary at `~/.local/bin/ccache`** (AL2023's dnf
  has no ccache package, same situation as Qt6 — fetched from ccache's GitHub releases),
  cache dir `~/.ccache`, capped 250M. The build dir is already configured with
  `-DCMAKE_CXX_COMPILER_LAUNCHER=ccache` (and the repo's own `USE_CCACHE` option also
  finds it now). Keep `~/.local/bin` on `PATH`.
- Disk is tight (~8 GiB root, was 94–96% full). `sudo dnf clean all` and pruning
  `.uitest/*.png` reclaim space; check `df -h /` before big builds.
- `build/` is configured with `-DTEST=ON -DWITH_SERVER=ON -DWITH_CLIENT=ON`. servatrice
  + cockatrice binaries built at Increment 1; the **client still needs a rebuild** after
  Increment 2's edits.
- Live-test recipe (Xvfb :99, servatrice, `.uitest/uitest.py`) is in `CLAUDE.md`. Gotcha
  learned this session: **avoid `pkill -f <pattern>` where `<pattern>` appears in your
  own command line** — it kills the wrapper shell (exit 144). Foreground `sleep` is also
  blocked; use a `timeout bash -c 'until ...; do :; done'` poll instead.
