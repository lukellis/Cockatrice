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
ensure_local_game_profile(). Switching between local-game and normal mode -- or between two
local-game scenarios that use different decks (see LOCAL_GAME_DECKS) -- restarts an
already-running client automatically (it's a startup-only setting).

Add a new scenario by writing a function `scenario_<name>(ctx) -> bool`, registering it in
SCENARIOS below, and adding its name (with its fixture deck) to LOCAL_GAME_SCENARIOS/
LOCAL_GAME_DECKS too if it needs a local game rather than a Home-tab-fresh client.
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


LOCAL_GAME_DECKS = {
    "mana_gate": REPO_ROOT / ".uitest" / "manatest.cod",
    "variable_mana_gate": REPO_ROOT / ".uitest" / "variablemanatest.cod",
    "turn_structure_gate": REPO_ROOT / ".uitest" / "turnstructuretest.cod",
    "counter_ui_gate": REPO_ROOT / ".uitest" / "countertest.cod",
}


def ensure_local_game_profile(enabled, deck_path=None):
    """Idempotently toggles this fork's [localgame] auto-start (debug.ini) -- scenarios that
    need a real board (mana pool counters, playing cards, casting spells) turn this on;
    scenarios that expect to land on the Home tab (e.g. "connect") need it off, since an
    auto-started local game switches the client straight to a Game tab instead. This is a
    startup-time-only setting -- see setup()'s mode-marker handling below for why switching
    between the two (or between two local-game scenarios with different @p deck_path, e.g.
    "mana_gate" vs "variable_mana_gate") requires restarting an already-running client rather
    than just rewriting this file. Returns True if anything actually changed."""
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
        },
    }
    if enabled:
        desired["localgame"][r"deck\Player 1"] = str(deck_path)
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


def ensure_client(local_game=False):
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
    # This Xvfb setup has no window manager, so nothing ever gives a freshly-mapped window
    # input focus on its own -- confirmed live (2026-07-21, while adding scenario_turn_
    # structure_gate) by finding that a keyboard-only action (Tab, "Next Phase") sent
    # literally nothing over the wire as the very first input to a fresh client, then worked
    # correctly right after a click landed inside the actual game-board QGraphicsView. Only
    # local-game scenarios need this fixed up here: they can legitimately start with a
    # keyboard shortcut (scenario_turn_structure_gate's phase-stepping does), and (400, 400)
    # is empirically known to land inside the battlefield, not blank margin -- (400, 700),
    # below the visible board, did NOT establish focus when tried. Deliberately NOT done for
    # a Home-tab (non-local-game) launch: (400, 400) lands on an unknown Home-tab list item
    # there (confirmed live to break scenario_connect, which already does its own first click
    # on a known widget immediately -- it never needed this fix and (400, 400) isn't safe for
    # it the way it is for the game board).
    if local_game:
        uitest.click(400, 400)
        time.sleep(0.2)
    print("[setup] client launched")


MODE_MARKER = Path("/tmp/cockatrice_scenario_mode")


