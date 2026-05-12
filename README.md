# LLP Protocol — Lightweight Link Protocol

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![PlatformIO CI](https://github.com/EnzoLeonel/llp-protocol/actions/workflows/platformio.yml/badge.svg)](https://github.com/EnzoLeonel/llp-protocol/actions/workflows/platformio.yml)
[![Arduino Compatible](https://img.shields.io/badge/Arduino-Compatible-brightgreen)](https://www.arduino.cc/)
[![C Standard](https://img.shields.io/badge/C-99-blue)](https://en.wikipedia.org/wiki/C99)

Protocolo de comunicación **liviano, robusto y extensible** para microcontroladores.
Ideal para UART, RF (433MHz, LoRa), RS485, CAN y otros medios con ruido.

**Características:**
- Ultra-liviano: ~500B de código, sin dependencias
- Robusto: CRC16-CCITT, byte stuffing, sincronización anti-ruido, timeouts
- Extensible: layer chain con metadata (passthrough y transform layers)
- Agnóstico del medio: UART, RF, RS485, Bluetooth, CAN, etc.
- Header-only library: incluir `llp_protocol.h` y listo

---

## Wire Format

```
[0xAA][0x55][LEN_L][LEN_H][PAYLOAD...][CRC_L][CRC_H]
```

Byte stuffing: `0xAA` en LEN/PAYLOAD/CRC se escribe como `0xAA 0x00`.
Layer chain en PAYLOAD: `[LAYER_ID][META_LEN][METADATA...]...[0x00][RAW DATA]`

---

## Estructura del proyecto

```
llp-protocol/
├── include/llp_protocol.h     # API pública (single-header)
├── src/llp_protocol.c         # Verifica compilación standalone
├── test/                      # Test suites con Unity (35 tests)
│   ├── test_parser_init.c
│   ├── test_frame_builder.c
│   ├── test_parser_process.c
│   └── test_crc.c
├── examples/                  # Sketches Arduino
├── .github/workflows/         # CI con GitHub Actions
├── platformio.ini             # Configuración multi-plataforma
└── library.properties         # Arduino Library Manager
```

---

## Uso rápido

```c
#include "llp_protocol.h"

llp_parser_t parser;

void setup() {
    Serial.begin(115200);
    llp_parser_init(&parser);
}

void loop() {
    while (Serial.available()) {
        uint8_t byte = Serial.read();
        int result = llp_parser_process_byte(&parser, byte, millis());
        if (result == 1) {
            uint8_t data[64];
            int len = llp_get_final_payload(&parser.frame, data, sizeof(data));
            if (len > 0) {
                // Procesar datos recibidos
            }
        }
    }
}
```

---

## Ejecutar tests

```bash
platformio test -e test -v
```

---

## Licencia

MIT — Ver [LICENSE](LICENSE)
