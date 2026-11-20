# Conformance — wire 4.0.0 (and Profile R1)

The conformance corpus is defined, generated and versioned in **`llp-spec`**:

- `llp-spec/docs/CONFORMANCE.md` — vector schema, per-kind rules, porting checklist;
- `llp-spec/vectors/` — the corpus itself, one JSON file per category;
- `llp-spec/tools/build_vectors.py` — the only generator (`--emit-c` emits the C fixture).

This repository **implements** that norm; it does not define it. To keep the C
suite self-contained and offline, the corpus is **vendored**: the JSON files and
their manifest are committed here and pinned by hash. Nothing is read from
another checkout at build time and the tests never parse JSON.

## What is vendored, and how it is pinned

| Path | Role |
|---|---|
| `spec/vectors/v4/*.json` | byte copy of `llp-spec/vectors/*.json` (170 cases) |
| `spec/vectors/v4/MANIFEST.sha256` | `sha256sum` of every vendored file, copied verbatim from `llp-spec` |
| `test/v4_vectors_generated.h` | C fixture emitted from the vendored corpus by `build_vectors.py --emit-c` |
| `spec/vectors/v4/FIXTURE.sha256` | `sha256` of the committed fixture, so a hand edit fails without a `llp-spec` checkout |

`scripts/check_corpus.py` checks the three levels in order:

1. **Vendored corpus** — every `spec/vectors/v4/*.json` matches `MANIFEST.sha256`
   and no file is missing or extra. This is the offline pin CI relies on.
2. **Fixture pin** — `test/v4_vectors_generated.h` matches `FIXTURE.sha256`.
3. **Against the norm** (only when a `llp-spec` checkout exists, `../llp-spec` by
   default): its `vectors/MANIFEST.sha256` is byte-identical to the vendored copy
   — this is what "the corpus changed without re-vendoring" looks like — and
   `tools/build_vectors.py --emit-c` reproduces the committed header byte for
   byte, which is the determinism check.

Level 3 degrades to a note (`llp-spec not found: only the offline pins were
checked`) so the suite still runs in a standalone clone; CI checks out
`llp-spec` and therefore always runs all three.

## Regenerating

```bash
sh scripts/sync_corpus.sh                     # from ../llp-spec (default)
sh scripts/sync_corpus.sh /path/to/llp-spec   # from an explicit checkout
```

`sync_corpus.sh` copies the JSON plus the upstream manifest, verifies
`sha256sum -c MANIFEST.sha256`, re-emits `test/v4_vectors_generated.h` and
rewrites `FIXTURE.sha256`. Regeneration is deterministic: same corpus in, same
bytes out, which is why the fixture can be committed and diffed.

## Case counts

Never quote these from memory; they are derivable:

```bash
python3 - <<'PY'
import json, pathlib, re
corpus = pathlib.Path('spec/vectors/v4')
cases = [c for p in sorted(corpus.glob('*.json')) for c in json.loads(p.read_text())['cases']]
r1 = [c for c in cases if c['id'].startswith('rel-')]
rows = len(re.findall(r'^    \{"', pathlib.Path('test/v4_vectors_generated.h').read_text(), re.M))
print(f"corpus {len(cases)} | R1 wire {len(r1)} | emitted {len(cases) - len(r1)} | fixture rows {rows}")
PY
```

Current output: `corpus 170 | R1 wire 9 | emitted 161 | fixture rows 161`.

| File | Cases | Kind |
|---|---:|---|
| `encode.json` | 39 | `encode` |
| `decode.json` | 45 | `decode` |
| `errors.json` | 15 | `decode` |
| `timing.json` | 12 | `decode` |
| `channels.json` | 14 | 9 `encode` + 5 `decode` |
| `payload.json` | 24 | 12 `encode` + 12 `decode` |
| `streams.json` | 12 | `stream` |
| `reliability.json` | 9 | Profile R1: 6 `encode` + 3 `decode` |

The fixture flattens everything into two tables whose rows are the assertions the
C suite walks: `llp4_vec_encode[]` (60 rows: `encode.json` + the encode half of
`channels.json` and `payload.json`) and `llp4_vec_decode[]` (101 rows: `decode`,
`errors`, `timing`, `streams` and the decode halves). `stream` cases carry
per-byte steps like the rest, so chunked delivery is exercised byte by byte.

## Profile R1: what `--emit-c` does not emit (L-I1b)

`build_vectors.py --emit-c` emits `encode.json`, `decode.json`, `errors.json`,
`timing.json`, `channels.json`, `payload.json` and `streams.json`. It does
**not** emit `reliability.json`: the nine canonical R1 wire cases (`rel-enc-*`,
`rel-dec-*`) are absent from the fixture by construction, not by accident.

This repository does not hand-write them, and does not patch the fixture:

- **Behaviour** is covered by [`../test/test_v4_rel.c`](../test/test_v4_rel.c):
  the nine conformance traces T1–T9 of `llp-spec/docs/RELIABILITY.md` §11 plus the
  unit behaviour of both state machines.
- **Wire layout** of the profile payloads is covered indirectly: those payloads
  are ordinary channel 1/2 frames, and `test_v4_rel.c` round-trips them through
  `llp4_encode`/`llp4_parser_feed`.
- **The nine canonical cases remain an open gap** in `llp-spec`: teaching
  `--emit-c` to emit `reliability.json` is **L-I1b**, a separate task in that
  repository. When it lands, `sh scripts/sync_corpus.sh` picks the cases up with no
  change here; the only local edits are the two pins in `test_v4.c` (60 → 66
  encode, 101 → 104 decode) and the counts in this table.

Base conformance does not require R1 (`llp-spec/docs/CONFORMANCE.md`, "Profile R1
(optional)"), so an implementation that stops at the 161 emitted cases is
conformant with the wire. This library claims R1 as well, hence the tests above.

## When a case fails

1. **Do not edit the vector.** Vectors are the contract and are generated in
   `llp-spec`; a hand edit breaks the manifest and `check_corpus.py` will catch it.
2. Decide where the defect is: if the normative text is unambiguous and the C is
   wrong, fix the C (and `scripts/llp_v4.py`, the vendored reference) in the same
   change. If the text is ambiguous or the vector looks wrong, open it in
   `llp-spec` first — the wire changes there, then here.
3. If the corpus changed upstream, re-vendor with `sh scripts/sync_corpus.sh` and
   commit the corpus, the fixture and `FIXTURE.sha256` together, in the same
   change, so no intermediate commit has a stale fixture.
4. Record any deviation and its evidence in `docs/DECISIONS.md`; "sin log, no
   pasó".

## Checklist

The per-field encoder/decoder checklist (stuffing, CRC coverage, `LEN` before
payload, timeout semantics, resource behaviour) is normative in
`llp-spec/docs/CONFORMANCE.md` and is not duplicated here. For the local view of
what is exercised, run the suite and `scripts/check_corpus.py`; both are gates in
`.github/workflows/platformio.yml`.