def setup(local_game=False, deck_path=None):
    ensure_xvfb()
    ensure_servatrice()
    ensure_local_game_profile(local_game, deck_path)

    # debug.ini's onStartup is only read at client launch, so an already-running client is
    # stale (and must be relaunched) whenever the requested mode differs from whichever mode
    # it was actually started in -- comparing debug.ini's *content* isn't enough on its own,
    # since a client already running from an earlier setup() call could predate the file's
    # current content entirely. The deck path is part of the mode key too: two local-game
    # scenarios with different fixture decks (see LOCAL_GAME_DECKS) must still trigger a
    # restart even though "local_game" itself didn't change.
    requested_mode = f"local_game:{deck_path}" if local_game else "normal"
    current_mode = MODE_MARKER.read_text().strip() if MODE_MARKER.exists() else None
    if _running(str(CLIENT_BIN)) and current_mode != requested_mode:
        print(f"[setup] running client is in '{current_mode}' mode, need '{requested_mode}' -- restarting it")
        teardown(kill_xvfb=False)

    ensure_client(local_game=local_game)
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
    # Turn-structure enforcement (2026-07-21, doc/commander-status/phase4-turn-structure.md)
    # means the old "click the Draw phase button 3 times" trick's first click -- which relied
    # on jumping straight from Untap to Draw -- now gets rejected (RespContextError, no card
    # drawn). Step there legally instead: two Tab presses (Untap(0)->Upkeep(1)->Draw(2), the
    # pre-existing "Next Phase" shortcut) auto-draws card 1 on entering Draw (playerCount==1 in
    # a local hotseat game, so rule 103.8a's two-player-only first-draw skip never applies);
    # the Draw phase button is then already active, so clicking it fires its double-click
    # convenience (Command_DrawCards) for cards 2 and 3, same as this scenario always relied on
    # for its 2nd/3rd clicks -- just not for the phase-jumping 1st one anymore.
    ctx.key("Tab")
    ctx.sleep(0.5)
    ctx.key("Tab")
    ctx.sleep(0.5)
    for _ in range(2):
        ctx.click(23, 190)  # Draw phase button, already active
        ctx.sleep(0.5)
    # This tiny deck empties on the 3rd draw, which pops an advisory "attempted to draw from
    # an empty library" QMessageBox -- sometimes more than once (Phase 4's own turn-structure
    # draw-step automation can independently trigger the same check). Each dismiss-click is a
    # harmless no-op on the battlefield if no dialog happens to be open at that moment.
    for _ in range(3):
        ctx.key("Return")  # QMessageBox::information's default (only) button -- not a fixed
        ctx.sleep(0.3)      # pixel coordinate, since the box auto-sizes to its message length
    ctx.sleep(0.5)

    # New sorcery-speed casting-timing gate (phase4-turn-structure.md): none of this scenario's
    # three cards are instants, so every cast below would otherwise be blocked purely on timing
    # before mana-cost gating (this scenario's actual subject) ever runs. One more legal Tab
    # reaches First Main (phase 3) from Draw (phase 2).
    ctx.key("Tab")
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


# Mana-counter click targets (Server_Player::setupZones()'s ids 1-6 -- w,u,b,r,g,x), verified
# live this session by clicking each and reading the resulting Command_IncCounter/
# Event_SetCounter counter_id off the client's debug log: 1=w (68,192), 2=u (68,233),
# 3=b (68,275), 4=r (68,316), 5=g (68,358). x (6) isn't needed by any scenario below.
_MANA_COUNTER_XY = {"w": (68, 192), "u": (68, 233), "b": (68, 275), "r": (68, 316), "g": (68, 358)}

# Placeholder card-art colors for the three DlgChooseVariableManaCost fixture cards (no real
# card images exist in this sandbox -- WITH_ORACLE=OFF -- so Cockatrice paints a flat color
# swatch instead), sampled the same way KAYAS_WRATH_COLOR was: Ghor-Clan Rampager (RG,
# multicolor) renders the identical gold placeholder as any other multicolor card --
# indistinguishable from KAYAS_WRATH_COLOR by color alone, but this deck has only one
# multicolor card, so gold-vs-not is still enough to identify it here.
_GHOR_CLAN_COLOR = KAYAS_WRATH_COLOR  # (250, 190, 30)
_FIREBALL_COLOR = (230, 0, 0)
_DISMEMBER_COLOR = (0, 0, 0)


