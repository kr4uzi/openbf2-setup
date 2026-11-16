#!/usr/bin/env python3
"""Exercise the actual CMake build/install hook and read-only artifact lifecycle."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from linux_packaging import package, write_metadata

sys.path.insert(0, str(Path(package.__file__).parent))
spec = importlib.util.spec_from_file_location("embed_client", Path(package.__file__).with_name("embed-client.py"))
embedding = importlib.util.module_from_spec(spec)
spec.loader.exec_module(embedding)


class Embedding(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="openbf2-embedding-")
        self.root = Path(self.temp.name)
        self.engine = self.root / "engine with spaces"
        source = self.root / "fixture.c"
        source.write_text("int fixture(void) { return 7; }\n")
        library = self.root / "fixture.so"
        subprocess.run(["cc", "-shared", "-nostdlib", str(source), "-o", str(library), "-Wl,-rpath,$ORIGIN"], check=True)
        for name in package.CLIENT + package.DYNAMIC_AUDIO:
            path = self.engine / name
            path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(library, path)
        # A runnable x86-64 ELF, independent of the host's libc version.
        source.write_text('void _start(void) { __asm__ volatile("mov $60, %rax; mov $7, %rdi; syscall"); }\n')
        subprocess.run(["cc", "-nostdlib", "-no-pie", str(source), "-o", str(self.engine / "bin/bf2_client"),
                        "-Wl,-rpath,$ORIGIN/../lib", "-Wl,--no-as-needed", "-L" + str(self.engine / "lib"), "-l:libaidll.so"], check=True)
        write_metadata(self.engine)
        self.destination = self.root / "build with spaces"

    def tearDown(self):
        self.temp.cleanup()

    def test_cmake_build_install_replace_disable(self):
        project = self.root / "project"
        project.mkdir()
        module = Path(package.__file__).with_name("client-artifact.cmake")
        (project / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.24)\nproject(EmbedCheck NONE)\n'
            'add_custom_target(OpenBF2 ALL)\n' + f'include("{module}")\n')
        roots_file = self.root / "producer-roots.json"
        roots_file.write_text(json.dumps(["/builder/engine", "/builder/sdk"]))
        def configure(source):
            subprocess.run(["cmake", "-G", "Ninja", "-S", str(project), "-B", str(self.destination), f"-DSETUP_CLIENT_ARTIFACT={source}",
                            f"-DSETUP_ARTIFACT_BUILD_ROOTS_FILE={roots_file}"], check=True, stdout=subprocess.DEVNULL)
        def build():
            subprocess.run(["cmake", "--build", str(self.destination), "--target", "OpenBF2"], check=True, stdout=subprocess.DEVNULL)
        before = {p.relative_to(self.engine): package.digest(p) for p in package.files(self.engine)}
        for path in package.files(self.engine):
            path.chmod(0o555 if os.access(path, os.X_OK) else 0o444)
        configure(self.engine)
        build()
        self.assertEqual(subprocess.run([str(self.destination / "bin/bf2_client")]).returncode, 7)
        self.assertTrue(all(package.digest(self.engine / name) == checksum for name, checksum in before.items()))
        prefix = self.root / "installation with spaces"
        subprocess.run(["cmake", "--install", str(self.destination), "--prefix", str(prefix)], check=True, stdout=subprocess.DEVNULL)
        destdir = self.root / "destdir"
        subprocess.run(["cmake", "--install", str(self.destination), "--prefix", "/usr"],
                       env={**os.environ, "DESTDIR": str(destdir)}, check=True, stdout=subprocess.DEVNULL)
        self.assertTrue((destdir / "usr/bin/bf2_client").exists())
        old_hash = package.digest(self.destination / "lib/libaidll.so")
        for path in package.files(self.engine):
            path.chmod(path.stat().st_mode | 0o200)
        changed = self.engine / "lib/libaidll.so"
        changed.chmod(0o755)
        original = changed.read_bytes()
        changed.write_bytes(original + b"\0/builder/engine/source.cpp\0")
        write_metadata(self.engine)
        for command in (["cmake", "--build", str(self.destination), "--target", "OpenBF2"],
                        ["cmake", "--install", str(self.destination), "--prefix", str(prefix)]):
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Build/source directory embedded", result.stdout + result.stderr)
        self.assertEqual(old_hash, package.digest(self.destination / "lib/libaidll.so"))
        changed.write_bytes(original)
        with changed.open("ab") as stream:
            stream.write(b"updated client")
        write_metadata(self.engine)
        build()
        self.assertNotEqual(old_hash, package.digest(self.destination / "lib/libaidll.so"))
        for root in (self.destination, prefix):
            (root / "lib/unrelated.txt").write_text("preserve")
        configure("")
        build()
        subprocess.run(["cmake", "--install", str(self.destination), "--prefix", str(prefix)], check=True, stdout=subprocess.DEVNULL)
        for root in (self.destination, prefix):
            self.assertFalse((root / "bin/bf2_client").exists())
            self.assertFalse((root / package.EMBEDDED).exists())
            self.assertEqual((root / "lib/unrelated.txt").read_text(), "preserve")

    def test_invalid_replacement_leaves_previous_client(self):
        embedding.embed(self.engine, self.destination)
        original = (self.destination / "lib/libaidll.so").read_bytes()
        (self.engine / "lib/libaidll.so").write_bytes(b"bad artifact")
        with self.assertRaisesRegex(ValueError, "checksums/inventory differ"):
            embedding.embed(self.engine, self.destination)
        self.assertEqual((self.destination / "lib/libaidll.so").read_bytes(), original)

    def test_unowned_file_symlink_and_overlap(self):
        self.destination.mkdir()
        (self.destination / "lib").symlink_to(self.engine / "lib", target_is_directory=True)
        with self.assertRaisesRegex(ValueError, "Symlink destination"):
            embedding.embed(self.engine, self.destination)
        (self.destination / "lib").unlink()
        (self.destination / "bin").mkdir()
        (self.destination / "bin/bf2_client").write_text("unowned")
        with self.assertRaisesRegex(ValueError, "unowned"):
            embedding.embed(self.engine, self.destination)
        with self.assertRaisesRegex(ValueError, "must not overlap"):
            embedding.embed(self.engine, self.engine / "nested")

    def test_development_override_and_release_rejection(self):
        (self.engine / "share/openbf2/engine-artifact.json").unlink()
        with self.assertRaises(FileNotFoundError):
            embedding.embed(self.engine, self.destination)
        embedding.embed(self.engine, self.destination, development=True)
        with self.assertRaisesRegex(ValueError, "cannot be released"):
            package.reject_development(self.destination)
        record = json.loads((self.destination / package.EMBEDDED).read_text())
        self.assertTrue(record["development"])
        # Development mode still rejects escaping/absolute loader paths.
        subprocess.run(["patchelf", "--set-rpath", "/tmp/host", str(self.engine / "lib/libaidll.so")], check=True)
        with self.assertRaisesRegex(ValueError, "runtime path"):
            embedding.embed(self.engine, self.destination, development=True)


if __name__ == "__main__":
    unittest.main()
