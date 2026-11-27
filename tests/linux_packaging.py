#!/usr/bin/env python3
"""Negative packaging checks use synthetic ELF only; never execute a client or need game bytes."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest

spec = importlib.util.spec_from_file_location("package", Path(__file__).resolve().parents[1] / "linux/package.py")
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


def write_metadata(root, kind="engine", sdk="example.invalid/openbf2-sdk@sha256:" + "3" * 64):
    license = root / f"share/licenses/{kind}/fixture.txt"
    license.parent.mkdir(parents=True, exist_ok=True)
    license.write_text("Fixture license")
    record = root / f"share/openbf2/{kind}-artifact.json"
    record.parent.mkdir(parents=True, exist_ok=True)
    data = {"schema": 1, "architecture": "x86_64", "glibc": "2.35", "revision": "1" * 40,
            "sdk": sdk, "toolchain": json.loads((Path(package.__file__).parent / "toolchain.json").read_text()),
            "dependencies": [{"name": kind, "version": "1", "source": "https://example.invalid/fixture", "sha256": "2" * 64,
                              "license": license.relative_to(root).as_posix()}],
            "files": {p.relative_to(root).as_posix(): package.digest(p) for p in package.files(root) if p != record}}
    record.write_text(json.dumps(data))


class Packaging(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="openbf2-package-check-")
        self.root = Path(self.temp.name) / "application with spaces"
        self.root.mkdir()
        source = Path(self.temp.name) / "fixture.c"
        source.write_text("int fixture(void) { return 7; }\n")
        self.elf = Path(self.temp.name) / "fixture.so"
        subprocess.run([os.environ.get("CC", "cc"), "-shared", "-nostdlib", str(source), "-o", str(self.elf), "-Wl,-rpath,$ORIGIN"], check=True)
        binary_names = package.CLIENT + package.DYNAMIC_AUDIO + ("bin/OpenBF2",) + package.PLUGINS + ("lib/qt6/plugins/platforms/libqwayland.so",)
        for name in binary_names:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(self.elf, path)
        for name in package.APP:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            if not path.exists():
                path.write_text("fixture\n")

    def tearDown(self):
        self.temp.cleanup()

    def test_relocation_readonly_and_archive(self):
        moved = self.root.with_name("relocated application")
        self.root.rename(moved)
        (moved / "share/openbf2/release.json").write_text(json.dumps({"schema": 1, "architecture": "x86_64", "glibc": "2.35", "engine_revision": "1" * 40, "setup_revision": "2" * 40,
            "sdk": "example.invalid/sdk@sha256:" + "3" * 64, "toolchain": json.loads((Path(package.__file__).parent / "toolchain.json").read_text())}))
        package.checksum(moved)
        for file in package.files(moved):
            file.chmod(0o555 if package.elf(file) else 0o444)
        package.audit(moved)
        output = Path(self.temp.name) / "test.tar.gz"
        package.archive(moved, output)
        self.assertTrue(output.with_name(output.name + ".sha256").is_file())
        with self.assertRaisesRegex(ValueError, "outside the application"):
            package.archive(moved, moved / "recursive.tar.gz")

    def test_absolute_runtime_path(self):
        subprocess.run(["patchelf", "--set-rpath", "/build/owner/lib", str(self.root / "lib/libaidll.so")], check=True)
        with self.assertRaisesRegex(ValueError, "Absolute build path|runtime path"):
            package.audit(self.root)

    def with_strings(self, path, values):
        source = Path(self.temp.name) / "paths.c"
        source.write_text("const char *paths[] = {" + ",".join(json.dumps(value) for value in values) + "};\n")
        subprocess.run(["cc", "-shared", "-nostdlib", str(source), "-o", str(path), "-Wl,-rpath,$ORIGIN"], check=True)

    def test_runtime_templates_and_similar_prefixes_are_allowed(self):
        self.with_strings(self.root / "lib/libSDL3.so.0", ["/tmp/SDL-tray-XXXXXX", "/tmp/alsa-dmix-%i-%li-%li",
                          "/build/foobar/file.cpp", "/build/foo-other/file.cpp", "/build/foo extra/file.cpp"])
        package.audit(self.root, roots=("/build/foo", "/tmp/source build"))

    def test_explicit_build_paths_rejected(self):
        for value in ("/build/foo", "/build/foo/file.cpp", "/tmp/source build/src/file.cpp"):
            with self.subTest(value=value):
                self.with_strings(self.root / "lib/libSDL3.so.0", [value])
                with self.assertRaisesRegex(ValueError, "Build/source directory embedded"):
                    package.audit(self.root, roots=("/build/foo", "/tmp/source build"))

    def test_roots_file_validation(self):
        filename = Path(self.temp.name) / "roots.json"
        filename.write_text(json.dumps(["/build/foo/", "/tmp/source build"]))
        self.assertEqual(package.build_roots(["/build/foo"], filename), ("/build/foo", "/tmp/source build"))
        for invalid in ([], {}, "relative", ["relative"], ["/"], ["//"], ["/tmp/invalid\0root"], [7]):
            filename.write_text(json.dumps(invalid))
            with self.assertRaises(ValueError):
                package.build_roots(filename=filename)
        with self.assertRaises(FileNotFoundError):
            package.build_roots(filename=filename.with_name("missing.json"))
        with self.assertRaisesRegex(ValueError, "outside artifact"):
            package.external_roots_file(filename, [Path(self.temp.name)])

    def test_escape_runtime_path(self):
        subprocess.run(["patchelf", "--set-rpath", "$ORIGIN/../../outside", str(self.root / "bin/OpenBF2")], check=True)
        with self.assertRaisesRegex(ValueError, "escapes application"):
            package.audit(self.root)

    def test_missing_dynamic_library(self):
        (self.root / "lib/libopenal.so.1").unlink()
        with self.assertRaisesRegex(ValueError, "Missing required"):
            package.audit(self.root)

    def test_missing_needed_library(self):
        subprocess.run(["patchelf", "--add-needed", "libmissing.so.1", str(self.root / "bin/OpenBF2")], check=True)
        with self.assertRaisesRegex(ValueError, "Unresolved bundled"):
            package.audit(self.root)

    def test_symlink_and_driver_rejected(self):
        alias = self.root / "lib/escape.so"
        alias.symlink_to(self.elf)
        with self.assertRaisesRegex(ValueError, "symlinks"):
            package.audit(self.root)
        alias.unlink()
        shutil.copy2(self.elf, self.root / "lib/libvulkan.so.1")
        with self.assertRaisesRegex(ValueError, "Do not bundle"):
            package.audit(self.root)

    def test_new_libc_rejected(self):
        source = Path(self.temp.name) / "new.c"
        source.write_text("int new_function(void) { return 0; }\n")
        versions = Path(self.temp.name) / "versions.txt"
        versions.write_text("GLIBC_2.36 { global: new_function; };\n")
        new = self.root / "lib/libnew.so"
        subprocess.run(["cc", "-shared", "-nostdlib", str(source), "-o", str(new), f"-Wl,--version-script={versions}", "-Wl,-soname,libnew.so", "-Wl,-rpath,$ORIGIN"], check=True)
        source.write_text("extern int new_function(void); int fixture(void) { return new_function(); }\n")
        subprocess.run(["cc", "-shared", "-nostdlib", str(source), "-o", str(self.root / "bin/OpenBF2"), "-L" + str(new.parent), "-lnew", "-Wl,-rpath,$ORIGIN/../lib"], check=True)
        with self.assertRaisesRegex(ValueError, "GLIBC requirement exceeds"):
            package.audit(self.root)

    def test_metadata_checksums(self):
        license = self.root / "share/licenses/test.txt"
        license.parent.mkdir(parents=True); license.write_text("Fixture license")
        data = {"schema": 1, "architecture": "x86_64", "glibc": "2.35", "revision": "1" * 40,
                "dependencies": [{"name": "fixture", "version": "1", "source": "https://example.invalid/fixture", "sha256": "2" * 64, "license": str(license.relative_to(self.root))}],
                "files": {str(p.relative_to(self.root)): package.digest(p) for p in package.files(self.root)}}
        record = self.root / "share/openbf2/engine-artifact.json"
        record.write_text(json.dumps(data))
        package.metadata(self.root, record.name)
        license.write_text("Changed")
        with self.assertRaisesRegex(ValueError, "checksums/inventory differ"):
            package.metadata(self.root, record.name)

    def inputs(self):
        app, engine, runtime = (Path(self.temp.name) / name for name in ("setup", "engine", "runtime"))
        groups = ((app, package.APP), (engine, package.CLIENT + package.DYNAMIC_AUDIO),
                  (runtime, package.PLUGINS + ("lib/qt6/plugins/platforms/libqwayland.so", "lib/libSDL3.so.0")))
        for root, names in groups:
            for name in names:
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(self.root / name, target)
        write_metadata(engine)
        write_metadata(runtime, "runtime")
        return SimpleNamespace(app=app, engine=engine, runtime=runtime, output=Path(self.temp.name) / "assembled", setup_revision="4" * 40, version="0.3.0-rc2")

    def test_assembly_and_provenance(self):
        args = self.inputs()
        before = {str(p): package.digest(p) for root in (args.app, args.engine, args.runtime) for p in package.files(root)}
        for path in before:
            file = Path(path)
            file.chmod(0o555 if os.access(file, os.X_OK) else 0o444)
        package.assemble(args)
        package.validate_release(args.output)
        record = json.loads((args.output / "share/openbf2/release.json").read_text())
        self.assertEqual(record["engine_revision"], "1" * 40)
        self.assertEqual(record["setup_revision"], args.setup_revision)
        self.assertEqual(record["version"], "0.3.0-rc2")
        self.assertEqual(record["sdk"], json.loads((args.engine / "share/openbf2/engine-artifact.json").read_text())["sdk"])
        self.assertTrue(all(package.digest(Path(path)) == checksum for path, checksum in before.items()))

    def test_mismatched_sdk_and_shared_dependency(self):
        args = self.inputs()
        write_metadata(args.runtime, "runtime", "example.invalid/different@sha256:" + "5" * 64)
        with self.assertRaisesRegex(ValueError, "same immutable SDK"):
            package.assemble(args)
        self.assertFalse(args.output.exists())
        with (args.runtime / "lib/libSDL3.so.0").open("ab") as stream:
            stream.write(b"different build")
        write_metadata(args.runtime, "runtime")
        with self.assertRaisesRegex(ValueError, "Conflicting dependency"):
            package.assemble(args)
        self.assertFalse(args.output.exists())

    def test_artifact_ownership(self):
        args = self.inputs()
        (args.engine / "bin/OpenBF2").write_text("wrong owner")
        write_metadata(args.engine)
        with self.assertRaisesRegex(ValueError, "replace setup files"):
            package.assemble(args)

    def test_runtime_build_roots_checked_before_assembly(self):
        args = self.inputs()
        self.with_strings(args.runtime / package.PLUGINS[0], ["/build/qt-source/src/plugin.cpp"])
        write_metadata(args.runtime, "runtime")
        with self.assertRaisesRegex(ValueError, "Build/source directory embedded"):
            package.assemble(args, roots=("/build/qt-source",))
        self.assertFalse(args.output.exists())

    def test_development_embedding_cannot_be_released(self):
        args = self.inputs()
        (args.app / package.EMBEDDED).write_text(json.dumps({"development": True}))
        with self.assertRaisesRegex(ValueError, "cannot be released"):
            package.assemble(args)
        self.assertFalse(args.output.exists())


if __name__ == "__main__":
    unittest.main()