def scenario_variable_mana_gate(ctx):
    """Live-verifies the hybrid/Phyrexian/{X} mana-cost addendum to real spell casting from
    hand (phase6-mana.md's addendum's own addendum) end to end, the same one-player local
    hotseat trick as scenario_mana_gate(). .uitest/variablemanatest.cod draws Ghor-Clan
    Rampager ({2}{R/G}, hybrid), Dismember ({1}{B/P}{B/P}, Phyrexian), and Fireball ({X}{R},
    variable) -- all three already in .uitest/sample_cards.xml.

    Card disambiguation samples each hand slot's placeholder-art color (see
    _GHOR_CLAN_COLOR/_FIREBALL_COLOR/_DISMEMBER_COLOR) -- three flat, distinct colors, no OCR
    needed. DlgChooseVariableManaCost's OK button position depends on how many rows it has (1
    hybrid row, 2 Phyrexian rows, or 1 X row respectively), so each of the three uses its own
    empirically-found coordinate rather than one shared constant -- verified live this session
    against the running client/server, same as every other coordinate in this file.
    """
    print("[variable_mana_gate] drawing the starting 3-card hand")
    # Same turn-structure-enforcement fix as scenario_mana_gate() -- see its comment for why
    # the old "3 draw-button clicks" trick's 1st click no longer works on its own.
    ctx.key("Tab")
    ctx.sleep(0.5)
    ctx.key("Tab")
    ctx.sleep(0.5)
    for _ in range(2):
        ctx.click(23, 190)  # Draw phase button, already active
        ctx.sleep(0.5)
    for _ in range(3):
        ctx.key("Return")  # dismiss the empty-library QMessageBox, same as scenario_mana_gate
        ctx.sleep(0.3)
    ctx.sleep(0.5)

    # New sorcery-speed casting-timing gate -- same fix as scenario_mana_gate(), one more legal
    # Tab from Draw (phase 2) to First Main (phase 3) before any of these casts are attempted.
    ctx.key("Tab")
    ctx.sleep(0.5)

    # Resampled after every card leaves hand, not computed once up front: casting a card
    # reflows the remaining hand onto a *different* set of x-coordinates (_HAND_SLOT_CENTERS
    # is keyed by remaining hand size), so a slot's x found while 3 cards are in hand is not
    # valid once only 2 (or 1) remain.
    def slot_for(color, hand_size):
        for x, sampled in _hand_colors(hand_size):
            if sampled == color:
                return x
        return None

    samples = _hand_colors(3)
    print(f"  sampled hand colors: {samples}")
    ghor_clan_x = slot_for(_GHOR_CLAN_COLOR, 3)
    if ghor_clan_x is None:
        print(f"  [FAIL] could not find Ghor-Clan Rampager's hand slot, sampled {samples}")
        ctx.ok = False
        return False

    print("[variable_mana_gate] hybrid: casting Ghor-Clan Rampager ({2}{R/G}) with R=2,G=1 in pool")
    for color in ("r", "r", "g"):
        ctx.click(*_MANA_COUNTER_XY[color])
        ctx.sleep(0.3)
    ctx.click(ghor_clan_x, HAND_ROW_Y)
    ctx.sleep(1.0)
    # Only one row (the hybrid R/G choice, defaulting to Red) is ambiguous here -- accept it.
    ctx.click(787, 392)  # DlgChooseVariableManaCost's OK button with exactly 1 row shown
    ctx.sleep(1.0)
    if not ctx.assert_log(CLIENT_LOG, _moved_to_table_pattern("Ghor-Clan Rampager"), timeout=5,
                          label="Ghor-Clan Rampager casts via the hybrid dialog's default color"):
        return False

    dismember_x = slot_for(_DISMEMBER_COLOR, 2)
    if dismember_x is None:
        print(f"  [FAIL] could not find Dismember's hand slot, sampled {_hand_colors(2)}")
        ctx.ok = False
        return False

    print("[variable_mana_gate] Phyrexian: casting Dismember ({1}{B/P}{B/P}) with B=3 in pool")
    for _ in range(3):
        ctx.click(*_MANA_COUNTER_XY["b"])
        ctx.sleep(0.3)
    ctx.click(dismember_x, HAND_ROW_Y)
    ctx.sleep(1.0)
    # Two ambiguous Phyrexian rows, both defaulting to "pay mana" -- accept both.
    ctx.click(787, 412)  # DlgChooseVariableManaCost's OK button with exactly 2 rows shown
    ctx.sleep(1.0)
    if not ctx.assert_log(CLIENT_LOG, _moved_to_table_pattern("Dismember"), timeout=5,
                          label="Dismember casts via the Phyrexian dialog's default (pay mana)"):
        return False

    fireball_x = _HAND_SLOT_CENTERS[1][0]  # only Fireball remains in hand

    print("[variable_mana_gate] X: casting Fireball ({X}{R}) with X=2, pool R=1,U=2")
    ctx.click(*_MANA_COUNTER_XY["r"])  # covers the fixed {R} pip
    ctx.sleep(0.3)
    for _ in range(2):
        ctx.click(*_MANA_COUNTER_XY["u"])  # covers X=2's generic amount
        ctx.sleep(0.3)
    ctx.click(fireball_x, HAND_ROW_Y)
    ctx.sleep(1.0)
    ctx.click(904, 351)  # X spinbox's up-arrow, clicked twice below to set X=2
    ctx.click(904, 351)
    ctx.sleep(0.3)
    ctx.click(787, 384)  # DlgChooseVariableManaCost's OK button with exactly 1 row (X) shown
    ctx.sleep(1.0)
    return ctx.assert_log(CLIENT_LOG, _moved_to_table_pattern("Fireball"), timeout=5,
                          label="Fireball casts after announcing X=2 via the new spinbox")


