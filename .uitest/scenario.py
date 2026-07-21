#!/usr/bin/env python3
"""One-shot scenario runner for this fork's live-verification testing.

Encapsulates the manual "start Xvfb -> launch servatrice+client -> wait ->
drive input -> assert against the debug log -> print PASS/FAIL -> teardown"
dance documented in CLAUDE.md's UI-testing section, so a scenario run is one
Bash call with plain-text output instead of a many-round-trip manual sequence
full of screenshot Reads. Assertions grep the client/server debug logs for
expected protobuf lines (cheap text, established as ground truth in this repo
-- see CLAUDE.md's "Reading the client's own debug log" testing method)
instead of reading PNGs; use `shot()`/uitest.py directly only when a check is
genuinely visual (layout, color, rotation).

Usage:
    python3 .uitest/scenario.py setup [--local-game]  # ensure Xvfb+servatrice+client running
    python3 .uitest/scenario.py teardown              # kill servatrice+client (keeps Xvfb)
    python3 .uitest/scenario.py teardown --all        # also kill Xvfb
    python3 .uitest/scenario.py list                  # list available scenarios
    python3 .uitest/scenario.py run <name> [<name> ...]  # setup + run named scenario(s)

setup()/run() also idempotently fix this fork's own client settings profile every time (see
ensure_test_client_profile()/ensure_test_server_profile()/ensure_test_card_database()) --
notably, disabling the "Congratulations on updating to Cockatrice <hash>!" QMessageBox that
this fork's git-hash-embedding VERSION_STRING otherwise pops on almost every session (a real,
previously undiagnosed root cause of "clicks don't seem to register" -- it was silently
sitting modally on top of the whole window, swallowing every subsequent scripted click, not a
fundamental Xvfb/no-window-manager limitation as earlier sessions suspected).

--local-game (or running a scenario in LOCAL_GAME_SCENARIOS, e.g. "mana_gate") switches the
client straight into a one-player hotseat game instead of the Home tab, for scenarios that
need a real board/hand/mana-pool rather than a server connection -- see
ensure_local_game_profile(). Switching between local-game and normal mode restarts an
already-running client automatically (it's a startup-only setting).

Add a new scenario by writing a function `scenario_<name>(ctx) -> bool`, registering it in
SCENARIOS below, and adding its name to LOCAL_GAME_SCENARIOS too if it needs a local game
rather than a Home-tab-fresh client.
"""
import re
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import uitest  # noqa: E402  (needs sys.path adjustment above)

REPO_ROOT = Path(__file__).resolve().parent.parent
DISPLAY = ":99"
CLIENT_BIN = REPO_ROOT / "build" / "cockatrice" / "cockatrice"
SERVATRICE_BIN = REPO_ROOT / "build" / "servatrice" / "servatrice"
SERVATRICE_INI = REPO_ROOT / ".uitest" / "servatrice_local.ini"
CLIENT_LOG = Path("/tmp/cockatrice_gui.log")
SERVATRICE_LOG = Path("/tmp/servatrice.log")


def _pgrep(pattern):
    result = subprocess.run(["pgrep", "-f", pattern], capture_output=True, text=True)
    return [pid for pid in result.stdout.split() if pid]


def _running(pattern):
    return len(_pgrep(pattern)) > 0


def _wait_for_log(logfile, pattern, timeout=15, poll=0.3):
    """Poll logfile for a line matching regex `pattern`. Returns the matched
    line, or None on timeout. Reads the whole file each poll (these logs stay
    small for a single scenario run) rather than tailing, for simplicity."""
    deadline = time.time() + timeout
    regex = re.compile(pattern)
    while time.time() < deadline:
        if logfile.exists():
            text = logfile.read_text(errors="replace")
            for line in text.splitlines():
                if regex.search(line):
                    return line
        time.sleep(poll)
    return None


