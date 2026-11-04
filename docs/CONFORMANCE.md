# Conformance vectors (moved)

The vector schema, the per-kind rules, the porting checklist and the corpus now
live in the specification repository:

> **<https://github.com/flamicomm/llp-spec/blob/main/docs/CONFORMANCE.md>**

This file is kept as a pointer so existing links keep working.

## State of this repository

This library still ships its own small vector set for the C suite:

```
spec/vectors/v4/{encode,decode,errors,timing}.json   29 cases
scripts/gen_vectors_v4.py                            generator + --emit-c
test/v4_vectors_generated.h                          generated fixture
```

CI checks that the JSON is reproducible (`--check`) and that the generated header
matches (`diff`). It is the fixture the Unity tests walk, and it has no JSON
dependency at build time.

llp-spec's corpus is the canonical, language-independent one (161 cases covering
the same wire, plus channels, opaque payloads and 16-bit time wrap). The planned
convergence is one-directional and does not change the wire: llp-spec emits the
same header shape with `build_vectors.py --emit-c`, and the C suite switches from
its local 29 cases to that header, deleting the duplicated generator. Until that
change lands, both sets are valid and must agree — they are generated from the
same reference implementation.

## Building the fixture

```bash
python3 scripts/gen_vectors_v4.py --check
python3 scripts/gen_vectors_v4.py --emit-c test/v4_vectors_generated.h
```

From llp-spec (the broader corpus):

```bash
python3 tools/build_vectors.py --emit-c test/v4_vectors_generated.h
```
