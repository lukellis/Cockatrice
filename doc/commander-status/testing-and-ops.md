# Testing & Operations Notes

The canonical build/test/lint process (Docker toolchain, GTest, `format.sh`, the
Xvfb/screenshot UI-testing recipe) lives in `CLAUDE.md` — read that first. This file
only holds operational notes that don't fit there: real-multiplayer hosting, and
testing-efficiency tips worth knowing before a long verification session.

## Testing-efficiency tips

- **Log-assertion-first**: the client's own debug log
  (`/tmp/cockatrice_gui.log` or wherever redirected) is ground truth for exact
  wire-level state (counter values, zone contents, event sequencing) — prefer
  `grep`-ing it over reading a screenshot for anything the log states exactly. Reserve
  screenshots for genuinely *visual* properties (layout, color, rotation). This
  already caught two real bugs (see `phase2-deck-validation.md` and
  `phase3-command-zone.md`) that GTest alone missed.
- **`.uitest/scenario.py`** (checked in alongside `.uitest/uitest.py`) wraps the
  Xvfb/servatrice/client bring-up + a scripted input sequence + log-assertion +
  teardown into one scripted, named scenario (`setup`/`run <name>`/`teardown`),
  turning a many-round-trip manual dance into one call with text-only output. Currently
  has one shipped scenario (`connect`) as a proof it works end-to-end; add more here
  as specific flows need repeatable verification, following the same pattern.

## EC2 hosting runbook (play-test from a real second client)

For testing with a real second player rather than a solo self-test. `servatrice`
binds `0.0.0.0:4747` (TCP) and `:4748` (websocket).
`.uitest/servatrice_local.ini` (`type=none` DB, `method=none` auth, registration off)
works as-is — no MySQL needed.

1. Open the host's firewall/security group: inbound TCP 4747 (and 4748 for
   websocket/browser clients), scoped to known IPs rather than `0.0.0.0/0`.
2. Run `servatrice` persistently (`tmux`/`systemd`/`nohup`) so it outlives the
   session: `nohup ./build/servatrice/servatrice --config .uitest/servatrice_local.ini
   --log-to-console > /tmp/servatrice.log 2>&1 & disown`.
3. Each real player connects via Connect → New Host → `<host-ip>:4747`, any username,
   no password.
4. **Friends need this fork's client build**, not stock Cockatrice, to see any
   client-side Commander feature (command-zone UI, keyword row, priority button,
   SBA/lethal warnings, pending-ability panel). Server-side behavior (command zone,
   tax, auto-untap/draw, priority events) fires regardless of client.
5. **Card database must match** on every client — this repo ships only a small
   hand-crafted `.uitest/sample_cards.xml`, not a full MTGJSON dataset.
6. **Security**: `method=none` means anyone reaching the port can join. Fine behind a
   locked-down firewall; never pair it with an open `0.0.0.0/0` rule.