_LIGHTNING_BOLT_COLOR = (230, 0, 0)  # mono-red placeholder, same value as _FIREBALL_COLOR


def scenario_turn_structure_gate(ctx):
    """Live-verifies the sorcery-speed casting-timing gate
    (doc/commander-status/phase4-turn-structure.md's "Turn-structure and timing enforcement"
    section) end to end, the same one-player local hotseat trick as scenario_mana_gate().
    .uitest/turnstructuretest.cod draws Plains (a land -- sorcery-speed by rule 305.1, so
    timing-gated even though it's never mana-cost-gated) and Lightning Bolt (an instant,
    deliberately exempt from the gate) -- both already in .uitest/sample_cards.xml.

    A fresh local game starts on phase 0 (Untap). Building a 2-card hand deliberately does
    *not* reuse scenario_mana_gate()'s old "click the Draw phase button 3 times" trick: that
    trick's first click relied on jumping straight from whatever phase to Draw (phase 2),
    which the new canAdvanceToPhase() enforcement this scenario is testing now correctly
    rejects -- confirmed live the hard way (that click now gets RespContextError and no card
    is drawn at all). Instead this steps forward legally via the pre-existing "Next Phase"
    shortcut (plain Tab, Untap(0)->Upkeep(1)->Draw(2)) -- entering Draw auto-draws 1 card
    (playerCount==1 in a local hotseat game, so rule 103.8a's two-player-only first-draw skip
    never applies -- see phase4-turn-structure.md), then a single click on the now-*already-
    active* Draw phase button fires its double-click convenience (Command_DrawCards) for the
    2nd card, same mechanism scenario_mana_gate() relied on for its 2nd/3rd clicks, just not
    for the phase-jumping 1st one. This conveniently lands the hand on phase 2 (Draw) --
    still not a main phase, so the "wrong timing" case needs no further phase setup.
    """
    print("[turn_structure_gate] stepping Untap->Upkeep->Draw and drawing the starting 2-card hand")
    for _ in range(2):
        ctx.key("Tab")  # legal one-step-forward advance
        ctx.sleep(0.5)
    ctx.click(23, 190)  # Draw phase button, already active -- fires its double-click draw action
    ctx.sleep(0.5)

    samples = _hand_colors(2)
    print(f"  sampled hand colors: {samples}")
    plains_x = next((x for x, color in samples if color != _LIGHTNING_BOLT_COLOR), None)
    bolt_x = next((x for x, color in samples if color == _LIGHTNING_BOLT_COLOR), None)
    if plains_x is None or bolt_x is None:
        print(f"  [FAIL] expected one red (Lightning Bolt) and one non-red (Plains) hand slot, sampled {samples}")
        ctx.ok = False
        return False

    print("[turn_structure_gate] casting Plains during Draw (phase 2, not a main phase) -- expect a block")
    ctx.click(plains_x, HAND_ROW_Y)
    ctx.sleep(1.0)
    line = _wait_for_log(CLIENT_LOG, _moved_to_table_pattern("Plains"), timeout=2)
    if line is not None:
        print(f"  [FAIL] Plains was cast outside a main phase -> {line.strip()}")
        ctx.ok = False
    else:
        print("  [PASS] Plains was correctly blocked outside a main phase (nothing logged)")
        ctx.key("Return")  # dismiss the new "Cannot Play" timing dialog
        ctx.sleep(0.3)

    print("[turn_structure_gate] +1 Red mana, then casting Lightning Bolt during Draw -- instants are exempt")
    ctx.click(*_MANA_COUNTER_XY["r"])
    ctx.sleep(0.3)
    ctx.click(bolt_x, HAND_ROW_Y)
    ctx.sleep(1.0)
    if not ctx.assert_log(CLIENT_LOG, _moved_to_table_pattern("Lightning Bolt"), timeout=5,
                          label="Lightning Bolt (an instant) is exempt from the sorcery-speed gate"):
        return False

    print("[turn_structure_gate] stepping to First Main (phase 3) via the pre-existing Next Phase shortcut (Tab)")
    ctx.key("Tab")
    ctx.sleep(0.5)
    if not ctx.assert_log(CLIENT_LOG, r"Event_SetActivePhase\.ext.*phase: 3\b", timeout=5,
                          label="a legal one-step Tab press reaches First Main (phase 3)"):
        return False

    print("[turn_structure_gate] casting Plains again, now in First Main -- expect it to succeed")
    ctx.click(_HAND_SLOT_CENTERS[1][0], HAND_ROW_Y)  # only Plains remains in hand
    ctx.sleep(1.0)
    return ctx.assert_log(CLIENT_LOG, _moved_to_table_pattern("Plains"), timeout=5,
                          label="Plains casts once in the controller's own main phase")


