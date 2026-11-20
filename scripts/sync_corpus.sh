#!/bin/sh
# Vendor the canonical LLP 4.0.0 conformance corpus from llp-spec and rebuild
# the C fixture from it.
#
# The corpus is vendored (never read at test time from another checkout) so the
# repository is self-contained; MANIFEST.sha256 travels with it and
# scripts/check_corpus.py verifies every file. FIXTURE.sha256 pins the emitted
# header so CI can detect hand edits without network access.
#
# Usage:
#   sh scripts/sync_corpus.sh [path-to-llp-spec]
#
# Default source: ../llp-spec (sibling checkout).

set -eu

SPEC_DIR="${1:-../llp-spec}"
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DEST="$ROOT/spec/vectors/v4"
FIXTURE="$ROOT/test/v4_vectors_generated.h"

if [ ! -d "$SPEC_DIR/vectors" ] || [ ! -f "$SPEC_DIR/tools/build_vectors.py" ]; then
    echo "error: $SPEC_DIR does not look like a llp-spec checkout" >&2
    echo "usage: $0 [path-to-llp-spec]" >&2
    exit 1
fi

mkdir -p "$DEST"
rm -f "$DEST"/*.json "$DEST"/MANIFEST.sha256
cp "$SPEC_DIR"/vectors/*.json "$SPEC_DIR"/vectors/MANIFEST.sha256 "$DEST"/

( cd "$DEST" && sha256sum -c MANIFEST.sha256 ) >/dev/null

python3 "$SPEC_DIR/tools/build_vectors.py" --emit-c "$FIXTURE"

{
    echo "# sha256 of the C fixture emitted from the vendored corpus."
    echo "# Regenerate with: sh scripts/sync_corpus.sh"
    ( cd "$ROOT" && sha256sum test/v4_vectors_generated.h )
} > "$DEST/FIXTURE.sha256"

echo "synced $(ls "$DEST"/*.json | wc -l) vector files from $SPEC_DIR/vectors"
echo "rebuilt test/v4_vectors_generated.h and spec/vectors/v4/FIXTURE.sha256"
