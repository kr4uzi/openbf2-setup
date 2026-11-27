#!/usr/bin/env python3
"""Offline Linux assembly and ELF audit. Never executes an input binary or uses ldd."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile

BASELINE = (2, 35)
EMBEDDED = "share/openbf2/setup/embedded-client.json"
CLIENT = ("bin/bf2_client", "lib/libdice_py.so", "lib/libaidll.so",
          "lib/libdxvk_d3d9.so.0", "lib/libSDL3.so.0", "lib/libopenal.so.1")
DYNAMIC_AUDIO = ("lib/libpulse.so.0", "lib/libasound.so.2")
APP = ("bin/OpenBF2", "bin/qt.conf", "share/openbf2/setup/content-150.tsv",
       "share/openbf2/setup/content-deluxe-150.tsv", "share/openbf2/setup/content-deluxe-base-150.tsv", "share/openbf2/setup/content-complete-150.tsv",
       "share/icons/hicolor/256x256/apps/org.openbf2.Setup.png")
PLUGINS = ("lib/qt6/plugins/platforms/libqxcb.so", "lib/qt6/plugins/platforms/libqoffscreen.so",
           "lib/qt6/plugins/wayland-shell-integration/libxdg-shell.so",
           "lib/qt6/plugins/platformthemes/libqxdgdesktopportal.so",
           # the provider file downloaded by URL (https): QtNetwork's TLS backend, which loads the system's libssl
           "lib/qt6/plugins/tls/libqopensslbackend.so")
# Only libc, the kernel-facing window system and GPU driver/loader remain host dependencies.
# All other DT_NEEDED entries must resolve inside the artifact, including C++/GCC runtimes.
HOST = frozenset(("ld-linux-x86-64.so.2", "libc.so.6", "libm.so.6", "libdl.so.2", "libpthread.so.0",
                  "librt.so.1", "libresolv.so.2", "libutil.so.1", "libanl.so.1", "libnss_dns.so.2",
                  "libnss_files.so.2", "libvulkan.so.1", "libGL.so.1", "libGLX.so.0", "libOpenGL.so.0",
                  "libEGL.so.1", "libGLdispatch.so.0", "libdrm.so.2", "libgbm.so.1",
                  "libX11.so.6", "libX11-xcb.so.1", "libXext.so.6", "libXcursor.so.1", "libXi.so.6",
                  "libXfixes.so.3", "libXrandr.so.2", "libXrender.so.1", "libxcb.so.1",
                  "libwayland-client.so.0", "libwayland-cursor.so.0", "libwayland-egl.so.1"))


def require(ok, message):
    if not ok:
        raise ValueError(message)


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def command(*args):
    return subprocess.check_output(args, text=True, env={**os.environ, "LC_ALL": "C"})


def build_roots(roots=(), filename=None):
    roots = list(roots)
    if filename:
        recorded = json.loads(Path(filename).read_text())
        require(isinstance(recorded, list) and recorded, "Build roots file must contain a nonempty JSON array")
        roots.extend(recorded)
    result = set()
    for root in roots:
        require(isinstance(root, str) and Path(root).is_absolute() and not any(c in root for c in "\0\n\r\t"),
                "Build roots must be absolute directory paths")
        normalized = os.path.normpath(root)
        require(normalized not in ("/", "//"), "The filesystem root is not a build directory")
        result.add(root.rstrip("/"))
        result.add(normalized)
    return tuple(sorted(result))


def audit_build_paths(path, roots):
    # Runtime templates such as /tmp/SDL-tray-XXXXXX and /tmp/alsa-dmix-...
    # are valid. Only concrete source/build/SDK prefixes supplied by the builder
    # identify build leaks; directory names alone cannot distinguish the two.
    if roots:
        strings = command("strings", "-a", str(path))
        for root in roots:
            require(not re.search(re.escape(root) + r"(?=/|$)", strings, re.MULTILINE), f"Build/source directory embedded in ELF: {path}")


def add_build_root_options(parser):
    parser.add_argument("--build-root", action="append", default=[], help="Concrete absolute source/build/SDK directory to reject; repeatable")
    parser.add_argument("--build-roots-file", default="", help="Local JSON array of build roots (never packaged)")


def external_roots_file(filename, artifacts):
    if filename:
        path = Path(filename).resolve()
        require(not any(path.is_relative_to(Path(root).resolve()) for root in artifacts),
                "Build roots file must stay outside artifact/application directories")


def files(root):
    require(root.is_dir() and not root.is_symlink(), f"Not a real artifact directory: {root}")
    for path in sorted(root.rglob("*")):
        require(not path.is_symlink(), f"Artifact symlinks must be materialized: {path.relative_to(root)}")
        require(path.is_dir() or path.is_file(), f"Nonregular artifact entry: {path}")
        require(not any(c in str(path.relative_to(root)) for c in "\n\r\t"), f"Invalid artifact path: {path}")
        if path.is_file():
            yield path


def metadata(root, name):
    path = root / "share/openbf2" / name
    data = json.loads(path.read_text())
    require(data.get("schema") == 1 and data.get("architecture") == "x86_64" and data.get("glibc") == "2.35",
            f"Unsupported artifact contract: {path}")
    require(re.fullmatch(r"[0-9a-f]{40}", data.get("revision", "")), f"Missing revision: {path}")
    require(data.get("dependencies"), f"Missing dependency provenance: {path}")
    for dep in data["dependencies"]:
        require(all(dep.get(field) for field in ("name", "version", "source", "sha256", "license")), f"Incomplete dependency: {path}")
        require(re.fullmatch(r"[0-9a-f]{64}", dep["sha256"]), f"Invalid source checksum: {dep['name']}")
        license = Path(dep["license"])
        require(not license.is_absolute() and ".." not in license.parts and (root / license).is_file(), f"Missing license: {dep['name']}")
    recorded = data.get("files", {})
    actual = {str(p.relative_to(root)): digest(p) for p in files(root) if p != path}
    require(recorded == actual, f"Artifact checksums/inventory differ: {path}")
    return data


def toolchain_metadata(data):
    lock = json.loads((Path(__file__).parent / "toolchain.json").read_text())
    require(data.get("toolchain") == lock, "Artifact was not built with the pinned baseline SDK toolchain")
    require(re.fullmatch(r"[^\s]+@sha256:[0-9a-f]{64}", data.get("sdk", "")), "Missing immutable SDK image identity")
    return lock


def artifact_files(root, kind):
    paths = list(files(root))
    for path in paths:
        name = path.relative_to(root).as_posix()
        require(path.relative_to(root).parts[0] in ("bin", "lib", "share"), "Artifact files must be under bin/, lib/ or share/")
        require(name not in APP and name not in (EMBEDDED, "share/openbf2/release.json", "share/openbf2/files.sha256")
                and not name.startswith(("share/openbf2/setup/", "share/applications/", "share/metainfo/")),
                f"Artifact may not replace setup files: {name}")
        if kind == "engine":
            require(name != "share/openbf2/runtime-artifact.json", "Runtime metadata belongs to the runtime artifact")
            require(not name.startswith("lib/qt6/") and not path.name.startswith("libQt6"), f"Qt belongs to the setup runtime artifact: {name}")
        else:
            require(name != "share/openbf2/engine-artifact.json", "Engine metadata belongs to the engine artifact")
            require(name not in CLIENT[:4], f"Client belongs to the engine artifact: {name}")
    return paths


def check_inputs(engine, runtime, roots=()):
    engine_meta = metadata(engine, "engine-artifact.json")
    runtime_meta = metadata(runtime, "runtime-artifact.json")
    toolchain_metadata(engine_meta)
    toolchain_metadata(runtime_meta)
    require(engine_meta["sdk"] == runtime_meta["sdk"], "Engine and runtime must use the same immutable SDK")
    engine_files = {path.relative_to(engine): digest(path) for path in artifact_files(engine, "engine")}
    for path in artifact_files(runtime, "runtime"):
        name = path.relative_to(runtime)
        require(name not in engine_files or engine_files[name] == digest(path), f"Conflicting dependency: {name}")
        elf(path, roots=roots)
    audit(engine, engine=True, roots=roots)
    return engine_meta, runtime_meta


def elf(path, development=False, roots=()):
    with path.open("rb") as stream:
        if stream.read(4) != b"\x7fELF":
            return None
    header = command("readelf", "-hW", str(path))
    require("Advanced Micro Devices X86-64" in header and "ELF64" in header, f"Not x86-64 ELF: {path}")
    dynamic = command("readelf", "-dW", str(path))
    program = command("readelf", "-lW", str(path))
    interpreter = re.search(r"Requesting program interpreter: ([^\]]+)", program)
    require(not interpreter or interpreter[1] == "/lib64/ld-linux-x86-64.so.2", f"Unexpected ELF interpreter: {path}")
    versions = command("readelf", "-VW", str(path))
    # Check requirements only, not definitions exported by a bundled libstdc++.
    needs = versions.split("Version needs section", 1)[-1] if "Version needs section" in versions else ""
    glibc = re.findall(r"Name: GLIBC_([\d.]+)", needs)
    if not development:
        require(all(tuple(map(int, value.split("."))) <= BASELINE for value in glibc), f"GLIBC requirement exceeds 2.35: {path}")
        require("Name: GLIBC_PRIVATE" not in needs and "Name: GLIBC_ABI_" not in needs, f"Private/new libc ABI: {path}")
    notes = command("readelf", "-nW", str(path))
    if not development:
        require(not re.search(r"ISA needed:.*x86-64-v[234]", notes), f"Nonbaseline CPU ISA: {path}")
        audit_build_paths(path, roots)
    needed = re.findall(r"\(NEEDED\).*\[([^\]]+)\]", dynamic)
    require(all("/" not in name for name in needed), f"Absolute/path DT_NEEDED: {path}")
    paths = re.findall(r"\((?:RUNPATH|RPATH)\).*\[([^\]]*)\]", dynamic)
    soname = re.findall(r"\(SONAME\).*\[([^\]]+)\]", dynamic)
    require(all("/" not in name for name in soname), f"Path SONAME: {path}")
    return {"needed": needed, "rpaths": [entry for group in paths for entry in group.split(":")], "soname": soname, "versions": needs}


def audit(root, engine=False, development=False, roots=()):
    paths = list(files(root))
    require(all(path.relative_to(root).parts[0] in ("bin", "lib", "share") for path in paths), "Artifact files must be under bin/, lib/ or share/")
    required = CLIENT + DYNAMIC_AUDIO if engine else CLIENT + DYNAMIC_AUDIO + APP + PLUGINS
    for name in required:
        require((root / name).is_file(), f"Missing required artifact: {name}")
    if not engine:
        require(any((root / "lib/qt6/plugins/platforms" / name).is_file() for name in ("libqwayland-generic.so", "libqwayland.so")), "Missing Qt Wayland platform plugin")
    for name in ("bin/bf2_client",) if engine else ("bin/bf2_client", "bin/OpenBF2"):
        require(os.access(root / name, os.X_OK), f"Not executable: {name}")
    binaries = {path: info for path in paths if (info := elf(path, development, roots)) is not None}
    for name in (CLIENT + DYNAMIC_AUDIO if engine else CLIENT + DYNAMIC_AUDIO + ("bin/OpenBF2",) + PLUGINS):
        require(root / name in binaries, f"Required binary is not ELF: {name}")
    for path, info in binaries.items():
        relative = path.relative_to(root)
        require(relative.parts[0] in ("bin", "lib"), f"ELF outside bin/lib: {relative}")
        require(path.name not in HOST, f"Do not bundle libc or GPU/window-system drivers: {relative}")
        require(info["rpaths"], f"Missing relative ELF runtime path: {relative}")
        search = []
        for entry in info["rpaths"]:
            require(entry == "$ORIGIN" or entry.startswith("$ORIGIN/"), f"Absolute/empty/nonrelative runtime path: {relative}: {entry}")
            resolved = Path(entry.replace("$ORIGIN", str(path.parent))).resolve()
            require(resolved.is_relative_to(root.resolve()), f"Runtime path escapes application: {relative}")
            search.append(resolved)
        for dependency in info["needed"]:
            if dependency in HOST:
                continue
            candidate = next((directory / dependency for directory in search if directory / dependency in binaries), None)
            require(candidate is not None, f"Unresolved bundled dependency: {relative} -> {dependency}")
            # Symbol version requirements (GLIBCXX/CXXABI/Qt/etc.) must be exported by the selected file.
            blocks = re.split(r"File: ", info["versions"])[1:]
            block = next((block for block in blocks if block.split()[0] == dependency), "")
            definitions = command("readelf", "-VW", str(candidate)).split("Version needs section", 1)[0]
            exported = set(re.findall(r"Name: (\S+)", definitions))
            for version in re.findall(r"Name: (\S+)", block):
                require(version in exported, f"Missing symbol version {version}: {relative} -> {dependency}")
    return {str(path.relative_to(root)): digest(path) for path in paths}


def copy_tree(source, destination):
    for file in files(source):
        target = destination / file.relative_to(source)
        if target.exists():
            require(digest(file) == digest(target), f"Conflicting dependency: {file.relative_to(source)}")
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(file, target)


def assemble(args, roots=()):
    app, engine, runtime, output = (Path(value).resolve() for value in (args.app, args.engine, args.runtime, args.output))
    require(not output.exists(), f"Output exists: {output}")
    require(not any(output.is_relative_to(source) for source in (app, engine, runtime)), "Output must be outside inputs")
    reject_development(app)
    engine_meta, runtime_meta = check_inputs(engine, runtime, roots)
    lock = toolchain_metadata(engine_meta)
    require(re.fullmatch(r"[0-9a-f]{40}", args.setup_revision), "Invalid setup revision")
    require(re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.-]+)?", args.version), f"Invalid version: {args.version}")
    output.mkdir(parents=True)
    for source in (app, engine, runtime):
        copy_tree(source, output)
    # Normalize only the staging copy; absolute paths in source artifacts are an error,
    # not something to silently hide by patching them away.
    for path in files(output):
        info = elf(path, roots=roots)
        if info is None:
            continue
        require(all(value == "$ORIGIN" or value.startswith("$ORIGIN/") for value in info["rpaths"]), f"Build-machine RPATH in input: {path}")
        to_lib = os.path.relpath(output / "lib", path.parent)
        rpath = "$ORIGIN" if to_lib == "." else "$ORIGIN:$ORIGIN/" + to_lib
        mode = path.stat().st_mode
        path.chmod(mode | 0o200)  # Inputs may be read-only; only this staging copy is patched.
        try:
            subprocess.run(["patchelf", "--set-rpath", rpath, str(path)], check=True)
        finally:
            path.chmod(mode)
    audit(output, roots=roots)
    metadata_dir = output / "share/openbf2"
    # Input provenance retains the source checksums; final checksums describe patched ELF bytes.
    record = {"schema": 1, "version": args.version, "setup_revision": args.setup_revision, "engine_revision": engine_meta["revision"],
              "architecture": "x86_64", "glibc": "2.35", "toolchain": lock, "sdk": engine_meta["sdk"],
              "engine_artifact_sha256": digest(engine / "share/openbf2/engine-artifact.json"),
              "runtime_artifact_sha256": digest(runtime / "share/openbf2/runtime-artifact.json"),
              "dependencies": engine_meta["dependencies"] + runtime_meta["dependencies"]}
    (metadata_dir / "release.json").write_text(json.dumps(record, indent=2) + "\n")
    checksum(output)


def checksum(root):
    path = root / "share/openbf2/files.sha256"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("".join(f"{digest(file)}  {file.relative_to(root)}\n" for file in files(root) if file != path))


def reject_development(root):
    record = root / EMBEDDED
    if record.exists():
        require(json.loads(record.read_text()).get("development") is False, "Development client embedding cannot be released")


def validate_release(root, roots=()):
    reject_development(root)
    audit(root, roots=roots)
    record = json.loads((root / "share/openbf2/release.json").read_text())
    require(record.get("schema") == 1 and record.get("architecture") == "x86_64" and record.get("glibc") == "2.35", "Not a baseline release")
    toolchain_metadata(record)
    for field in ("engine_revision", "setup_revision"):
        require(re.fullmatch(r"[0-9a-f]{40}", record.get(field, "")), f"Missing release {field}")
    checksums = root / "share/openbf2/files.sha256"
    recorded = {}
    for line in checksums.read_text().splitlines():
        value, name = line.split("  ", 1)
        require(name not in recorded and re.fullmatch(r"[0-9a-f]{64}", value), "Malformed release checksum inventory")
        recorded[name] = value
    actual = {str(path.relative_to(root)): digest(path) for path in files(root) if path != checksums}
    require(recorded == actual, "Release checksum/inventory mismatch")


def archive(root, output, roots=()):
    require(not output.exists(), f"Output exists: {output}")
    require(not output.is_relative_to(root), "Archive output must be outside the application")
    validate_release(root, roots=roots)
    # A tar.gz is also the fallback when AppImage mounting is unavailable.
    with tarfile.open(output, "w:gz", format=tarfile.PAX_FORMAT) as tar:
        tar.add(root, arcname="OpenBF2")
    output.with_name(output.name + ".sha256").write_text(f"{digest(output)}  {output.name}\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    check = commands.add_parser("audit"); check.add_argument("root", type=Path); check.add_argument("--engine", action="store_true")
    release = commands.add_parser("validate-release"); release.add_argument("root", type=Path)
    runtime = commands.add_parser("audit-runtime"); runtime.add_argument("file", type=Path)
    inputs = commands.add_parser("check-inputs")
    inputs.add_argument("--engine", required=True, type=Path)
    inputs.add_argument("--runtime", required=True, type=Path)
    build = commands.add_parser("assemble")
    for name in ("app", "engine", "runtime", "output", "setup-revision"):
        build.add_argument("--" + name, required=True)
    build.add_argument("--version", default="0.0.0-dev", help="OpenBF2 release, such as 0.3.0-rc2 (the engine's tag without its v)")
    pack = commands.add_parser("archive"); pack.add_argument("root", type=Path); pack.add_argument("output", type=Path)
    for subparser in (check, release, runtime, inputs, build, pack):
        add_build_root_options(subparser)
    args = parser.parse_args()
    external_roots_file(args.build_roots_file, [getattr(args, name) for name in ("root", "app", "engine", "runtime")
                                               if hasattr(args, name) and isinstance(getattr(args, name), (str, Path))])
    roots = build_roots(args.build_root, args.build_roots_file)
    if args.command == "audit":
        audit(args.root.resolve(), args.engine, roots=roots); print("ELF architecture, libc baseline, runtime paths and closure passed")
    elif args.command == "assemble":
        assemble(args, roots)
    elif args.command == "check-inputs":
        check_inputs(args.engine, args.runtime, roots); print("Engine/runtime inventories, SDK identity and ELF audit passed")
    elif args.command == "validate-release":
        validate_release(args.root.resolve(), roots); print("Release ELF audit and checksum inventory passed")
    elif args.command == "audit-runtime":
        info = elf(args.file, roots=roots)
        require(info is not None and not info["rpaths"] and all(name in HOST for name in info["needed"]), "AppImage runtime must have no private library dependencies or search paths")
    else:
        archive(args.root.resolve(), args.output.resolve(), roots)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        sys.exit(f"linux-package: {error}")
