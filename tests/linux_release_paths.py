#!/usr/bin/env python3
"""Check the built app has no diagnostic scratch literal; preserve CLI restrictions."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

app, cli, scratch = (Path(arg).resolve() for arg in sys.argv[1:])
configured = os.fsencode(sys.argv[3])
assert configured not in app.read_bytes(), "Diagnostic scratch directory leaked into the distributed app"
assert configured in cli.read_bytes(), "Diagnostic CLI lost its configured scratch restriction"

with tempfile.TemporaryDirectory(prefix="diagnostic-paths-", dir=scratch) as inside, \
        tempfile.TemporaryDirectory(prefix="outside-diagnostic-scratch-") as outside:
    inside, outside = Path(inside), Path(outside)
    game, reference = inside / "game", inside / "reference"
    game.mkdir(); reference.mkdir()
    names = [f"fixture-{number}.txt" for number in range(125)]
    for name in names:
        (game / name).write_text(name)
        (reference / name).write_text(name)
    acceptance = inside / "acceptance.txt"
    acceptance.write_text("\n".join(names) + "\n")
    result = subprocess.run([str(cli), "compare", str(game), str(reference), str(acceptance)], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    assert (inside / "comparison.tsv").is_file()
    result = subprocess.run([str(cli), "compare", str(outside / "game"), str(reference), str(acceptance)], capture_output=True, text=True)
    assert result.returncode != 0 and "comparison report must stay" in result.stderr, result.stderr
    assert not (outside / "comparison.tsv").exists()
    result = subprocess.run([str(cli), "prototype", "unused.iso", "unused141.exe", "unused150.exe",
                             str(outside / "new-output"), str(reference), str(acceptance)], capture_output=True, text=True)
    assert result.returncode != 0 and "new direct child of SETUP_SCRATCH" in result.stderr, result.stderr
    assert not (outside / "new-output").exists()
print("Application has no diagnostic scratch path; CLI comparison and scratch restrictions passed")
