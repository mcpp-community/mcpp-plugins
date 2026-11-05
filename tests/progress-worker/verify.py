"""Exercise the standalone writer's environment and file contract on this host."""

import ctypes
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


compiler = str(Path(sys.argv[1]).resolve())
project = Path(__file__).resolve().parent
environment = os.environ.copy()
environment.pop("MCPP_ACTION_PROGRESS_FILE", None)
environment.pop("MCPP_ACTION_PROGRESS_ROOT", None)

if os.name == "nt":
    code_page = ctypes.windll.kernel32.GetACP()
    print(f"Windows ACP: {code_page}", flush=True)
    if code_page == 65001:
        print("UTF-8 ACP: this run validates paths without reproducing ANSI loss.", flush=True)


def run(env, probe=False):
    command = [compiler, "run"]
    if probe:
        command += ["--", "probe"]
    subprocess.run(command, cwd=project, env=env, check=True, timeout=600)


run(environment)
with tempfile.TemporaryDirectory(prefix="mcpp-progress-") as directory:
    root = Path(directory) / "channel-通道 Ω"
    root.mkdir()
    channel = root / "snapshot.json"
    run(dict(environment, MCPP_ACTION_PROGRESS_FILE=str(channel)), probe=True)
    snapshot = json.loads(channel.read_text(encoding="utf-8"))
    assert snapshot == dict(v=1, seq=1, phase="compile", done=1, total=1), snapshot
    # A directory cannot be replaced with the snapshot file. Telemetry failure
    # remains optional for a worker whose actual task succeeds.
    run(dict(environment, MCPP_ACTION_PROGRESS_FILE=str(root)))

print("PASS: disabled, Unicode channel and optional write failure", flush=True)
