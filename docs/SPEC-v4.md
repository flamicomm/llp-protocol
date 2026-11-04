# Protocol Specification — LLP v4.0.0 (moved)

The normative specification for wire format 4.0.0 is maintained in the
specification repository:

> **<https://github.com/flamicomm/llp-spec/blob/main/docs/SPEC-v4.md>**

This file used to hold the text. It is kept as a pointer so existing links — in
this repository's README, in `docs/DECISIONS.md`, in `docs/WHY-LLP.md`,
`docs/INTEGRATION.md` and in the `include/llp_v4.h` header comment — keep
working. Do not add normative content here: the specification is versioned
together with the conformance vectors that verify it.

## What lives where

| Document | Repository | Role |
|---|---|---|
| `docs/SPEC-v4.md` | **llp-spec** | normative wire specification |
| `docs/CONFORMANCE.md` | **llp-spec** | vector schema, per-kind rules, checklist |
| `vectors/` + `MANIFEST.sha256` | **llp-spec** | the conformance corpus |
| `reference/llp_v4.py` | **llp-spec** | canonical Python reference (this repo vendors it at `scripts/llp_v4.py`) |
| `docs/DECISIONS.md` | here | ADR log: why each wire decision was made |
| `include/llp_v4.h` | here | the C implementation of the spec |
| `docs/ROBUSTNESS.md`, `docs/INTEGRATION.md`, `docs/WHY-LLP.md` | here | measurements and integration guidance |

The ownership decision is recorded as
[ADR-011](DECISIONS.md#adr-011--llp-spec-is-the-normative-home-of-the-wire-specification)
in [`DECISIONS.md`](DECISIONS.md).

## Conformance

This library conforms to the specification in llp-spec. The C suite does not
parse JSON: `scripts/gen_vectors_v4.py --emit-c` generates
`test/v4_vectors_generated.h` from the vectors, and CI fails if the header is
stale. The same mechanism can consume llp-spec's corpus directly; see
[`CONFORMANCE.md`](CONFORMANCE.md) for the current state.