def scenario_counter_ui_gate(ctx):
    """Live-verifies the command-zone/counter UI cleanup: the command-zone/avatar overlap fix
    (player_graphics_item.cpp's commandZoneY derivation), the auto-tracked read-only Storm
    counter (TextCounter, Server_Player::onCardBeingMoved()/resetStormCount()), and the
    Commander Tax badge now rendered on the command zone itself instead of the counter column
    (CommanderCounterNames::isTaxCounter()). .uitest/countertest.cod's bannerCard is Atraxa,
    Praetors' Voice (a Legendary Creature, so a legal commander -- routed to the command zone
    by Server_Player::setupZones() instead of the library), plus Plains and Lightning Bolt --
    the same 2-card-hand shape as turnstructuretest.cod, reusing its hand-building sequence.

    Screenshots are taken for the parts that are genuinely visual (layout/overlap, label text)
    rather than asserted against the log -- read them with the Read tool afterward.
    """
    print("[counter_ui_gate] stepping Untap->Upkeep->Draw and drawing the starting 2-card hand")
    for _ in range(2):
        ctx.key("Tab")
        ctx.sleep(0.5)
    ctx.click(23, 190)  # Draw phase button, already active -- fires its double-click draw action
    ctx.sleep(0.5)

    print("[counter_ui_gate] screenshotting the board: command zone (with its Tax badge) vs. avatar overlap")
    ctx.shot("/tmp/counter_ui_gate_board.png")

    samples = _hand_colors(2)
    print(f"  sampled hand colors: {samples}")
    plains_x = next((x for x, color in samples if color != _LIGHTNING_BOLT_COLOR), None)
    bolt_x = next((x for x, color in samples if color == _LIGHTNING_BOLT_COLOR), None)
    if plains_x is None or bolt_x is None:
        print(f"  [FAIL] expected one red (Lightning Bolt) and one non-red (Plains) hand slot, sampled {samples}")
        ctx.ok = False
        return False

    print("[counter_ui_gate] casting Lightning Bolt -- expect Storm (counter id 7) to auto-increment to 1")
    ctx.click(*_MANA_COUNTER_XY["r"])
    ctx.sleep(0.3)
    ctx.click(bolt_x, HAND_ROW_Y)
    ctx.sleep(1.0)
    if not ctx.assert_log(CLIENT_LOG, _moved_to_table_pattern("Lightning Bolt"), timeout=5,
                          label="Lightning Bolt is cast"):
        return False
    if not ctx.assert_log(CLIENT_LOG, r"Event_SetCounter\.ext.*counter_id: 7\b.*value: 1\b", timeout=5,
                          label="Storm auto-increments to 1 after casting a spell"):
        return False

    print("[counter_ui_gate] screenshotting again: Storm should read as plain text, no colored circle")
    ctx.shot("/tmp/counter_ui_gate_storm.png")

    print("[counter_ui_gate] left-clicking on Storm's text -- expect no reaction (read-only, interactive=false)")
    # A left-click on an ordinary counter is its quick +1 gesture (AbstractCounter::mousePressEvent) --
    # this is the actual interaction TextCounter's interactive=false is meant to suppress, more so than
    # a plain right-click (which is the quick -1 gesture, not the menu -- that's middle-click/shift-click).
    ctx.click(40, 452, button=1)
    ctx.sleep(0.5)
    ctx.shot("/tmp/counter_ui_gate_storm_rightclick.png")
    if _wait_for_log(CLIENT_LOG, r"Event_SetCounter\.ext.*counter_id: 7\b.*value: 2\b", timeout=2) is not None:
        print("  [FAIL] Storm reacted to a click -- it should be read-only")
        ctx.ok = False
        return False
    print("  [PASS] Storm did not react to a click (still read-only)")

    print("[counter_ui_gate] stepping Draw(2) -> End/Cleanup(10), then ending the turn")
    for _ in range(8):
        ctx.key("Tab")
        ctx.sleep(0.4)
    if not ctx.assert_log(CLIENT_LOG, r"Event_SetActivePhase\.ext.*phase: 10\b", timeout=5,
                          label="reached End/Cleanup (phase 10)"):
        return False

    ctx.key_combo("ctrl", "Return")  # Next Turn (Player/aNextTurn)
    ctx.sleep(1.0)
    if not ctx.assert_log(CLIENT_LOG, r"Event_SetActivePhase\.ext.*phase: 0\b", timeout=5,
                          label="Next Turn lands back on Untap (phase 0)"):
        return False
    return ctx.assert_log(CLIENT_LOG, r"Event_SetCounter\.ext.*counter_id: 7\b.*value: 0\b", timeout=5,
                          label="Storm resets to 0 at the start of the new turn")


