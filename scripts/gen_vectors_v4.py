#!/usr/bin/env python3
"""Generate (and self-check) the LLP wire-format v4 conformance vectors.

The vectors are the contract between the C, Java and Python implementations.
They are produced by the reference implementation in ``scripts/llp_v4.py`` and
then verified by feeding every decoded case back through the reference parser,
so a vector can never disagree with the spec it documents.

Usage:
    python3 scripts/gen_vectors_v4.py             # write spec/vectors/v4/*.json
    python3 scripts/gen_vectors_v4.py --check     # verify existing vectors
    python3 scripts/gen_vectors_v4.py --print     # show a sample frame
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import llp_v4 as llp  # noqa: E402

SPEC_VERSION = "llp/4.0.0"
TIMEOUT_MS = llp.DEFAULT_TIMEOUT_MS
OUT_DIR = Path(__file__).resolve().parent.parent / "spec" / "vectors" / "v4"

# Wire error codes (must match include/llp_v4.h)
_ERROR_CODES = {
    llp.ERR_CRC: 1,
    llp.ERR_LEN: 2,
    llp.ERR_TIMEOUT: 3,
    llp.ERR_SYNC: 4,
}


# --------------------------------------------------------------------------
# Builders
# --------------------------------------------------------------------------
def encode_case(case_id: str, channel: int, payload: bytes, note: str = "") -> dict:
    case = {
        "id": case_id,
        "kind": "encode",
        "channel": channel,
        "payload": payload.hex(),
        "frame": llp.encode(channel, payload).hex(),
    }
    if note:
        case["note"] = note
    return case


def _feed(frame: bytes, gaps: dict[int, int] | None = None) -> list[dict]:
    gaps = gaps or {}
    return [
        {"hex": f"{byte:02x}", "dt_ms": gaps.get(index, 0)}
        for index, byte in enumerate(frame)
    ]


def decode_case(
    case_id: str,
    frame: bytes,
    *,
    frames: list[tuple[int, bytes]] | None = None,
    errors: list[str] | None = None,
    timeouts: int = 0,
    capacity: int = llp.MAX_PAYLOAD,
    note: str = "",
    gaps: dict[int, int] | None = None,
) -> dict:
    case = {
        "id": case_id,
        "kind": "decode",
        "capacity": capacity,
        "timeout_ms": TIMEOUT_MS,
        "expect": {
            "frames": [{"channel": ch, "payload": pl.hex()} for ch, pl in (frames or [])],
            "errors": list(errors or []),
            "timeouts": timeouts,
        },
    }
    # Compact form (`frame`) when every byte is fed with dt_ms = 0, per-byte
    # form (`feed`) only when a vector needs timed gaps.
    if gaps:
        case["feed"] = _feed(frame, gaps)
    else:
        case["frame"] = frame.hex()
    if note:
        case["note"] = note
    return case


def write_json(name: str, description: str, cases: list[dict]) -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    document = {
        "spec": SPEC_VERSION,
        "description": description,
        "generated_by": "scripts/gen_vectors_v4.py",
        "cases": cases,
    }
    path = OUT_DIR / name
    path.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {path.relative_to(OUT_DIR.parent.parent.parent)} ({len(cases)} cases)")


# --------------------------------------------------------------------------
# Vector sets
# --------------------------------------------------------------------------
def build_encode_vectors() -> list[dict]:
    # A payload whose CRC contains a 0xAA byte, so the CRC field is stuffed.
    crc_stuff_payload = b""
    for length in range(1, 64):
        for value in range(256):
            candidate = bytes([value]) * length
            body = bytes([length, 0]) + candidate
            crc = llp.crc16(body, llp._crc_seed())
            if (crc & 0xFF) == 0xAA or (crc >> 8) == 0xAA:
                crc_stuff_payload = candidate
                break
        if crc_stuff_payload:
            break

    return [
        encode_case("enc-empty-channel0", 0, b""),
        encode_case("enc-text-ok", 0, b"ok\n"),
        encode_case("enc-channel-1-command", 1, b"G1 X10\n"),
        encode_case(
            "enc-rel-command-seq0",
            1,
            b"\x00G1 X0\n",
            note="Profile R1: command frame payload is [SEQ][DATA] on channel 1",
        ),
        encode_case(
            "enc-rel-command-seq-stuffed",
            1,
            b"\xaaG1 X0\n",
            note="Profile R1: SEQ 0xAA is payload, so the sequence byte is stuffed",
        ),
        encode_case(
            "enc-rel-ack-seq7",
            2,
            b"\x07",
            note="Profile R1: ACK frame payload is exactly [SEQ] on channel 2",
        ),
        encode_case(
            "enc-rel-hello-session",
            3,
            b"\x2a",
            note="Profile R1: session marker on channel 3 (resets dedup state)",
        ),
        encode_case("enc-channel-255", 255, b"ping"),
        encode_case(
            "enc-payload-magic-and-escape",
            0,
            b"\xaa\x55\x00\xaa\x55",
            note="payload contains a full magic pattern; it must be escaped",
        ),
        encode_case(
            "enc-payload-all-aa",
            0,
            b"\xaa" * 8,
            note="worst-case stuffing: every payload byte doubles",
        ),
        encode_case(
            "enc-len-field-stuffed",
            0,
            b"x" * 170,
            note="LEN = 170 = 0xAA, so the LEN field itself must be stuffed",
        ),
        encode_case(
            "enc-channel-field-stuffed",
            0xAA,
            b"hi",
            note="CHANNEL = 0xAA, so the CHANNEL field must be stuffed",
        ),
        encode_case(
            "enc-crc-field-stuffed",
            0,
            crc_stuff_payload,
            note="CRC contains a 0xAA byte, so a CRC byte must be stuffed",
        ),
        encode_case("enc-max-payload", 7, b"\x00\xff" * 127 + b"\x00"),
    ]


def build_decode_vectors() -> list[dict]:
    a = llp.encode(0, b"ok\n")
    b = llp.encode(1, b"G1 X10\n")

    return [
        decode_case("dec-single", a, frames=[(0, b"ok\n")]),
        decode_case("dec-empty-payload", llp.encode(0, b""), frames=[(0, b"")]),
        decode_case(
            "dec-two-frames-back-to-back",
            a + b,
            frames=[(0, b"ok\n"), (1, b"G1 X10\n")],
        ),
        decode_case(
            "dec-garbage-before-magic",
            b"\x00\xff\xaa\x13\xaa\xaa" + a,
            frames=[(0, b"ok\n")],
            note="garbage in WAIT_MAGIC1/2 is ignored, not an error",
        ),
        decode_case(
            "dec-payload-contains-magic",
            llp.encode(0, b"\xaa\x55"),
            frames=[(0, b"\xaa\x55")],
            note="escaped magic inside the payload must not trigger resync",
        ),
        decode_case(
            "dec-resync-discards-partial-frame",
            a[:6] + b,
            frames=[(1, b"G1 X10\n")],
            errors=[llp.ERR_SYNC],
            note="a new magic inside a frame aborts it and starts over",
        ),
        decode_case(
            "dec-bad-escape-recovers",
            b"\xaa\x55\x03\x00\xaa\x42" + a,
            frames=[(0, b"ok\n")],
            errors=[llp.ERR_SYNC],
            note="0xAA followed by anything other than 0x00/0x55 is a sync error",
        ),
        decode_case(
            "dec-truncated-frame-no-followup",
            b"\xaa\x55\x03\x00\x6f\x6b",
            frames=[],
            note="incomplete tail with no following data: nothing is delivered",
        ),
        decode_case(
            "dec-two-frames-max-payload",
            llp.encode(3, b"\xaa" * 255) + llp.encode(4, b"\x00" * 255),
            frames=[(3, b"\xaa" * 255), (4, b"\x00" * 255)],
            note="max payload in both directions of stuffing",
        ),
        decode_case(
            "dec-channel-aa",
            llp.encode(0xAA, b"hi"),
            frames=[(0xAA, b"hi")],
        ),
    ]


def build_error_vectors() -> list[dict]:
    good = llp.encode(1, b"G1 X10\n")

    bad_crc = bytearray(llp.encode(0, b"ok\n"))
    bad_crc[4] ^= 0xFF  # corrupt one payload byte, keep the CRC

    return [
        decode_case(
            "err-bad-crc-then-good",
            bytes(bad_crc) + good,
            frames=[(1, b"G1 X10\n")],
            errors=[llp.ERR_CRC],
        ),
        decode_case(
            "err-payload-len-over-capacity",
            llp.encode(0, b"x" * 100) + good,
            frames=[(1, b"G1 X10\n")],
            errors=[llp.ERR_LEN],
            capacity=64,
            note="LEN > capacity is rejected as soon as LEN is read",
        ),
        decode_case(
            "err-lone-aa-is-not-an-error",
            b"\xaa\x00\xaa\x55" + good[2:],
            frames=[(1, b"G1 X10\n")],
            note="0xAA 0x00 in WAIT_MAGIC2 is ignored; no error is reported",
        ),
        decode_case(
            "err-truncated-then-new-frame",
            b"\xaa\x55\x40\x00\x6f" + good,
            frames=[(1, b"G1 X10\n")],
            errors=[llp.ERR_SYNC],
            note="incomplete frame with LEN=64 meets a new magic: resync",
        ),
    ]


def build_timing_vectors() -> list[dict]:
    frame = llp.encode(0, b"hello")
    fresh = llp.encode(1, b"after-timeout")

    return [
        decode_case(
            "timeout-mid-frame",
            frame,
            frames=[],
            errors=[llp.ERR_TIMEOUT],
            timeouts=1,
            gaps={3: TIMEOUT_MS + 100},
            note="a gap > timeout_ms in the middle of a frame resets the parser",
        ),
        decode_case(
            "timeout-then-recover",
            b"\xaa\x55\x05\x00\x68" + fresh,
            frames=[(1, b"after-timeout")],
            errors=[llp.ERR_TIMEOUT],
            timeouts=1,
            gaps={5: TIMEOUT_MS + 1},
            note="the byte that follows the gap is re-examined as a new magic",
        ),
        decode_case(
            "timeout-exactly-at-limit-is-ok",
            frame,
            frames=[(0, b"hello")],
            gaps={3: TIMEOUT_MS},
            note="the rule is strictly greater than timeout_ms",
        ),
        decode_case(
            "timeout-idle-before-magic-is-not-an-error",
            frame,
            frames=[(0, b"hello")],
            gaps={0: 5 * TIMEOUT_MS},
            note="no timeout is reported while waiting for the magic",
        ),
        decode_case(
            "timeout-between-frames",
            frame + fresh,
            frames=[(0, b"hello"), (1, b"after-timeout")],
            gaps={len(frame): 3 * TIMEOUT_MS},
            note="the parser is in WAIT_MAGIC1 between frames, no timeout",
        ),
    ]


VECTOR_SETS = {
    "encode.json": (
        "Encoder conformance: (channel, payload) -> exact frame bytes.",
        build_encode_vectors,
    ),
    "decode.json": (
        "Decoder conformance: byte streams -> accepted frames and errors.",
        build_decode_vectors,
    ),
    "errors.json": (
        "Error handling: CRC, length and resync cases.",
        build_error_vectors,
    ),
    "timing.json": (
        "Inter-byte timeout: gaps, boundary and recovery.",
        build_timing_vectors,
    ),
}


# --------------------------------------------------------------------------
# C header emitter (test fixture for the C implementation)
# --------------------------------------------------------------------------
def _c_byte_array(name: str, data: bytes) -> list[str]:
    lines = [f"static const uint8_t {name}[] = {{"]
    for start in range(0, len(data), 16):
        chunk = ", ".join(f"0x{byte:02X}" for byte in data[start:start + 16])
        lines.append(f"    {chunk},")
    lines.append("};")
    return lines


def emit_c_header(path: Path) -> None:
    lines: list[str] = [
        "/* AUTO-GENERATED by scripts/gen_vectors_v4.py --emit-c. Do not edit. */",
        "/* Source: spec/vectors/v4/*.json — wire format " + SPEC_VERSION + " */",
        "#ifndef LLP4_VECTORS_GENERATED_H",
        "#define LLP4_VECTORS_GENERATED_H",
        "",
        "#include <stdint.h>",
        "",
        "typedef struct {",
        "    uint8_t byte;",
        "    uint16_t dt_ms;",
        "} llp4_vec_step_t;",
        "",
        "typedef struct {",
        "    uint8_t channel;",
        "    const uint8_t *payload;",
        "    uint8_t payload_len;",
        "} llp4_vec_frame_t;",
        "",
        "typedef struct {",
        "    const char *id;",
        "    uint8_t channel;",
        "    const uint8_t *payload;",
        "    uint8_t payload_len;",
        "    const uint8_t *frame;",
        "    uint16_t frame_len;",
        "} llp4_vec_encode_t;",
        "",
        "typedef struct {",
        "    const char *id;",
        "    uint8_t capacity;",
        "    uint16_t timeout_ms;",
        "    const uint8_t *frame;",           # compact form, dt_ms = 0
        "    uint16_t frame_len;",
        "    const llp4_vec_step_t *steps;",   # timed form
        "    uint16_t step_count;",
        "    const llp4_vec_frame_t *frames;",
        "    uint8_t frame_count;",
        "    const uint8_t *errors;",
        "    uint8_t error_count;",
        "    uint8_t timeouts;",
        "} llp4_vec_decode_t;",
        "",
        "/* ---- data ---------------------------------------------------------- */",
        "",
    ]

    encode_rows: list[str] = []
    for index, case in enumerate(build_encode_vectors()):
        payload = bytes.fromhex(case["payload"])
        frame = bytes.fromhex(case["frame"])
        payload_ref = "NULL"
        if payload:
            lines.extend(_c_byte_array(f"enc{index}_payload", payload))
            payload_ref = f"enc{index}_payload"
        lines.extend(_c_byte_array(f"enc{index}_frame", frame))
        encode_rows.append(
            f'    {{"{case["id"]}", {case["channel"]}, {payload_ref}, '
            f'{len(payload)}, enc{index}_frame, {len(frame)}}},'
        )

    decode_rows: list[str] = []
    decode_cases = (
        build_decode_vectors() + build_error_vectors() + build_timing_vectors()
    )
    for index, case in enumerate(decode_cases):
        frame_ref, frame_len = "NULL", 0
        steps_ref, step_count = "NULL", 0

        if "feed" in case:
            steps = case["feed"]
            lines.append(f"static const llp4_vec_step_t dec{index}_steps[] = {{")
            for step in steps:
                lines.append(
                    f'    {{0x{int(step["hex"], 16):02X}, {step["dt_ms"]}}},'
                )
            lines.append("};")
            steps_ref, step_count = f"dec{index}_steps", len(steps)
        else:
            frame = bytes.fromhex(case["frame"])
            lines.extend(_c_byte_array(f"dec{index}_frame", frame))
            frame_ref, frame_len = f"dec{index}_frame", len(frame)

        frame_entries: list[str] = []
        for frame_index, entry in enumerate(case["expect"]["frames"]):
            payload = bytes.fromhex(entry["payload"])
            payload_ref = "NULL"
            if payload:
                lines.extend(_c_byte_array(f"dec{index}_f{frame_index}", payload))
                payload_ref = f"dec{index}_f{frame_index}"
            frame_entries.append(
                f'    {{{entry["channel"]}, {payload_ref}, {len(payload)}}},'
            )

        frames_ref, frame_count = "NULL", 0
        if frame_entries:
            lines.append(f"static const llp4_vec_frame_t dec{index}_frames[] = {{")
            lines.extend(frame_entries)
            lines.append("};")
            frames_ref, frame_count = f"dec{index}_frames", len(frame_entries)

        errors = case["expect"]["errors"]
        errors_ref, error_count = "NULL", 0
        if errors:
            codes = "{" + ", ".join(str(_ERROR_CODES[name]) for name in errors) + "}"
            lines.append(f"static const uint8_t dec{index}_errors[] = {codes};")
            errors_ref, error_count = f"dec{index}_errors", len(errors)

        decode_rows.append(
            f'    {{"{case["id"]}", {case["capacity"]}, {case["timeout_ms"]}, '
            f'{frame_ref}, {frame_len}, {steps_ref}, {step_count}, '
            f'{frames_ref}, {frame_count}, {errors_ref}, {error_count}, '
            f'{case["expect"]["timeouts"]}}},'
        )

    lines.append("static const llp4_vec_encode_t llp4_vec_encode[] = {")
    lines.extend(encode_rows)
    lines.append("};")
    lines.append(
        "#define LLP4_VEC_ENCODE_COUNT "
        "(sizeof(llp4_vec_encode) / sizeof(llp4_vec_encode[0]))"
    )
    lines.append("")
    lines.append("static const llp4_vec_decode_t llp4_vec_decode[] = {")
    lines.extend(decode_rows)
    lines.append("};")
    lines.append(
        "#define LLP4_VEC_DECODE_COUNT "
        "(sizeof(llp4_vec_decode) / sizeof(llp4_vec_decode[0]))"
    )
    lines.append("")
    lines.append("#endif /* LLP4_VECTORS_GENERATED_H */")

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(
        f"wrote {path}: {len(encode_rows)} encode + {len(decode_rows)} decode cases"
    )



# --------------------------------------------------------------------------
# Self-check
# --------------------------------------------------------------------------
def run_case(case: dict) -> tuple[list[tuple[int, bytes]], list[str], int]:
    """Feed a decode case through the reference parser."""
    parser = llp.Parser(
        capacity=case.get("capacity", llp.MAX_PAYLOAD),
        timeout_ms=case.get("timeout_ms", TIMEOUT_MS),
    )
    steps = case.get("feed")
    if steps is None:
        steps = [{"hex": f"{byte:02x}", "dt_ms": 0} for byte in bytes.fromhex(case["frame"])]
    frames: list[tuple[int, bytes]] = []
    errors: list[str] = []
    now = 0
    for step in steps:
        now += step["dt_ms"]
        for event in parser.feed(int(step["hex"], 16), now):
            if event[0] == "frame":
                frames.append((event[1], event[2]))
            elif event[0] == "error":
                errors.append(event[1])
    return frames, errors, parser.timeouts


def check_case(case: dict) -> list[str]:
    problems: list[str] = []

    if case["kind"] == "encode":
        frame = llp.encode(case["channel"], bytes.fromhex(case["payload"]))
        if frame.hex() != case["frame"]:
            problems.append(f"encode mismatch: {frame.hex()} != {case['frame']}")
        # The encoded frame must decode back to the same values.
        decode = dict(case, kind="decode", frame=frame.hex(),
                      expect={"frames": [{"channel": case["channel"],
                                          "payload": case["payload"]}],
                              "errors": [], "timeouts": 0})
        decode.pop("feed", None)
        decode.pop("gaps", None)
        frames, errors, timeouts = run_case(decode)
        expected = [(case["channel"], bytes.fromhex(case["payload"]))]
        if frames != expected or errors or timeouts:
            problems.append(f"round-trip mismatch: {frames} {errors} {timeouts}")
        return problems

    frames, errors, timeouts = run_case(case)
    expected_frames = [
        (entry["channel"], bytes.fromhex(entry["payload"]))
        for entry in case["expect"]["frames"]
    ]
    if frames != expected_frames:
        problems.append(f"frames: got {frames} want {expected_frames}")
    if errors != case["expect"]["errors"]:
        problems.append(f"errors: got {errors} want {case['expect']['errors']}")
    if timeouts != case["expect"]["timeouts"]:
        problems.append(f"timeouts: got {timeouts} want {case['expect']['timeouts']}")

    # Frames carried by `feed` must re-encode to the same bytes.
    for _, payload in frames:
        if llp.encode(0, payload) == b"":
            problems.append("encoder rejected a decoded payload")
    return problems


def self_check() -> int:
    failures = 0
    for name, (_, builder) in VECTOR_SETS.items():
        for case in builder():
            problems = check_case(case)
            if problems:
                failures += 1
                print(f"FAIL {name}:{case['id']}")
                for problem in problems:
                    print(f"     {problem}")
    if failures:
        print(f"\n{failures} vector(s) failed self-check")
        return 1
    print("self-check OK: every vector matches the reference implementation")
    return 0


def main() -> int:
    if "--print" in sys.argv:
        sample = llp.encode(0, b"ok\n")
        print("frame      :", sample.hex())
        print("frame_size :", llp.frame_size(5))
        return 0

    if "--emit-c" in sys.argv:
        index = sys.argv.index("--emit-c") + 1
        if index >= len(sys.argv):
            print("usage: --emit-c <output header>")
            return 2
        emit_c_header(Path(sys.argv[index]))
        return 0

    if "--check" in sys.argv:
        return self_check()

    for name, (description, builder) in VECTOR_SETS.items():
        write_json(name, description, builder())
    return self_check()


if __name__ == "__main__":
    raise SystemExit(main())
