#!/usr/bin/env python3
"""Stamp the release version into linglong.yaml before a linglong build.

linglong package versions are four dotted numbers (X.Y.Z.W) while the release
tag stays the single version source for every platform. The Linux workflow
derives the linglong spelling from the tag and then calls this script, which
rewrites only the 'version:' entry of the 'package:' block so the comments,
the build script and every other field stay byte-identical.

    v1.0.12        -> 1.0.12.0
    v1.0.12-linux  -> 1.0.12.0
"""

import argparse
import io
import re
import sys
from pathlib import Path

PACKAGE_VERSION = re.compile(r"^([ \t]+)version:([ \t]*)(\S+)[ \t]*$")
LINGLONG_VERSION = re.compile(r"^[0-9]+(?:\.[0-9]+){3}$")


def update_manifest(path: Path, version: str) -> bool:
    """Rewrite the package version in *path*; return True when it changed."""
    with io.open(path, "r", encoding="utf-8", newline="") as handle:
        text = handle.read()
    newline = "\r\n" if "\r\n" in text else "\n"
    lines = text.split(newline)

    in_package = False
    for index, line in enumerate(lines):
        stripped = line.strip()
        if stripped == "package:":
            in_package = True
            continue
        if not in_package:
            continue
        if line and not line[0].isspace():
            break  # left the package block without finding a version
        match = PACKAGE_VERSION.match(line)
        if match:
            current = match.group(3)
            if current == version:
                return False
            lines[index] = "%sversion:%s%s" % (match.group(1), match.group(2), version)
            with io.open(path, "w", encoding="utf-8", newline="") as handle:
                handle.write(newline.join(lines))
            return True
    raise SystemExit("error: no package version found in %s" % path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True,
                        help="linglong package version, four dotted numbers")
    parser.add_argument("--manifest", default="linglong.yaml",
                        help="linglong manifest to rewrite in place")
    parser.add_argument("--dry-run", action="store_true",
                        help="Validate the version without writing")
    args = parser.parse_args()

    version = args.version.strip()
    if not LINGLONG_VERSION.match(version):
        print("error: version must have four numeric components: %s" % version,
              file=sys.stderr)
        return 1

    path = Path(args.manifest)
    if args.dry_run:
        print("resolved linglong version: %s (no write)" % version)
        return 0
    if not path.is_file():
        print("error: manifest not found: %s" % path, file=sys.stderr)
        return 1

    changed = update_manifest(path, version)
    print("linglong.yaml version -> %s (%s)" %
          (version, "updated" if changed else "already current"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