SCENARIOS = {
    "connect": scenario_connect,
    "mana_gate": scenario_mana_gate,
    "variable_mana_gate": scenario_variable_mana_gate,
    "turn_structure_gate": scenario_turn_structure_gate,
    "counter_ui_gate": scenario_counter_ui_gate,
}

# Scenarios needing a fresh local hotseat game (see ensure_local_game_profile()) rather than
# the Home tab a plain client launch lands on. Each needs its own fixture deck -- see
# LOCAL_GAME_DECKS.
LOCAL_GAME_SCENARIOS = {"mana_gate", "variable_mana_gate", "turn_structure_gate", "counter_ui_gate"}


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    cmd = sys.argv[1]
    if cmd == "setup":
        local_game = "--local-game" in sys.argv[2:]
        # Optional trailing scenario name picks which LOCAL_GAME_DECKS entry to load --
        # defaults to "mana_gate" for backward compatibility with the plain `--local-game` form.
        deck_scenario = next((a for a in sys.argv[2:] if a in LOCAL_GAME_DECKS), "mana_gate")
        setup(local_game=local_game, deck_path=LOCAL_GAME_DECKS[deck_scenario] if local_game else None)
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
        local_names = [n for n in names if n in LOCAL_GAME_SCENARIOS]
        setup(local_game=bool(local_names), deck_path=LOCAL_GAME_DECKS[local_names[0]] if local_names else None)
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
