#!/usr/bin/env python3
"""Skip clang-tidy on files that passed it before with the same inputs (CI only).

The C++ toolchain workflow mounts this script over clang-tidy-17 in the
ics-cpp image, at /usr/local/bin, which comes before the real one in /usr/bin
on the PATH. So the run-clang-tidy-17 that cpp/policy/check-policy.sh runs
calls it once per file, exactly as it would call clang-tidy-17.

It hashes everything that decides clang-tidy's verdict on the file:
- the file preprocessed with its comments, which holds the text of every
  header it includes;
- the file's compile command;
- every .clang-tidy in the file's folder and above it;
- clang-tidy's arguments and version, and its binary's size and time, which
  a package update changes even when the version string stays the same.

When a file with that hash passed before, the script exits 0 without
running clang-tidy. Otherwise it runs the real clang-tidy and, when it passes,
records the hash in TIDY_CACHE_DIR. Without TIDY_CACHE_DIR, or when the hash
cannot be worked out, it only runs clang-tidy: the cache can skip work, never
a finding. The build folder's path, which differs from run to run, is left
out of the hash.
"""

from __future__ import annotations

import hashlib
import json
import os
import shlex
import subprocess
import sys
from collections.abc import Sequence
from pathlib import Path

JsonObject = dict[str, object]

REAL_CLANG_TIDY: str = os.environ.get("ICS_CLANG_TIDY", "/usr/bin/clang-tidy-17")
# Raise it whenever what goes into the hash changes.
CACHE_VERSION: bytes = b"1"
BUILD_PLACEHOLDER: str = "<build>"
# Compile options that write files, and the ones of those that take a value.
DROPPED: frozenset[str] = frozenset({"-c", "-MD", "-MMD"})
DROPPED_WITH_VALUE: frozenset[str] = frozenset({"-o", "-MF", "-MT", "-MQ"})
SIZE_BYTES: int = 8


class CacheError(Exception):
    """The hash cannot be worked out, so clang-tidy has to run."""


def build_path(args: Sequence[str]) -> str:
    """The build folder run-clang-tidy passes as -p=DIR."""
    found = [arg.removeprefix("-p=") for arg in args if arg.startswith("-p=")]
    if len(found) != 1:
        raise CacheError("expected exactly one -p=DIR argument")
    return found[0]


def entry_path(entry: object) -> str | None:
    """The real path of the file a compile_commands.json entry compiles."""
    if not isinstance(entry, dict):
        return None
    directory, file = entry.get("directory"), entry.get("file")
    if not isinstance(directory, str) or not isinstance(file, str):
        return None
    return os.path.realpath(Path(directory, file))


def compile_entry(build: str, source: str) -> JsonObject:
    """The compile_commands.json entry for source."""
    document: object = json.loads(Path(build, "compile_commands.json").read_text(encoding="utf-8"))
    wanted = os.path.realpath(source)
    for entry in document if isinstance(document, list) else []:
        if isinstance(entry, dict) and entry_path(entry) == wanted:
            return entry
    raise CacheError(f"{source} is not in {build}/compile_commands.json")


def compile_arguments(entry: JsonObject) -> list[str]:
    """The entry's command, from its arguments or its command line."""
    arguments = entry.get("arguments")
    if isinstance(arguments, list) and all(isinstance(argument, str) for argument in arguments):
        return [str(argument) for argument in arguments]
    command = entry.get("command")
    if isinstance(command, str):
        return shlex.split(command)
    raise CacheError("a compile command with neither arguments nor a command line")


def preprocess_arguments(arguments: Sequence[str]) -> list[str]:
    """The compile command changed to preprocess, comments kept, to standard output."""
    kept: list[str] = []
    skip_value = False
    for argument in arguments:
        if skip_value:
            skip_value = False
        elif argument in DROPPED_WITH_VALUE:
            skip_value = True
        elif argument not in DROPPED:
            kept.append(argument)
    return [*kept, "-E", "-C"]


def preprocessed(entry: JsonObject) -> bytes:
    """The file as the compiler sees it, every included header in place."""
    directory = entry.get("directory")
    if not isinstance(directory, str):
        raise CacheError("a compile command without a directory")
    result = subprocess.run(
        preprocess_arguments(compile_arguments(entry)), cwd=directory, capture_output=True, check=False
    )
    if result.returncode != 0:
        raise CacheError("the file does not preprocess")
    return result.stdout


def tidy_configs(source: str) -> list[bytes]:
    """Each .clang-tidy from the file's folder up to the root, with its path."""
    folder = Path(os.path.realpath(source)).parent
    configs: list[bytes] = []
    for candidate in (folder, *folder.parents):
        config = candidate / ".clang-tidy"
        if config.is_file():
            configs.append(str(config).encode() + b"\n" + config.read_bytes())
    return configs


def without_build(data: bytes, build: str) -> bytes:
    """data with the build folder's path, as given and as resolved, replaced."""
    for path in sorted({build, os.path.realpath(build)}, key=len, reverse=True):
        data = data.replace(path.encode(), BUILD_PLACEHOLDER.encode())
    return data


def tool_identity() -> bytes:
    """The real clang-tidy's version, and its binary's size and modification time."""
    version = subprocess.run([REAL_CLANG_TIDY, "--version"], capture_output=True, check=True).stdout
    binary = Path(REAL_CLANG_TIDY).resolve().stat()
    return version + f"{binary.st_size}:{binary.st_mtime_ns}".encode()


def cache_key(args: Sequence[str]) -> str:
    """The hash of everything that decides clang-tidy's verdict on the file."""
    build = build_path(args)
    entry = compile_entry(build, args[-1])
    parts = [
        CACHE_VERSION,
        tool_identity(),
        without_build("\0".join(args).encode(), build),
        without_build("\0".join(compile_arguments(entry)).encode(), build),
        *tidy_configs(args[-1]),
        without_build(preprocessed(entry), build),
    ]
    digest = hashlib.sha256()
    for part in parts:
        digest.update(len(part).to_bytes(SIZE_BYTES, "big"))
        digest.update(part)
    return digest.hexdigest()


def run_clang_tidy(args: Sequence[str]) -> int:
    return subprocess.run([REAL_CLANG_TIDY, *args], check=False).returncode


def main(argv: Sequence[str]) -> int:
    cache_dir = os.environ.get("TIDY_CACHE_DIR", "")
    if not cache_dir or not argv:
        return run_clang_tidy(argv)
    try:
        key = cache_key(argv)
    except (CacheError, OSError, ValueError, subprocess.CalledProcessError):
        return run_clang_tidy(argv)
    passed = Path(cache_dir, key)
    if passed.is_file():
        return 0
    status = run_clang_tidy(argv)
    if status == 0:
        passed.parent.mkdir(parents=True, exist_ok=True)
        passed.touch()
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
