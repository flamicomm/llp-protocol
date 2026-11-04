# LLP v4 examples

Reference **transport adapters**. The library (see
[`docs/INTEGRATION.md`](../../docs/INTEGRATION.md)) only frames bytes; these
examples show how to wire it to real links.

| Example | Link | Shows |
|---|---|---|
| [`avr_baremetal/`](avr_baremetal/main.c) | UART on ATmega328P | interrupt RX/TX rings, streaming sink, bounded TX reservation, 1 ms tick, text lines on channel 0 |
| [`host_posix/`](host_posix/host_posix.c) | file descriptor / TCP | the whole adapter in ~40 lines, `--selftest` with noise injection, `--stdio` and `--tcp` modes |

Both are built in CI (`scripts/check_examples.sh`).

## AVR bare-metal

```bash
avr-gcc -mmcu=atmega328p -Os -std=c99 -Wall -Wextra -I include \
        examples/v4/avr_baremetal/main.c -o /tmp/avr_baremetal.elf
```

Flash it on an Arduino Uno (bare metal, no Arduino core: the UART interrupts
belong to the sketch). Behaviour:

- channel 0 text: sending `PING\n` replies `PONG\n`;
- channel 2: a 2-byte counter every second, mirrored back.

RAM: ≈ 220 bytes of buffers + 17 bytes of parser state.

## Host (POSIX)

```bash
cc -Os -std=c99 -Wall -Wextra -I include \
   examples/v4/host_posix/host_posix.c -o /tmp/host_posix

/tmp/host_posix --selftest        # 300 roundtrips + noise test
```

Live modes (use `scripts/llp_v4.py` to encode frames):

```bash
# over a TCP port
/tmp/host_posix --tcp 7777 &
python3 - <<'PY'
import socket, sys
sys.path.insert(0, "scripts")
import llp_v4
s = socket.create_connection(("127.0.0.1", 7777))
s.sendall(llp_v4.encode(0, b"PING\n"))
print(s.recv(64))      # -> PONG frame
PY

# over stdio
python3 -c 'import sys; sys.path.insert(0,"scripts"); import llp_v4; sys.stdout.buffer.write(llp_v4.encode(0,b"PING\n"))' \
  | /tmp/host_posix --stdio | xxd
```

## ESP8266 / ESP32

Those boards are a *combination* of the two adapters above: a UART interrupt for
the MCU side and a socket for the WiFi side. The production reference is the
[`esp8266-llp-bridge`](https://github.com/enzogomezs/esp8266-llp-bridge) repo,
which is written exactly this way; `host_posix --tcp` shows the socket half and
`avr_baremetal` the UART half.

## RS-485

Half-duplex only needs the direction pin around the frame; see
[`docs/INTEGRATION.md`](../../docs/INTEGRATION.md#rs-485-half-duplex).
