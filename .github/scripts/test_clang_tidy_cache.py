"""Tests for clang_tidy_cache.py, with a stand-in clang-tidy and compiler."""

from __future__ import annotations

import json
import os
import shutil
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import clang_tidy_cache as ctc

# Prints its version, or records each call and fails a file that holds "BAD".
FAKE_TIDY = """#!/usr/bin/env python3
import os, sys
if sys.argv[1:] == ["--version"]:
    print("fake clang-tidy 17")
    sys.exit(0)
with open(os.environ["FAKE_TIDY_LOG"], "a", encoding="utf-8") as log:
    log.write(sys.argv[-1] + "\\n")
text = open(sys.argv[-1], encoding="utf-8").read() if os.path.isfile(sys.argv[-1]) else ""
sys.exit(1 if "BAD" in text else 0)
"""

# Preprocesses by printing the source and its flags; fails a file that holds "NOPP".
FAKE_CC = """#!/usr/bin/env python3
import sys
source = [arg for arg in sys.argv[1:] if arg.endswith(".cpp")][0]
text = open(source, encoding="utf-8").read()
if "NOPP" in text:
    sys.exit(1)
print(" ".join(arg for arg in sys.argv[1:] if arg.startswith("-D")))
print(text)
"""

EXIT_PASSED = 0
EXIT_FAILED = 1


def write_tool(path: Path, text: str) -> str:
    path.write_text(text, encoding="utf-8")
    path.chmod(0o755)
    return str(path)


class Tree:
    """A source folder, a build folder with compile_commands.json, and the fakes."""

    def __init__(self, root: Path, build_name: str = "build") -> None:
        self.root = root
        self.source = root / "src" / "a.cpp"
        self.build = root / build_name
        self.log = root / "tidy.log"
        self.cache = root / "cache"
        self.source.parent.mkdir(parents=True, exist_ok=True)
        self.build.mkdir(exist_ok=True)
        if not self.source.exists():
            self.source.write_text("int a() { return 1; }\n", encoding="utf-8")
            (root / "src" / ".clang-tidy").write_text("Checks: '*'\n", encoding="utf-8")
        self.tidy = write_tool(root / "fake-tidy", FAKE_TIDY)
        compiler = write_tool(root / "fake-cc", FAKE_CC)
        arguments = [compiler, "-DX=1", f"-ffile-prefix-map={self.build}=build", "-o", "a.o", "-c", str(self.source)]
        entry = {"directory": str(self.build), "file": str(self.source), "arguments": arguments}
        (self.build / "compile_commands.json").write_text(json.dumps([entry]), encoding="utf-8")

    def args(self) -> list[str]:
        return [f"-p={self.build}", "-quiet", str(self.source)]

    def calls(self) -> int:
        return len(self.log.read_text(encoding="utf-8").splitlines()) if self.log.exists() else 0

    def run(self, args: list[str] | None = None, cache: bool = True) -> int:
        environment = {"FAKE_TIDY_LOG": str(self.log)}
        if cache:
            environment["TIDY_CACHE_DIR"] = str(self.cache)
        with patch.dict(os.environ, environment), patch.object(ctc, "REAL_CLANG_TIDY", self.tidy):
            return ctc.main(self.args() if args is None else args)


class ArgumentTests(unittest.TestCase):
    def test_reads_the_build_folder(self) -> None:
        self.assertEqual(ctc.build_path(["-quiet", "-p=/b", "f.cpp"]), "/b")
        for args in (["f.cpp"], ["-p=/a", "-p=/b", "f.cpp"]):
            with self.subTest(args=args), self.assertRaises(ctc.CacheError):
                ctc.build_path(args)

    def test_turns_a_compile_command_into_preprocessing(self) -> None:
        command = ["cc", "-DX", "-MD", "-MT", "a.o", "-MF", "a.d", "-o", "a.o", "-c", "a.cpp"]
        self.assertEqual(ctc.preprocess_arguments(command), ["cc", "-DX", "a.cpp", "-E", "-C"])

    def test_reads_arguments_or_a_command_line(self) -> None:
        self.assertEqual(ctc.compile_arguments({"arguments": ["cc", "a.cpp"]}), ["cc", "a.cpp"])
        self.assertEqual(ctc.compile_arguments({"command": "cc -DX='a b' a.cpp"}), ["cc", "-DX=a b", "a.cpp"])
        with self.assertRaises(ctc.CacheError):
            ctc.compile_arguments({"arguments": ["cc", 1]})

    def test_skips_entries_it_cannot_read(self) -> None:
        self.assertIsNone(ctc.entry_path("not an entry"))
        self.assertIsNone(ctc.entry_path({"directory": "/d"}))
        with self.assertRaises(ctc.CacheError):
            ctc.preprocessed({"arguments": ["cc"]})


class CacheTests(unittest.TestCase):
    def setUp(self) -> None:
        self.folder = tempfile.TemporaryDirectory()
        self.tree = Tree(Path(self.folder.name))

    def tearDown(self) -> None:
        self.folder.cleanup()

    def test_runs_clang_tidy_once_for_unchanged_inputs(self) -> None:
        self.assertEqual(self.tree.run(), EXIT_PASSED)
        self.assertEqual(self.tree.run(), EXIT_PASSED)
        self.assertEqual(self.tree.calls(), 1)

    def test_runs_it_again_when_the_file_or_a_config_changes(self) -> None:
        self.tree.run()
        self.tree.source.write_text("int a() { return 2; }\n", encoding="utf-8")
        self.tree.run()
        (self.tree.root / ".clang-tidy").write_text("Checks: '-*'\n", encoding="utf-8")
        self.tree.run()
        self.assertEqual(self.tree.calls(), 3)

    def test_runs_it_again_when_the_arguments_or_flags_change(self) -> None:
        self.tree.run()
        self.tree.run([*self.tree.args()[:-1], "-header-filter=.*", str(self.tree.source)])
        commands = json.loads((self.tree.build / "compile_commands.json").read_text(encoding="utf-8"))
        commands[0]["arguments"].insert(1, "-DY=2")
        (self.tree.build / "compile_commands.json").write_text(json.dumps(commands), encoding="utf-8")
        self.tree.run()
        self.assertEqual(self.tree.calls(), 3)

    def test_ignores_where_the_build_folder_is(self) -> None:
        self.tree.run()
        other = Tree(self.tree.root, "build-elsewhere")
        self.assertEqual(other.run(), EXIT_PASSED)
        self.assertEqual(self.tree.calls(), 1)

    def test_never_records_a_failure(self) -> None:
        self.tree.source.write_text("int a() { return 1; } // BAD\n", encoding="utf-8")
        self.assertEqual(self.tree.run(), EXIT_FAILED)
        self.assertEqual(self.tree.run(), EXIT_FAILED)
        self.assertEqual(self.tree.calls(), 2)
        self.assertFalse(self.tree.cache.exists())

    def test_runs_clang_tidy_whenever_it_cannot_hash(self) -> None:
        self.tree.run([f"-p={self.tree.build}", "-list-checks", "-"])
        self.tree.run(cache=False)
        self.tree.run(cache=False)
        self.tree.source.write_text("int a(); // NOPP\n", encoding="utf-8")
        self.tree.run()
        self.tree.run()
        shutil.rmtree(self.tree.build)
        self.tree.run()
        self.assertEqual(self.tree.calls(), 6)


if __name__ == "__main__":
    unittest.main()