def ensure_xvfb():
    if _running(f"Xvfb {DISPLAY}"):
        print(f"[setup] Xvfb {DISPLAY} already running")
        return
    print(f"[setup] starting Xvfb {DISPLAY}")
    subprocess.Popen(
        ["Xvfb", DISPLAY, "-screen", "0", "1280x800x24", "-nolisten", "tcp"],
        start_new_session=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    for _ in range(20):
        try:
            from Xlib import display as xdisplay

            xdisplay.Display(DISPLAY)
            print("[setup] Xvfb is up")
            return
        except Exception:
            time.sleep(0.3)
    raise RuntimeError("Xvfb did not come up in time")


def ensure_servatrice():
    if _running(str(SERVATRICE_BIN)):
        print("[setup] servatrice already running")
        return
    if not SERVATRICE_BIN.exists():
        raise RuntimeError(f"servatrice binary not found at {SERVATRICE_BIN}")
    print("[setup] starting servatrice")
    log = open(SERVATRICE_LOG, "w")
    subprocess.Popen(
        [str(SERVATRICE_BIN), "--config", str(SERVATRICE_INI), "--log-to-console"],
        cwd=str(REPO_ROOT),
        stdout=log,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )
    line = _wait_for_log(SERVATRICE_LOG, r"Server started|listening", timeout=20)
    if not line:
        raise RuntimeError(
            f"servatrice did not report ready within timeout; see {SERVATRICE_LOG}"
        )
    print(f"[setup] servatrice ready: {line.strip()}")


def ensure_test_client_profile():
    """Idempotently disable this fork's own startup dialogs that would otherwise sit
    modally on top of the main window and swallow every subsequent scripted click --
    the actual root cause found behind "connect" scenario failing at the login step
    (see doc/commander-status/testing-and-ops.md): this fork's VERSION_STRING embeds
    the current git commit hash, so it differs from whatever version is recorded in
    the profile on almost every session, which triggers a "Congratulations on
    updating to Cockatrice <hash>!" QMessageBox (window_main.cpp's
    alertForcedOracleRun()) dead center over the Home tab before anything else can be
    clicked. Writes directly into this fork's Cockatrice client settings profile
    (QSettings IniFormat under $HOME, plain key=value pairs) rather than a checked-in
    file, since the profile only exists at runtime and is otherwise auto-populated on
    first launch. Safe to call every time -- only rewrites the file if a key is
    actually missing or wrong."""
    import configparser

    settings_dir = Path.home() / ".local" / "share" / "Cockatrice" / "Cockatrice" / "settings"
    settings_dir.mkdir(parents=True, exist_ok=True)
    ini_path = settings_dir / "global.ini"

    parser = configparser.RawConfigParser()
    parser.optionxform = str  # preserve key case exactly as Cockatrice itself writes it
    if ini_path.exists():
        parser.read(ini_path)

    # Exact key casing matches cache_settings.cpp's own settings->setValue() calls.
    desired = {
        "personal": {
            "newversionnotification": "false",  # the "Congratulations on updating" dialog
            "startupCardUpdateCheckPromptForUpdate": "false",  # DlgStartupCardCheck
            "startupUpdateCheck": "false",  # actCheckClientUpdates() network call
        },
        "tipOfDay": {
            "showTips": "false",  # DlgTipOfTheDay
        },
        "interface": {
            # A single synthetic click reliably registers; two clicks close enough together to
            # land inside Qt's double-click interval are much less reliable over XTest (timing
            # is right at the edge depending on how much sleep padding surrounds each click), so
            # scenarios click once to play a card rather than fight double-click timing.
            "doubleclicktoplay": "false",
        },
    }
    changed = False
    for section, keys in desired.items():
        if not parser.has_section(section):
            parser.add_section(section)
            changed = True
        for key, value in keys.items():
            if parser.get(section, key, fallback=None) != value:
                parser.set(section, key, value)
                changed = True

    if changed:
        with open(ini_path, "w") as f:
            parser.write(f, space_around_delimiters=False)
        print(f"[setup] disabled startup dialogs in {ini_path}")
    else:
        print("[setup] startup dialogs already disabled")


def ensure_test_server_profile():
    """Idempotently pre-seeds a single, deterministic "known host" entry (127.0.0.1:4747,
    matching SERVATRICE_INI) into this fork's servers.ini, so the "Connect to Server" dialog
    always opens with exactly one predictable, already-selected saved server -- no clicking
    through "New Host" radio buttons / Name / Host / Port fields required at all (those turned
    out to be a second, independent source of scenario flakiness: DlgConnect::actOk() reads
    whatever's in those QLineEdits regardless of visibility, and their pixel position shifts
    between "Known Hosts" and "New Host" layouts, which a previous session's hand-picked
    coordinates didn't account for). This also sidesteps DlgConnect::preRebuildComboBoxList()'s
    real network call to download the public server list, which only fires when the saved-host
    list has exactly one (unnamed placeholder) entry -- i.e. an empty/fresh profile."""
    import configparser

    settings_dir = Path.home() / ".local" / "share" / "Cockatrice" / "Cockatrice" / "settings"
    settings_dir.mkdir(parents=True, exist_ok=True)
    ini_path = settings_dir / "servers.ini"

    save_name = "scenariotest"
    parser = configparser.RawConfigParser()
    parser.optionxform = str
    if ini_path.exists():
        parser.read(ini_path)
    if not parser.has_section("server"):
        parser.add_section("server")

    # Keys match ServersSettings'/UserConnection_Information's own literal key names --
    # "server_details\\<field><index>" is a flat key (the backslash is not a QSettings group
    # separator here), index 1, matching what actOk()/getServerInfo() read back.
    desired = {
        "previoushostName": save_name,
        "previoushostlogin": "0",
        "auto_connect": "0",
        r"server_details\totalServers": "1",
        r"server_details\saveName1": save_name,
        r"server_details\server1": "127.0.0.1",
        r"server_details\port1": "4747",
        r"server_details\username1": "scenariotest",
        r"server_details\password1": "",
        r"server_details\savePassword1": "false",
        r"server_details\site1": "",
    }
    changed = False
    for key, value in desired.items():
        if parser.get("server", key, fallback=None) != value:
            parser.set("server", key, value)
            changed = True

    if changed:
        with open(ini_path, "w") as f:
            parser.write(f, space_around_delimiters=False)
        print(f"[setup] pre-seeded known-host profile ({save_name} -> 127.0.0.1:4747) in {ini_path}")
    else:
        print("[setup] known-host profile already seeded")


def ensure_test_card_database():
    """Idempotently copies .uitest/sample_cards.xml over this fork's client-profile card
    database. A previous session found this out the hard way: the profile's copy silently
    goes stale (missing whatever fixture cards a *later* session's checked-in sample_cards.xml
    added), which then makes any card added for a new scenario appear to not exist at all
    in-game -- confusing to debug, since the parser/build side is completely fine. Refreshing
    on every setup() makes that whole class of staleness impossible."""
    cards_dir = Path.home() / ".local" / "share" / "Cockatrice" / "Cockatrice"
    cards_dir.mkdir(parents=True, exist_ok=True)
    dest = cards_dir / "cards.xml"
    src = REPO_ROOT / ".uitest" / "sample_cards.xml"
    if not dest.exists() or dest.read_bytes() != src.read_bytes():
        dest.write_bytes(src.read_bytes())
        print(f"[setup] refreshed card database from {src}")
    else:
        print("[setup] card database already up to date")


LOCAL_GAME_DECK = REPO_ROOT / ".uitest" / "manatest.cod"


def ensure_local_game_profile(enabled):
    """Idempotently toggles this fork's [localgame] auto-start (debug.ini) -- scenarios that
    need a real board (mana pool counters, playing cards, casting spells) turn this on;
    scenarios that expect to land on the Home tab (e.g. "connect") need it off, since an
    auto-started local game switches the client straight to a Game tab instead. This is a
    startup-time-only setting -- see setup()'s mode-marker handling below for why switching
    between the two requires restarting an already-running client rather than just rewriting
    this file. Returns True if anything actually changed."""
    import configparser

    settings_dir = Path.home() / ".local" / "share" / "Cockatrice" / "Cockatrice" / "settings"
    settings_dir.mkdir(parents=True, exist_ok=True)
    ini_path = settings_dir / "debug.ini"

    parser = configparser.RawConfigParser()
    parser.optionxform = str
    if ini_path.exists():
        parser.read(ini_path)
    for section in ("debug", "localgame"):
        if not parser.has_section(section):
            parser.add_section(section)

    desired = {
        "debug": {"showCardId": "true"},
        "localgame": {
            "onStartup": "true" if enabled else "false",
            "playerCount": "1",
            r"deck\Player 1": str(LOCAL_GAME_DECK),
        },
    }
    changed = False
    for section, keys in desired.items():
        for key, value in keys.items():
            if parser.get(section, key, fallback=None) != value:
                parser.set(section, key, value)
                changed = True

    if changed:
        with open(ini_path, "w") as f:
            parser.write(f, space_around_delimiters=False)
    state = "enabled" if enabled else "disabled"
    print(f"[setup] local-game auto-start {state} ({'updated' if changed else 'already set'}) in {ini_path}")
    return changed


def ensure_client():
    if _running(str(CLIENT_BIN)):
        print("[setup] client already running")
        return
    if not CLIENT_BIN.exists():
        raise RuntimeError(f"cockatrice binary not found at {CLIENT_BIN}")
    ensure_test_client_profile()
    ensure_test_server_profile()
    ensure_test_card_database()
    print("[setup] starting cockatrice client")
    log = open(CLIENT_LOG, "w")
    env_display = {"DISPLAY": DISPLAY}
    import os

    env = dict(os.environ)
    env.update(env_display)
    subprocess.Popen(
        [str(CLIENT_BIN)],
        cwd=str(REPO_ROOT),
        stdout=log,
        stderr=subprocess.STDOUT,
        start_new_session=True,
        env=env,
    )
    # No single definitive "ready" log line for the client at idle; give the
    # Qt event loop + window manager-less Xvfb time to map the main window.
    for _ in range(30):
        if CLIENT_LOG.exists() and CLIENT_LOG.stat().st_size > 0:
            break
        time.sleep(0.3)
    time.sleep(2.0)
    print("[setup] client launched")


MODE_MARKER = Path("/tmp/cockatrice_scenario_mode")


def setup(local_game=False):
    ensure_xvfb()
    ensure_servatrice()
    ensure_local_game_profile(local_game)

    # debug.ini's onStartup is only read at client launch, so an already-running client is
    # stale (and must be relaunched) whenever the requested mode differs from whichever mode
    # it was actually started in -- comparing debug.ini's *content* isn't enough on its own,
    # since a client already running from an earlier setup() call could predate the file's
    # current content entirely.
    requested_mode = "local_game" if local_game else "normal"
    current_mode = MODE_MARKER.read_text().strip() if MODE_MARKER.exists() else None
    if _running(str(CLIENT_BIN)) and current_mode != requested_mode:
        print(f"[setup] running client is in '{current_mode}' mode, need '{requested_mode}' -- restarting it")
        teardown(kill_xvfb=False)

    ensure_client()
    MODE_MARKER.write_text(requested_mode)


def teardown(kill_xvfb=False):
    for pattern in (str(CLIENT_BIN), str(SERVATRICE_BIN)):
        for pid in _pgrep(pattern):
            subprocess.run(["kill", pid])
    if kill_xvfb:
        for pid in _pgrep(f"Xvfb {DISPLAY}"):
            subprocess.run(["kill", pid])
    print("[teardown] done" + (" (including Xvfb)" if kill_xvfb else " (Xvfb left running)"))


class Ctx:
    """Passed to each scenario function; thin wrapper over uitest's input
    primitives plus the log-assertion helper, so scenarios read as a linear
    script."""

    def __init__(self):
        self.ok = True

    def click(self, x, y, button=1):
        uitest.click(x, y, button)

    def move(self, x, y):
        uitest.move(x, y)

    def drag(self, x1, y1, x2, y2, button=1, steps=10):
        uitest.drag(x1, y1, x2, y2, button, steps)

    def key(self, keysym_name):
        uitest.key(keysym_name)

    def key_combo(self, modifier_name, keysym_name):
        uitest.key_combo(modifier_name, keysym_name)

    def type_text(self, text):
        uitest.type_text(text)

    def shot(self, outfile):
        uitest.shot(outfile)

    def sleep(self, seconds):
        time.sleep(seconds)

    def assert_log(self, logfile, pattern, timeout=10, label=None):
        line = _wait_for_log(logfile, pattern, timeout=timeout)
        label = label or pattern
        if line:
            print(f"  [PASS] {label}\n         -> {line.strip()}")
            return True
        print(f"  [FAIL] {label}\n         (no match for /{pattern}/ in {logfile} within {timeout}s)")
        self.ok = False
        return False


# ---------------------------------------------------------------------------
# Scenarios
# ---------------------------------------------------------------------------


def scenario_connect(ctx):
    """Connect the running client to the local servatrice as a guest and
    confirm a successful login round-trip. Assumes the client is freshly
    launched and sitting on the Home tab (true right after ensure_client()
    on a clean start; re-running against an already-connected client will
    just fail the "not yet connected" assumptions in the UI, which is fine
    -- this scenario is meant to run once per fresh client).

    Deliberately keyboard-driven past the initial "Connect" click rather than
    clicking each field: DlgConnect always defaults to "Known Hosts" mode
    (DlgConnect::DlgConnect() unconditionally checks that radio) and always
    gives the Player name field initial focus (playernameEdit->setFocus()) --
    combined with ensure_test_server_profile() pre-seeding exactly one known
    host (so it's both the only choice and the pre-selected one), there's no
    need to click into any field at all, which is what made the old
    hand-picked pixel coordinates fragile (they silently assumed a "New Host"
    layout whose field positions don't match the default "Known Hosts" view)."""
    print("[connect] opening Connect to Server dialog")
    # Home tab's "Connect" button -- the first, full-width button in the list,
    # forgiving to click since it spans nearly the whole window width.
    ctx.click(392, 317)
    ctx.sleep(0.7)
    # Player name field already has focus; select-all + retype is defensive
    # (works whether or not the pre-seeded username survived) then submit via
    # the dialog's default button (QDialogButtonBox's AcceptRole button).
    ctx.key_combo("ctrl", "a")
    ctx.type_text("scenariotest")
    ctx.key("Return")
    ctx.sleep(1.5)
    return ctx.assert_log(
        CLIENT_LOG,
        r"Event_ServerIdentification|Event_Login|ReloginOrLoginResponse|Response_Login",
        timeout=10,
        label="client receives a login/identification response from servatrice",
    )


# Hand-card slots are centered as a group rather than left-aligned, so their x-centers shift
# depending on how many cards remain in hand -- these are the exact values this deck's 3 -> 2
# -> 1 shrink sequence was empirically measured at (1280x800 Xvfb, this fork's default window
# geometry). HAND_ROW_Y is the y-coordinate of the row itself.
HAND_ROW_Y = 528
_HAND_SLOT_CENTERS = {3: [367, 434, 500], 2: [401, 469], 1: [436]}

# Kaya's Wrath's WB colors render a distinct gold card-frame placeholder; Sol Ring (artifact)
# and Plains (land) both render the same plain-gray placeholder since no card image data is
# loaded for this fixture deck (there's no OCR available to read the printed name text
# directly, so this is the only cheap way to single out Kaya's Wrath by sight).
KAYAS_WRATH_COLOR = (250, 190, 30)


def _hand_colors(n):
    """(x, (r, g, b)) for each of the n current hand-card slots, sampled inside the card body
    (below its name-label strip) -- see KAYAS_WRATH_COLOR."""
    return [(x, uitest.pixel(x, HAND_ROW_Y + 32)) for x in _HAND_SLOT_CENTERS[n]]


def _moved_to_table_pattern(card_names):
    """Regex matching the real Event_MoveCard.ext protobuf line for @p card_names (a string or
    list of alternatives) arriving on the table zone -- the actual ground truth for "this card
    was cast," unlike the human-readable "puts X into play" chat-panel message, which turns out
    to only ever get rendered client-side and is never itself written to the debug log file."""
    if isinstance(card_names, str):
        card_names = [card_names]
    # The log's protobuf text-format serializer backslash-escapes an apostrophe inside a quoted
    # string (card_name: "Kaya\'s Wrath"), so a plain re.escape() alone (which leaves "'"
    # untouched) doesn't match -- insert the literal backslash the log actually contains.
    alternatives = "|".join(re.escape(name).replace("'", r"\\'") for name in card_names)
    return rf'Event_MoveCard\.ext.*card_name: "(?:{alternatives})".*target_zone: "table"'


def scenario_mana_gate(ctx):
    """Live-verifies real mana payment for casting spells from hand end to end (see
    doc/commander-status/phase6-mana.md's addendum) using a one-player local hotseat game
    instead of a real second client -- this only needs one player's own hand and mana pool.
    Requires local-game mode (see LOCAL_GAME_SCENARIOS/run()'s dispatch), which puts the
    client straight into Game tab #1 with an empty hand and a 3-card library
    (.uitest/manatest.cod: Sol Ring, Kaya's Wrath, Plains -- all three already in
    .uitest/sample_cards.xml).

    Sol Ring/Plains are told apart without any visual signal at all: attempting to cast Sol
    Ring while the pool is empty is *supposed* to be silently blocked (the "Cannot Cast"
    dialog is a client-local QMessageBox, never sent to the server, so no move-into-play line
    ever appears in the log) -- so this scenario just tries the first non-gold hand slot and
    treats "no log line within a couple seconds" as proof that slot was Sol Ring being
    correctly rejected, and "a log line appeared" as proof it was Plains (a land, always
    free) instead. That single click therefore doubles as this scenario's unaffordable-cast
    check.
    """
    print("[mana_gate] drawing the starting 3-card hand")
    for _ in range(3):
        ctx.click(23, 190)  # Draw button
        ctx.sleep(0.5)
    # This tiny deck empties on the 3rd draw, which pops an advisory "attempted to draw from
    # an empty library" QMessageBox -- sometimes more than once (Phase 4's own turn-structure
    # draw-step automation can independently trigger the same check). Each dismiss-click is a
    # harmless no-op on the battlefield if no dialog happens to be open at that moment.
    for _ in range(3):
        ctx.key("Return")  # QMessageBox::information's default (only) button -- not a fixed
        ctx.sleep(0.3)      # pixel coordinate, since the box auto-sizes to its message length
    ctx.sleep(0.5)

    samples = _hand_colors(3)
    gray_xs = [x for x, color in samples if color != KAYAS_WRATH_COLOR]
    if len(gray_xs) != 2:
        print(f"  [FAIL] expected 2 non-gold (Sol Ring/Plains) hand slots, sampled {samples}")
        ctx.ok = False
        return False

    print("[mana_gate] casting the first non-Kaya's-Wrath card with an empty mana pool (expect a block)")
    ctx.click(gray_xs[0], HAND_ROW_Y)
    ctx.sleep(1.0)
    line = _wait_for_log(CLIENT_LOG, _moved_to_table_pattern(["Sol Ring", "Plains"]), timeout=2)
    if line is None:
        print("  [PASS] unaffordable Sol Ring cast was correctly blocked (nothing logged)")
        ctx.key("Return")  # dismiss the "Cannot Cast" dialog
        ctx.sleep(0.3)
        plains_x = gray_xs[1]
    else:
        ok = "Plains" in line
        print(f"  [{'PASS' if ok else 'FAIL'}] first slot played immediately -> {line.strip()}")
        ctx.ok = ctx.ok and ok
        plains_x = gray_xs[0]

    print("[mana_gate] playing Plains (a land -- always free, never gated)")
    ctx.click(plains_x, HAND_ROW_Y)
    ctx.sleep(1.0)
    if not ctx.assert_log(CLIENT_LOG, _moved_to_table_pattern("Plains"), timeout=5,
                          label="Plains enters the battlefield for free"):
        return False

    print("[mana_gate] +1 White mana, then casting Sol Ring ({1} -- unambiguous, no dialog expected)")
    ctx.click(68, 192)  # White mana counter, +1 per left-click
    ctx.sleep(0.3)
    sol_ring_x = next(x for x, color in _hand_colors(2) if color != KAYAS_WRATH_COLOR)
    ctx.click(sol_ring_x, HAND_ROW_Y)
    ctx.sleep(1.0)
    if not ctx.assert_log(CLIENT_LOG, _moved_to_table_pattern("Sol Ring"), timeout=5,
                          label="Sol Ring auto-pays from the only available color, no dialog"):
        return False

    print("[mana_gate] W=2, U=2, B=1 -> Kaya's Wrath's ({2}{W}{B}) generic cost is now ambiguous")
    for x, y in [(68, 192), (68, 192), (68, 233), (68, 233), (68, 275)]:
        ctx.click(x, y)
        ctx.sleep(0.3)
    ctx.click(_HAND_SLOT_CENTERS[1][0], HAND_ROW_Y)  # only Kaya's Wrath remains in hand
    ctx.sleep(1.0)
    ctx.click(768, 405)  # DlgChooseGenericManaPayment's OK, accepting its pre-filled default split
    ctx.sleep(1.0)
    return ctx.assert_log(CLIENT_LOG, _moved_to_table_pattern("Kaya's Wrath"), timeout=5,
                          label="Kaya's Wrath casts via the ambiguous-payment dialog's default split")


SCENARIOS = {
    "connect": scenario_connect,
    "mana_gate": scenario_mana_gate,
}

# Scenarios needing a fresh local hotseat game (see ensure_local_game_profile()) rather than
# the Home tab a plain client launch lands on.
LOCAL_GAME_SCENARIOS = {"mana_gate"}


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    cmd = sys.argv[1]
    if cmd == "setup":
        setup(local_game="--local-game" in sys.argv[2:])
    elif cmd == "teardown":
        teardown(kill_xvfb="--all" in sys.argv[2:])
    elif cmd == "list":
        for name in SCENARIOS:
            print(name)
    elif cmd == "run":
        names = sys.argv[2:]
        if not names:
            print("usage: scenario.py run <name> [<name> ...]")
            sys.exit(1)
        unknown = [n for n in names if n not in SCENARIOS]
        if unknown:
            print(f"unknown scenario(s): {', '.join(unknown)}")
            print(f"available: {', '.join(SCENARIOS)}")
            sys.exit(1)
        setup(local_game=any(n in LOCAL_GAME_SCENARIOS for n in names))
        overall_ok = True
        for name in names:
            print(f"[run] {name}")
            ctx = Ctx()
            result = SCENARIOS[name](ctx)
            passed = bool(result) and ctx.ok
            overall_ok = overall_ok and passed
            print(f"[{'PASS' if passed else 'FAIL'}] {name}")
        sys.exit(0 if overall_ok else 1)
    else:
        print(f"unknown command: {cmd}")
        print(__doc__)
        sys.exit(1)


if __name__ == "__main__":
    main()
