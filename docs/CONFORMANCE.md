# Conformance vectors

The vectors in [`spec/vectors/v4/`](../spec/vectors/v4/) are the contract
between implementations. If a C parser, a Java parser and a Python parser all
pass them, they speak the same protocol — regardless of internal structure.

They are **generated and self-checked** by
[`scripts/gen_vectors_v4.py`](../scripts/gen_vectors_v4.py), which uses the
reference implementation in [`scripts/llp_v4.py`](../scripts/llp_v4.py). A
vector can therefore never disagree with the spec it documents.

```bash
python3 scripts/gen_vectors_v4.py           # regenerate + self-check
python3 scripts/gen_vectors_v4.py --check   # verify the checked-in files
```

## File layout

| File | Contents |
|---|---|
| `encode.json` | `(channel, payload)` → exact frame bytes |
| `decode.json` | byte streams → accepted frames (garbage, resync, max payload…) |
| `errors.json` | CRC, length and sync error handling |
| `timing.json` | inter-byte timeouts, boundaries and recovery |

## Schema

```json
{
  "spec": "llp/4.0.0",
  "description": "…",
  "generated_by": "scripts/gen_vectors_v4.py",
  "cases": [
    {
      "id": "enc-text-ok",
      "kind": "encode",
      "channel": 0,
      "payload": "6f6b0a",
      "frame": "aa5503006f6b0a08db"
    },
    {
      "id": "dec-resync-discards-partial-frame",
      "kind": "decode",
      "capacity": 255,
      "timeout_ms": 2000,
      "frame": "aa5503006f6baa5507014731205831300ae427",
      "expect": {
        "frames": [ { "channel": 1, "payload": "4731205831300a" } ],
        "errors": [ "LLP_ERR_SYNC" ],
        "timeouts": 0
      },
      "note": "…"
    },
    {
      "id": "timeout-mid-frame",
      "kind": "decode",
      "capacity": 255,
      "timeout_ms": 2000,
      "feed": [ { "hex": "aa", "dt_ms": 0 }, { "hex": "55", "dt_ms": 0 },
                { "hex": "05", "dt_ms": 2100 } ],
      "expect": { "frames": [], "errors": [ "LLP_ERR_TIMEOUT" ], "timeouts": 1 }
    }
  ]
}
```

A decode case carries **either**:

- `frame` — a hex string fed byte by byte with `dt_ms = 0` (compact form), or
- `feed` — an explicit list of `{hex, dt_ms}` steps, required when the vector
  needs timed gaps (timeout cases) or per-byte control.

All hex strings are lowercase, without separators. `note` is optional prose.

### Encode cases

1. Encoding `(channel, payload)` must produce exactly `frame`.
2. Decoding `frame` must produce exactly one frame with the same
   `(channel, payload)` and no errors.

### Decode cases

`frame` (or every entry of `feed`) is applied byte by byte, in order:

1. The driver keeps a monotonic `now_ms` (any origin) and adds `dt_ms` to it
   **before** feeding that byte.
2. Feed the byte to the parser.
3. Collection rules:
   - `expect.frames` — frames delivered, in order, as `(channel, payload)`.
   - `expect.errors` — error codes, in detection order. The exact strings are
     `LLP_ERR_CRC`, `LLP_ERR_LEN`, `LLP_ERR_TIMEOUT` and `LLP_ERR_SYNC`.
   - `expect.timeouts` — value of the timeout counter after the run; it must
     equal the number of `LLP_ERR_TIMEOUT` events. Implementations that do not
     expose counters can ignore the field.

A conformant implementation must deliver **exactly** the listed frames, in
order: no extra frames, no missing frames. It may report errors at a finer
granularity only if it still reports the listed ones in the listed order.

### Error names

| Name | Value | Meaning |
|---|---:|---|
| `LLP_ERR_CRC` | 1 | CRC mismatch on a complete frame |
| `LLP_ERR_LEN` | 2 | `LEN` > instance capacity |
| `LLP_ERR_TIMEOUT` | 3 | inter-byte gap > `timeout_ms` |
| `LLP_ERR_SYNC` | 4 | bad escape, or a new magic inside a frame |

## Conformance checklist

### Encoder

- [ ] Header is `AA 55` then `LEN` then `CHANNEL`, single bytes each.
- [ ] Every `0xAA` in `LEN`, `CHANNEL`, `PAYLOAD` and `CRC` is emitted as
      `AA 00`; the magic itself is never escaped.
- [ ] CRC16-CCITT-FALSE over `AA 55 LEN CHANNEL PAYLOAD` (unescaped values).
- [ ] CRC transmitted low byte first.
- [ ] `LEN` may be 0; payload larger than 255 is rejected.

### Decoder

- [ ] Garbage while waiting for the magic is ignored, never reported as an error.
- [ ] `AA AA 55` is a valid frame start (the second `AA` is not treated as an
      escape).
- [ ] `AA 55` inside a frame reports `LLP_ERR_SYNC` and starts a new frame.
- [ ] `AA` + anything else reports `LLP_ERR_SYNC` and discards the frame.
- [ ] `AA 00` yields a literal `0xAA` in the current field.
- [ ] `LEN == 0` is valid and skips the payload.
- [ ] `LEN > capacity` reports `LLP_ERR_LEN` **before** storing any payload byte.
- [ ] CRC mismatch reports `LLP_ERR_CRC` and discards the frame.
- [ ] No payload byte is observable by the application before the CRC validates.

### Timing

- [ ] Timeout compares with **strictly greater than** `timeout_ms`.
- [ ] No timeout is reported in `WAIT_MAGIC1` (idle link).
- [ ] A timeout resets the frame but the offending byte is re-examined as a
      possible `0xAA` magic.
- [ ] Timestamp arithmetic is wrap-safe on 16 bits.

### Resource behaviour

- [ ] Work per byte is O(1) and does not depend on payload content.
- [ ] A parser instance never buffers more than `capacity` payload bytes.
- [ ] No dynamic allocation, no recursion, no VLAs.

## Porting to a new language

Implement three primitives and you have a conformant port:

1. `encode(channel, payload) -> bytes`
2. `Parser(capacity, timeout_ms)` with `feed(byte, now_ms) -> events`
3. The four error codes.

Then run the vectors. [`scripts/llp_v4.py`](../scripts/llp_v4.py) is ~200 lines
and can be transliterated directly; it was written to be readable rather than
clever.

## Adding vectors

1. Add the case to the relevant `build_*_vectors()` function.
2. Run `python3 scripts/gen_vectors_v4.py`.
3. The generator refuses to write a case that the reference implementation does
   not reproduce, so a failing self-check means the *expectation* is wrong (or
   the spec changed).
4. Commit the JSON files and the generator change together.
