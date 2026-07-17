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
    python3 .uitest/scenario.py setup              # ensure Xvfb+servatrice+client running
    python3 .uitest/scenario.py teardown            # kill servatrice+client (keeps Xvfb)
    python3 .uitest/scenario.py teardown --all       # also kill Xvfb
    python3 .uitest/scenario.py list                  # list available scenarios
    python3 .uitest/scenario.py run <name> [<name> ...]  # setup + run named scenario(s)

Add a new scenario by writing a function `scenario_<name>(ctx) -> bool` and
registering it in SCENARIOS below.
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


def ensure_client():
    if _running(str(CLIENT_BIN)):
        print("[setup] client already running")
        return
    if not CLIENT_BIN.exists():
        raise RuntimeError(f"cockatrice binary not found at {CLIENT_BIN}")
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


def setup():
    ensure_xvfb()
    ensure_servatrice()
    ensure_client()


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
    -- this scenario is meant to run once per fresh client)."""
    print("[connect] opening Connect to Server dialog")
    # Home tab's "Connect" button (top action in the Home tab's button list).
    ctx.click(376, 320)
    ctx.sleep(0.7)
    # Use "New Host" explicitly and type host/port -- don't rely on a "Known
    # Hosts" entry that only exists if a prior manual session saved one.
    ctx.click(278, 114)  # "New Host" radio button
    ctx.sleep(0.2)
    ctx.click(383, 144)  # Name field (required to save the new host profile)
    ctx.type_text("scenario-test")
    ctx.click(383, 175)  # Host field
    ctx.key_combo("ctrl", "a")
    ctx.type_text("127.0.0.1")
    ctx.click(383, 206)  # Port field
    ctx.key_combo("ctrl", "a")
    ctx.type_text("4747")
    ctx.click(404, 417)  # Player name field
    ctx.key_combo("ctrl", "a")
    ctx.type_text("testuser")
    ctx.click(388, 550)  # Connect button
    ctx.sleep(1.5)
    return ctx.assert_log(
        CLIENT_LOG,
        r"Event_ServerIdentification|Event_Login|ReloginOrLoginResponse|Response_Login",
        timeout=10,
        label="client receives a login/identification response from servatrice",
    )


SCENARIOS = {
    "connect": scenario_connect,
}


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    cmd = sys.argv[1]
    if cmd == "setup":
        setup()
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
        setup()
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
