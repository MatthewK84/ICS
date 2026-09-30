#!/usr/bin/env bash
# Install the geoid grids pinned in tools.txt beside this script (ICS-014).
#
# Usage: install-geoids.sh TARGET_DIR [CA_BUNDLE]
#
# Each grid is a .tar.bz2 archive from GeographicLib's distribution. The
# download must match its pinned sha256; the files under geoids/ in it are
# written to TARGET_DIR, where GeographicLib looks for them
# (/usr/share/GeographicLib/geoids in the images). Python's standard library
# does the download and unpacking, because the images have neither curl nor
# bzip2. CA_BUNDLE defaults to the system bundle.
set -euo pipefail

readonly TARGET="${1:?usage: install-geoids.sh TARGET_DIR [CA_BUNDLE]}"
readonly CA_BUNDLE="${2:-/etc/ssl/certs/ca-certificates.crt}"
HERE="$(cd "$(dirname "$0")" && pwd)"
readonly HERE

python3 - "${HERE}/tools.txt" "${TARGET}" "${CA_BUNDLE}" <<'EOF'
import hashlib
import shutil
import ssl
import sys
import tarfile
import tempfile
import urllib.request
from pathlib import Path

MAX_GRIDS = 4
MAX_FILES = 16
pins, target, ca_bundle = sys.argv[1:]
entries = [line.split() for line in Path(pins).read_text().splitlines() if line.strip() and not line.startswith("#")]
if not entries or len(entries) > MAX_GRIDS or any(len(entry) != 4 for entry in entries):
    sys.exit(f"error: {pins} must list 1 to {MAX_GRIDS} grids as: name version sha256 url")
context = ssl.create_default_context(cafile=ca_bundle)
Path(target).mkdir(parents=True, exist_ok=True)
for name, version, sha256, url in entries:
    with tempfile.TemporaryDirectory() as work:
        archive = Path(work) / f"{name}.tar.bz2"
        request = urllib.request.Request(url, headers={"User-Agent": "ics-toolchain"})
        with urllib.request.urlopen(request, context=context, timeout=300) as response, archive.open("wb") as out:
            shutil.copyfileobj(response, out)
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        if digest != sha256:
            sys.exit(f"error: {name}: the download's sha256 {digest} does not match the pin {sha256}")
        with tarfile.open(archive, "r:bz2") as bundle:
            members = [member for member in bundle.getmembers() if member.isfile() and member.name.startswith("geoids/")]
            if not members or len(members) > MAX_FILES:
                sys.exit(f"error: {name}: expected 1 to {MAX_FILES} files under geoids/, found {len(members)}")
            for member in members:
                source = bundle.extractfile(member)
                if source is None:
                    sys.exit(f"error: {name}: cannot read {member.name}")
                with source, (Path(target) / Path(member.name).name).open("wb") as destination:
                    shutil.copyfileobj(source, destination)
    print(f"installed {name} {version} in {target}")
EOF
