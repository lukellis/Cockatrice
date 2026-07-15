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

## Design principles for Commander-rules changes

- Cockatrice is a manual "physical simulator" (no automated mana/combat/turn
  enforcement, not even life ≤ 0 detection). New Commander mechanics (tax, damage)
  follow that pattern: auto-created counters, manually incremented, advisory
  (non-blocking) warnings — not hard auto-loss.
- Reuse existing generic mechanisms (Zone/Counter serialization, per-card legality
  flags) instead of adding new protocol messages — keep the diff `.proto`-free.
