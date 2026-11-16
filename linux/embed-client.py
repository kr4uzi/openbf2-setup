#!/usr/bin/env python3
"""Embed a local engine artifact; never modify inputs, fetch, or build the engine."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

import package


def target_path(root, name):
    relative = Path(name)
    package.require(not relative.is_absolute() and relative.parts and ".." not in relative.parts
                    and relative.as_posix() == name and relative.parts[0] in ("bin", "lib", "share"),
                    f"Unsafe embedded path: {name}")
    path = root
    for part in relative.parts:
        path /= part
        package.require(not path.is_symlink(), f"Symlink destination: {path}")
    return path


def replace_file(source, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".embed-", dir=target.parent)
    os.close(descriptor)
    try:
        shutil.copy2(source, temporary)
        os.replace(temporary, target)
    finally:
        Path(temporary).unlink(missing_ok=True)


def embed(source, destination, development=False):
    package.require(not destination.is_symlink(), "Destination must be a real directory")
    destination = destination.resolve()
    record = target_path(destination, package.EMBEDDED)
    previous = {}
    if record.exists():
        data = json.loads(record.read_text())
        package.require(data.get("schema") == 1 and isinstance(data.get("files"), dict), "Invalid embedded-client inventory")
        previous = data["files"]

    inventory = {}
    if source is not None:
        package.require(not source.is_symlink(), "Source must be a real artifact directory")
        source = source.resolve()
        package.require(not source.is_relative_to(destination) and not destination.is_relative_to(source),
                        "Source and destination must not overlap")
        paths = package.artifact_files(source, "engine")
        if not development:
            package.toolchain_metadata(package.metadata(source, "engine-artifact.json"))
        package.audit(source, engine=True, development=development)
        inventory = {path.relative_to(source).as_posix(): package.digest(path) for path in paths}

    # Validate every overwrite/removal before changing anything. The inventory only
    # owns files this helper copied; unrelated build/install files stay untouched.
    for name, checksum in previous.items():
        package.require(name != package.EMBEDDED and name not in package.APP, f"Invalid owned path: {name}")
        target = target_path(destination, name)
        package.require(not target.exists() or (target.is_file() and package.digest(target) == checksum),
                        f"Previously embedded file was modified: {name}")
    for name in inventory:
        target = target_path(destination, name)
        package.require(name in previous or not target.exists(), f"Refusing to overwrite an unowned file: {name}")
    for name, checksum in inventory.items():
        target = target_path(destination, name)
        if not target.exists() or package.digest(target) != checksum or target.stat().st_mode != (source / name).stat().st_mode:
            replace_file(source / name, target)
    for name in previous.keys() - inventory.keys():
        target_path(destination, name).unlink(missing_ok=True)
    if source is None:
        record.unlink(missing_ok=True)
        return

    record.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".embed-", dir=record.parent)
    try:
        with os.fdopen(descriptor, "w") as stream:
            json.dump({"schema": 1, "development": development, "files": inventory}, stream, indent=2)
            stream.write("\n")
        os.chmod(temporary, 0o644)
        os.replace(temporary, record)
    finally:
        Path(temporary).unlink(missing_ok=True)
    if development:
        print("WARNING: embedded a development client; this application cannot be released", file=sys.stderr)
    print(f"Embedded {len(inventory)} engine artifact files (source unchanged)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", default="", help="Empty removes only previously embedded files")
    parser.add_argument("--destination", required=True, type=Path)
    parser.add_argument("--allow-development", action="store_true")
    args = parser.parse_args()
    embed(Path(args.source) if args.source else None, args.destination, args.allow_development)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        sys.exit(f"linux-embed-client: {error}")
