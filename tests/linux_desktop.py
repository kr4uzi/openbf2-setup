#!/usr/bin/env python3
"""Run only desktop integration against a fresh HOME; do not install anything in the user's menu."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="openbf2-desktop-check-") as temporary:
    root = Path(temporary)
    home = root / "home"
    home.mkdir()
    app = root / 'app with spaces $dollar `tick` "quote" %percent'
    (app / "bin").mkdir(parents=True)
    shutil.copy2(sys.argv[1], app / "bin/OpenBF2")
    icon = "share/icons/hicolor/256x256/apps/org.openbf2.Setup.png"
    (app / icon).parent.mkdir(parents=True)
    shutil.copy2(sys.argv[2], app / icon)
    for relative in ("Registry.con", "ProtectedData.key", "Profiles/fixture.con", "CustomMods/test/mod.desc", "Setup/Current.txt"):
        path = home / ".local/share/OpenBF2" / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("existing data: " + relative)
    support = home / ".local/share/OpenBF2"
    original = {p.relative_to(support): p.read_bytes() for p in support.rglob("*") if p.is_file()}
    environment = {**os.environ, "HOME": str(home), "XDG_DATA_HOME": "relative-is-ignored", "XDG_CONFIG_HOME": str(root / "config"), "XDG_CACHE_HOME": str(root / "cache"), "QT_QPA_PLATFORM": "offscreen"}
    environment.pop("APPIMAGE", None)
    environment.pop("FLATPAK_ID", None)
    def client_check(expected):
        result = subprocess.run([str(app / "bin/OpenBF2"), "--check-client"], env=environment, capture_output=True, text=True)
        assert result.returncode == expected, result.stdout + result.stderr
    client_check(1)
    # This check uses the real application's ClientLayout without executing a client.
    client_files = ("bin/bf2_client", "lib/libdice_py.so", "lib/libaidll.so",
                    "lib/libdxvk_d3d9.so.0", "lib/libSDL3.so.0", "lib/libopenal.so.1")
    for name in client_files:
        target = app / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text("presence fixture; never executed")
    client_check(1)
    (app / "bin/bf2_client").chmod(0o755)
    client_check(0)
    (app / client_files[-1]).unlink()
    client_check(1)
    for name in client_files[:-1]:
        (app / name).unlink()
    for _ in range(2):
        for path in app.rglob("*"):
            path.chmod(0o555 if path.is_dir() or path.name == "OpenBF2" else 0o444)
        app.chmod(0o555)
        subprocess.run([str(app / "bin/OpenBF2"), "--install-desktop"], env=environment, check=True)
        desktop = home / ".local/share/applications/org.openbf2.Setup.desktop"
        commands = [line.removeprefix("Exec=") for line in desktop.read_text().splitlines() if line.startswith("Exec=")]
        # Desktop string unescaping happens before tokenizing the Exec line; %% is literal %.
        # shlex intentionally retains backslashes before $ and ` inside quotes;
        # Desktop Exec consumes those escapes without shell expansion.
        decoded = [shlex.split(line.replace("\\\\", "\\"))[0].replace("\\$", "$" ).replace("\\`", "`").replace("%%", "%") for line in commands]
        assert decoded == [str(app / "bin/OpenBF2")] * 2, "desktop quoting or relocation failed"
        assert commands[1].endswith(" --launcher"), "desktop launcher action missing"
        assert {p.relative_to(support): p.read_bytes() for p in support.rglob("*") if p.is_file()} == original, "application replacement changed user data"
        app.chmod(0o755)
        for path in app.rglob("*"):
            if path.is_dir(): path.chmod(0o755)
        moved = app.with_name(app.name + " relocated")
        app.rename(moved)
        app = moved
    print("Desktop entry quoting, relocation, read-only application and user-data preservation passed")
