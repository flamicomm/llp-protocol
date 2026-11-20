#!/usr/bin/env python3
"""Verify that the vendored conformance corpus and the C fixture are pinned.

The corpus is authoritative in ``llp-spec``; this repository vendors a copy in
``spec/vectors/v4/`` together with the source ``MANIFEST.sha256``, and commits
the C fixture emitted from it (``test/v4_vectors_generated.h``) pinned by
``spec/vectors/v4/FIXTURE.sha256``.

Checks, in order:

1. every vendored ``spec/vectors/v4/*.json`` matches ``MANIFEST.sha256`` and no
   file is missing or extra — this is the offline pin CI relies on;
2. ``test/v4_vectors_generated.h`` matches ``FIXTURE.sha256``, so a hand edit of
   the generated header fails even without a ``llp-spec`` checkout;
3. when a ``llp-spec`` checkout is available (``../llp-spec`` by default, or
   ``--spec-dir``): its ``vectors/MANIFEST.sha256`` must be byte-identical to
   the vendored one (this is what "the corpus changed without re-vendoring"
   looks like), and ``tools/build_vectors.py --emit-c`` must reproduce the
   committed header byte for byte.

No network access is used. Exit code 1 on any mismatch.

Usage:
    python3 scripts/check_corpus.py [--spec-dir PATH] [--no-regenerate]
"""

from __future__ import annotations

import argparse
import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CORPUS = ROOT / "spec" / "vectors" / "v4"
MANIFEST = CORPUS / "MANIFEST.sha256"
FIXTURE = ROOT / "test" / "v4_vectors_generated.h"
FIXTURE_PIN = CORPUS / "FIXTURE.sha256"


def read_manifest(path: Path) -> dict[str, str]:
    """Parse a ``sha256sum``-style manifest, skipping comments and blanks."""
    entries: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        digest, name = line.split(None, 1)
        entries[name.strip()] = digest.lower()
    return entries


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def check_vendored_corpus() -> list[str]:
    problems: list[str] = []
    expected = read_manifest(MANIFEST)
    on_disk = {path.name for path in CORPUS.glob("*.json")}

    missing = sorted(set(expected) - on_disk)
    extra = sorted(on_disk - set(expected))
    if missing:
        problems.append(f"missing from spec/vectors/v4/: {', '.join(missing)}")
    if extra:
        problems.append(f"not listed in MANIFEST.sha256: {', '.join(extra)}")

    for name in sorted(set(expected) & on_disk):
        actual = digest((CORPUS / name).read_bytes())
        if actual != expected[name]:
            problems.append(f"{name}: sha256 {actual} != manifest {expected[name]}")
    return problems


def check_fixture_pin() -> list[str]:
    pins = read_manifest(FIXTURE_PIN)
    expected = pins.get("test/v4_vectors_generated.h")
    if expected is None:
        return [f"{FIXTURE_PIN}: no entry for test/v4_vectors_generated.h"]
    actual = digest(FIXTURE.read_bytes())
    if actual != expected:
        return [
            "test/v4_vectors_generated.h does not match FIXTURE.sha256 "
            f"({actual} != {expected}); regenerate it with "
            "sh scripts/sync_corpus.sh"
        ]
    return []


def check_against_spec(spec_dir: Path) -> list[str]:
    problems: list[str] = []
    upstream_manifest = spec_dir / "vectors" / "MANIFEST.sha256"
    if not upstream_manifest.is_file():
        return [f"{spec_dir}: vectors/MANIFEST.sha256 not found"]

    if upstream_manifest.read_bytes() != MANIFEST.read_bytes():
        problems.append(
            "the vendored corpus is stale: llp-spec/vectors/MANIFEST.sha256 "
            "differs; run sh scripts/sync_corpus.sh"
        )

    with tempfile.TemporaryDirectory() as tmp:
        emitted = Path(tmp) / "v4_vectors_generated.h"
        result = subprocess.run(
            [sys.executable, str(spec_dir / "tools" / "build_vectors.py"),
             "--emit-c", str(emitted)],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            problems.append(
                "llp-spec/tools/build_vectors.py --emit-c failed: "
                + (result.stderr.strip() or result.stdout.strip())
            )
        elif emitted.read_bytes() != FIXTURE.read_bytes():
            problems.append(
                "test/v4_vectors_generated.h is not what the canonical corpus "
                "emits; regenerate it with sh scripts/sync_corpus.sh"
            )
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--spec-dir",
        default=str(ROOT.parent / "llp-spec"),
        help="path to a llp-spec checkout (default: ../llp-spec)",
    )
    parser.add_argument(
        "--no-regenerate",
        action="store_true",
        help="skip the byte-for-byte regeneration against llp-spec",
    )
    args = parser.parse_args()

    problems = check_vendored_corpus()
    if problems:
        for problem in problems:
            print(f"FAIL  {problem}")
        print("\nthe vendored corpus is not internally consistent "
              "(re-vendor with sh scripts/sync_corpus.sh)")
        return 1
    print(f"ok    vendored corpus matches {MANIFEST.relative_to(ROOT)}")

    problems = check_fixture_pin()
    if problems:
        for problem in problems:
            print(f"FAIL  {problem}")
        return 1
    print("ok    test/v4_vectors_generated.h matches FIXTURE.sha256")

    spec_dir = Path(args.spec_dir)
    if args.no_regenerate:
        print("note  skipped the regeneration against llp-spec (--no-regenerate)")
        return 0
    if not spec_dir.is_dir():
        print(f"note  {spec_dir} not found: only the offline pins were checked")
        return 0

    problems = check_against_spec(spec_dir)
    if problems:
        for problem in problems:
            print(f"FAIL  {problem}")
        return 1
    print(f"ok    corpus and fixture match {spec_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
