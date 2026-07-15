# cockatrice-commander

Fork of [Cockatrice/Cockatrice](https://github.com/Cockatrice/Cockatrice) that adds
enforcement of official MTG Commander/EDH rules. Full design rationale, file-by-file
change list, and **current implementation/build status** live in
[`COMMANDER_IMPLEMENTATION_STATUS.md`](COMMANDER_IMPLEMENTATION_STATUS.md) — **read
that file first** before resuming work; it's kept up to date as the source of truth
across sessions (this repo's build has previously been interrupted by sandbox OOM,
so don't assume anything in conversation memory is current — check the file).

The original design inspiration is
[`doc/design-docs/mtg-commander-rules-engine.md`](doc/design-docs/mtg-commander-rules-engine.md)
(mirrored in-repo from [jeffyche/fun-stuff](https://github.com/jeffyche/fun-stuff/blob/master/design-docs/mtg-commander-rules-engine.md)
so it survives if the source disappears/changes). It proposes a much larger 9–13
month, 9-phase roadmap (turn structure, stack/priority, mana, abilities, combat,
full state-based actions, plus a 4-player UI overhaul) — this fork intentionally
implements only its own §8 "Recommended Starting Point" (deck validation +
commander damage/tax via existing counters + 40 life), not the full engine. See
`COMMANDER_IMPLEMENTATION_STATUS.md`'s phase-tracking table before treating any
unimplemented phase as an oversight — it's a scope decision, not a gap to
silently fill without discussing size/risk first.

## Git remotes — read before pushing

- `origin` = the real upstream `Cockatrice/Cockatrice` repo. **Not owned by this
  user — never push to `origin`.**
- `fork` = `github.com/lukellis/Cockatrice`, the user's own fork. All work happens
  on the `commander-rules` branch there. Push with `git push fork commander-rules`.
- Commit and push to `fork` at logical stopping points (a task completed, a build
  stage verified, before attempting something risky) — not just at the very end.
  This is deliberate: the sandbox has previously been OOM-killed mid-session, and a
  pushed commit plus an up-to-date status doc is what let a later session recover
  cleanly instead of re-deriving everything from scratch.

## Sandbox / build environment gotchas

This sandbox is small: **2 vCPUs, ~1.9 GiB RAM**. Take this seriously when building
C++/Qt — a naive full build has OOM-killed the session before.

- `/tmp` is **tmpfs** (RAM-backed, ~955 MiB). Never install large things (like a Qt6
  SDK) there — it directly eats system RAM. Use a disk-backed path instead, e.g.
  `~/qt6install`.
- A swap file should exist at `/swapfile` (1.5 GiB, `sudo swapon`). If it's missing
  (e.g. fresh sandbox), recreate it before building:
  `sudo fallocate -l 1536M /swapfile && sudo chmod 600 /swapfile && sudo mkswap /swapfile && sudo swapon /swapfile`
- AL2023's dnf repos don't ship Qt6. Fetch a prebuilt SDK via `aqtinstall`
  (`pip3 install --user aqtinstall`), e.g.:
  `python3 -m aqt install-qt linux desktop 6.7.0 linux_gcc_64 -O ~/qt6install -m qtwebsockets qtmultimedia`
  (`qtbase` is the implicit base install, not a `-m` module).
- Configure with `cmake -DCMAKE_PREFIX_PATH=~/qt6install/6.7.0/gcc_64 -DCMAKE_BUILD_TYPE=Release -DWITH_SERVER=ON -DWITH_CLIENT=ON -DWITH_ORACLE=OFF -DTEST=OFF ..`
- Build with **`make -j1`**, not `-j2` — two parallel `cc1plus`/MOC processes on
  this box without swap is what likely caused the original OOM. Build **one target
  at a time** (e.g. `make -j1 libcockatrice_network`) rather than a bare `make`, and
  check `free -h` between stages.
- Qt6's shared libs need system runtime libs at link time that dnf doesn't pull in
  by default: `sudo dnf install -y pulseaudio-libs fontconfig freetype` (fixes
  `undefined reference to pa_*` / `Fc*` / `FT_*` link errors against
  `libQt6Multimedia`/`libQt6Gui`).
- Run heavy builds via a backgroundable shell command and wait for its completion
  notification rather than polling with `sleep` in a loop.

## Testing methods used on this fork

Every Commander-rules change should be verified by whichever of these actually
exercises it — compiling is not verification. In order of how much of the
real system each one exercises:

1. **GTest unit/integration suite** (`tests/`, esp. `tests/commander/`) — pure
   algorithmic logic (`commander_rules_test.cpp`) and integration tests
   against the real `DeckListModel`/`CardDatabaseManager` pipeline
   (`commander_deck_validator_test.cpp`), plus turn-structure/priority logic
   (`tests/movecard_tests/commander_turn_structure_test.cpp`). Build with
   `-DTEST=ON`, run via `ctest --output-on-failure` from `build/`, or run
   individual binaries directly (e.g. `build/tests/commander/commander_rules_test`).
   See `COMMANDER_IMPLEMENTATION_STATUS.md`'s "Testing" section for the full
   current pass/fail count and what's covered.
2. **`./format.sh --cmake --branch master`** — clang-format + cmake-format
   lint, matching what CI's lint workflow checks. Always pass `--branch
   master` explicitly (see note in the UI-testing section's sibling "Lint"
   section of the status doc) — the default `--branch origin/master` points
   at the real upstream remote and can pick up unrelated diffs.
3. **Headless UI testing** (Xvfb + screenshots + input simulation) — the only
   way to verify anything about how a change actually *looks* or *behaves* in
   the running client/server, as opposed to whether it compiles or passes a
   unit test written against the same assumptions as the code under test.
   Full recipe below. **This is not optional for client-visible Commander
   features** — it already caught two real bugs (a deck-validator
   double-count, and a command-zone card-count display bug) that 100%-passing
   GTest suites had completely missed, because both bugs were in the gap
   between "the isolated logic is correct" and "the real UI/event flow wires
   it up correctly."
4. **Reading the client's own debug log** (`/tmp/cockatrice_gui.log` or
   wherever stdout/stderr is redirected) — logs every protobuf message
   (`IN`/`OUT`) in full since this is a debug build. Use this *alongside*
   screenshots, not instead of them: screenshots confirm what a human would
   see, the log confirms exact wire-level state (zone card counts, counter
   values, event sequencing) that's easy to misread from pixels alone. The
   command-zone card-count bug (item 3 above) was only conclusively
   root-caused by cross-referencing a screenshot showing "0 cards" against
   the log's `Event_GameStateChanged` showing `card_count: 1` for the same
   zone at the same point in time — pixels alone would have suggested a
   styling/positioning problem, not the actual bug (a zone-lifecycle bug in
   `PlayerLogic::eventGameStateChanged()`).

## UI testing (screenshot + input simulation) in this sandbox

There is no display and no screenshot/VNC tooling by default, but a real Qt GUI
*can* be driven and screenshotted headlessly here — this took real effort to
figure out (missing-library errors are not obvious from the Qt error message
alone), so don't rediscover it from scratch. Recipe:

1. Disk is usually critically tight in this sandbox (often <100 MiB free on
   the 8 GiB root fs) — check `df -h /` first and `sudo dnf clean all` /
   remove stale screenshots if needed before installing anything below.
2. Install: `sudo dnf install -y xorg-x11-server-Xvfb xcb-util-cursor libxkbcommon-x11 xcb-util-keysyms xcb-util-wm`
   (the last four fix `libqxcb.so`'s missing dependencies — Qt's own error
   message only names `xcb-cursor0`/`libxcb-cursor0`, but `ldd` on
   `~/qt6install/6.7.0/gcc_64/plugins/platforms/libqxcb.so` reveals the rest:
   `libxkbcommon-x11.so.0`, `libxcb-icccm.so.4`, `libxcb-keysyms.so.1`).
3. `pip3 install --user python-xlib pillow` (screenshotting + XTest input
   simulation; both pure-Python/small, safe even with little disk headroom).
4. Start a virtual display once per session and leave it running in the
   background: `Xvfb :99 -screen 0 1280x800x24 -nolisten tcp &` then `disown`
   (background processes survive across separate tool calls in this harness;
   `xdpyinfo` isn't installed to check liveness — use
   `DISPLAY=:99 python3 -c "from Xlib import display; display.Display(':99')"`
   instead, or just check `ps aux | grep Xvfb`).
5. Launch the client: `DISPLAY=:99 nohup ~/projects/cockatrice-commander/build/cockatrice/cockatrice > /tmp/cockatrice_gui.log 2>&1 & disown`
6. **No card database exists by default** (`WITH_ORACLE=OFF`, and fetching a
   real MTGJSON dataset isn't feasible with disk this tight). Drop a small
   hand-crafted one at `~/.local/share/Cockatrice/Cockatrice/cards.xml` (see
   `.uitest/sample_cards.xml` in this repo — gitignored, not part of the
   Cockatrice product — for a working ~12-card example with a real commander
   and format rules) before launching, or restart the client after adding it.
7. Drive and observe with `.uitest/uitest.py` (gitignored, kept in-repo so it
   survives across sessions): `python3 .uitest/uitest.py {shot <file.png> |
   click <x> <y> | move <x> <y> | key <keysym> | type <text>}`, all against
   `DISPLAY=:99`. Read the resulting PNG with the Read tool to actually look
   at it. `move` + a ~2s pause before `shot` triggers Qt tooltips, which is
   how the deck-editor Commander-validation tooltip text got read directly
   off a live screenshot.
8. **This already found a real bug**: the deck-editor Commander validator was
   double-counting the commander (see `COMMANDER_IMPLEMENTATION_STATUS.md`),
   caught only by actually driving the real UI — the unit tests had built the
   test scenario in a way that didn't match how the real deck editor adds a
   commander. Prefer confirming any client-visible Commander feature this way
   before calling it done, not just via GTest.
9. **For server-side behavior (command zone, counters, phase automation,
   priority), spin up a real local `servatrice` too** — don't rely on the
   client alone. `.uitest/servatrice_local.ini` (gitignored, copy of
   `servatrice/servatrice.ini.example` with `type=none` database,
   `method=none` auth, and room game types renamed to `"Commander"`/
   `"Standard"`) needs no MySQL/auth setup:
   `nohup ./build/servatrice/servatrice --config .uitest/servatrice_local.ini --log-to-console > /tmp/servatrice.log 2>&1 & disown`,
   then connect the client via Home → Connect → New Host →
   `127.0.0.1` : `4747` (any username, no password). Create/join a 1-player
   game to self-test without needing a second real player.
10. **Read the client's own debug log for ground truth**, not just
    screenshots — `/tmp/cockatrice_gui.log` (or wherever you redirected
    stdout/stderr) logs every protobuf message in full (`IN`/`OUT` lines with
    the decoded message content) since this is a debug build. This is far
    more precise than reading pixel colors/tooltips off a cramped 1280×800
    screenshot for verifying exact counter names, zone contents, or event
    sequencing — e.g. confirming `Event_GameStateChanged`'s `zone_list`/
    `counter_list` fields directly, or that `Event_PriorityChanged` fires on
    every phase change.

## Design principles for Commander-rules changes

- Cockatrice is a manual "physical simulator" (no automated mana/combat/turn
  enforcement, not even life ≤ 0 detection). New Commander mechanics (tax, damage)
  follow that pattern: auto-created counters, manually incremented, advisory
  (non-blocking) warnings — not hard auto-loss.
- Reuse existing generic mechanisms (Zone/Counter serialization, per-card legality
  flags) instead of adding new protocol messages where possible — most of this
  fork's diff is `.proto`-free by design. Phase 5 (priority passing) was the
  first exception, since real priority-passing genuinely has no existing
  protocol hook to reuse; see `COMMANDER_IMPLEMENTATION_STATUS.md`'s Phase 5
  section for why that one broke the streak and how it's still kept purely
  additive.
