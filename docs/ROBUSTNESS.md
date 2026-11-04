# Robustness under noise

LLP makes two promises. This document measures both.

1. **No corrupted frame reaches the application.** A wrong payload is worse than
   a lost one: a CNC that receives `X-90.5` instead of `X-10.5` cuts the wrong
   path. The CRC guarantee is therefore the headline property.
2. **Recovery is bounded.** After any damage the parser finds the next frame
   within a few dozen bytes, so a noisy moment costs a frame, not the link.

## Method

All measurements are **deterministic**: integer-only arithmetic and a fixed
seed, so the table below is reproducible on any machine and checked in CI.

**Stream.** 400 frames, payload 8..64 bytes, channels 0..3, payload starts with
a 16-bit sequence number so a delivered frame can be matched exactly against
what was sent.

**Noise model** (applied to the concatenated stream):

| Scenario | Model |
|---|---|
| `clean` | nothing |
| `bit flips 1e-5` | one random bit flipped every 100 000 bytes |
| `bit flips 1e-4` | one random bit flipped every 10 000 bytes |
| `bit flips 1e-3` | one random bit flipped every 1 000 bytes |
| `drops 1e-4` | one byte removed every 10 000 bytes |
| `inserts 1e-4` | one random byte inserted every 10 000 bytes |
| `burst 8B / 4KB` | 8 consecutive bytes replaced every 4 KB |

**Metrics.**

- *Frames intact*: delivered frames that are byte-identical to a sent frame
  (unique count).
- *Corrupt delivered*: delivered frames that do **not** match any sent frame.
  This must be **0** — it is the property being tested.
- *Errors*: parser error events (CRC, length, sync).
- *Bytes to resync*: distance from an error to the next delivered frame.

## Results

<!-- LLP4-NOISE-TABLE:BEGIN -->
| Scenario | Frames intact | Corrupt delivered | Errors | Avg bytes to resync | Max |
|---|---:|---:|---:|---:|---:|
| clean | 400/400 (100.0%) | 0 | 0 | 0 | 0 |
| bit flips 1e-5 | 400/400 (100.0%) | 0 | 0 | 0 | 0 |
| bit flips 1e-4 | 398/400 (99.5%) | 0 | 2 | 56 | 65 |
| bit flips 1e-3 | 389/400 (97.3%) | 0 | 10 | 50 | 70 |
| drops 1e-4 | 398/400 (99.5%) | 0 | 2 | 54 | 63 |
| inserts 1e-4 | 398/400 (99.5%) | 0 | 2 | 57 | 66 |
| burst 8B / 4KB | 395/400 (98.8%) | 0 | 4 | 46 | 66 |

False accepts: **0** frames delivered from 16 MB of pure random bytes (expected < 1).
<!-- LLP4-NOISE-TABLE:END -->

## False accepts

Feeding **16 MB of pure random bytes** (no frames at all) delivers **0 frames**.
For a false frame you need the `AA 55` magic (2⁻¹⁶ per position) *and* a matching
CRC16 (2⁻¹⁶), so the expected number in 16 MB is ~0.004. The measured upper bound
is therefore around **1e-8 per byte** — 16 MB of pure noise cannot create a
command.

## Property fuzzer

`tools/fuzz_v4.c` runs a deterministic campaign over four input distributions
(uniform bytes, ASCII text lines, magic/escape-heavy, valid frames mixed with
garbage) and checks five invariants on **every** input:

1. the parser never writes outside the payload buffer (guard bytes);
2. a delivered frame never exceeds the capacity and points at the caller buffer;
3. a delivered frame round-trips exactly (re-encode → re-parse gives the same
   channel and payload);
4. the returned error code matches `p->error` and is one of the four;
5. after any input, a repeated good frame is accepted (resynchronisation).

```
600 000 inputs across 3 seeds, AddressSanitizer + UBSan: no failures
```

A stray `0xAA` at a frame boundary can consume the next byte as an escape
continuation, which costs exactly one frame; the following frame resynchronises.
That is why invariant 5 retries once.

## What the numbers mean in the field

- At 1e-4 bit-flip rate (one flipped bit every 10 KB — already a broken link),
  **99.5 % of frames survive**, no frame is ever corrupted, and each loss costs
  ~56 bytes of resync.
- Byte drops and insertions behave the same: one frame lost, immediate recovery.
- A burst of 8 corrupted bytes costs ~1 frame per burst.
- The application never sees a wrong payload, only missing messages.

## Hardware loopback (manual)

The automated tests run on the host. To reproduce on real hardware:

1. Flash `examples/v4/avr_baremetal` on an Arduino and the bridge firmware on an
   ESP8266 (UART at 115200).
2. On the host, run `host_posix --selftest` to validate the framing first.
3. Send a long stream over TCP and count `frames_ok`/`frames_error` on the
   firmware (they are exposed in the telemetry channel).
4. With the spindle running (the actual noise source), compare the error rate
   with the machine idle. A CRC error rate that grows with the spindle indicates
   wiring/EMI, not protocol problems — the link is still delivering every
   uncorrupted frame.

## Reproduce

```bash
sh scripts/check_robustness.sh          # fuzzer + noise table + doc check
```
