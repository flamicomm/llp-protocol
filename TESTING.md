# Testing Guide - LLP Protocol v3.0.0

## Quick Start

### Prerequisites

Instala PlatformIO:

```bash
pip install platformio
```

### Run All Tests

```bash
cd llp-protocol
git checkout feature/v3.0.0
platformio test -e test -v
```

Expected output:
```
========= [PASSED] ========= 
33 passed in 0.42s
```

---

## Test Structure

### 4 Test Suites

| Suite | File | Tests | Focus |
|-------|------|-------|-------|
| **Initialization** | `test/test_parser_init.c` | 5 | Parser startup, state reset |
| **Frame Building** | `test/test_frame_builder.c` | 10 | Frame construction, CRC |
| **Processing** | `test/test_parser_process.c` | 9 | Byte parsing, state machine |
| **CRC Validation** | `test/test_crc.c` | 9 | CRC16-CCITT, error detection |

**Total: 33 tests**

---

## Test Execution Examples

### Run Tests Verbosely

```bash
platformio test -e test -v
```

Output:
```
Running test_parser_init_sets_state_to_waiting_magic ... [PASSED]
Running test_parser_init_initializes_all_fields ... [PASSED]
...
```

### Run Specific Test File

```bash
platformio test -e test --filter test_frame_builder
```

### Run Tests with Extra Verbosity

```bash
platformio test -e test -vvv
```

Shows compiler warnings, full linking output, etc.

---

## Test File Breakdown

### test_parser_init.c

**Purpose:** Validar que el parser se inicialice correctamente

**Tests:**
1. `test_parser_init_sets_state_to_waiting_magic` - Estado inicial correcto
2. `test_parser_init_initializes_all_fields` - Todos los campos inicializados
3. `test_parser_init_clears_frame_buffer` - Buffer vacío
4. `test_parser_can_be_reinitialized` - Reset funciona
5. `test_parser_init_resets_stats` - Estadísticas a cero

**Example assertion:**
```c
TEST_ASSERT_EQUAL_INT(0, parser.state);
```

### test_frame_builder.c

**Purpose:** Validar construcción correcta de frames

**Tests:**
1. `test_build_frame_with_empty_payload` - Frame sin payload
2. `test_build_frame_sets_correct_magic_bytes` - 0xAA, 0x55
3. `test_build_frame_sets_type_field` - Campo tipo correcto
4. `test_build_frame_stores_id_little_endian` - ID en LE
5. `test_build_frame_sets_payload_length` - Longitud correcta
6. `test_build_frame_copies_payload_correctly` - Datos copiados
7. `test_build_frame_includes_crc16` - CRC presente
8. `test_build_frame_returns_zero_for_small_buffer` - Validación de buffer
9. `test_build_frame_returns_zero_for_oversized_payload` - Payload máximo
10. `test_build_frame_works_with_all_message_types` - Todos los tipos

**Example:**
```c
uint8_t buffer[520];
size_t frame_len = llp_build_frame(
    buffer, sizeof(buffer),
    LLP_PING, 123,
    NULL, 0
);
TEST_ASSERT_EQUAL_HEX8(0xAA, buffer[0]);
```

### test_parser_process.c

**Purpose:** Validar procesamiento byte por byte

**Tests:**
1. `test_process_byte_returns_zero_for_incomplete_frame` - Retorna 0 si incompleto
2. `test_process_byte_returns_one_for_complete_frame` - Retorna 1 si completo
3. `test_process_byte_extracts_correct_frame_data` - Datos extraídos correctamente
4. `test_process_byte_returns_negative_one_for_crc_error` - Retorna -1 si error
5. `test_process_byte_recovers_from_bad_magic_bytes` - Recuperación de ruido
6. `test_process_byte_handles_multiple_frames` - Múltiples frames
7. `test_process_byte_increments_frames_ok_counter` - Contador de frames OK
8. `test_process_byte_handles_maximum_payload` - Payload máximo
9. `test_process_byte_handles_all_message_types` - Todos los tipos

**Example:**
```c
llp_parser_t parser;
llp_parser_init(&parser);

// Procesar frame byte por byte
for (size_t i = 0; i < frame_len; i++) {
    result = llp_parser_process_byte(&parser, buffer[i], 0);
}

TEST_ASSERT_EQUAL_INT(1, result);  // Frame completo
```

### test_crc.c

**Purpose:** Validar CRC16-CCITT

**Tests:**
1. `test_crc16_calculated_for_empty_frame` - CRC incluso sin payload
2. `test_crc16_differs_for_different_payloads` - CRC varía con payload
3. `test_crc16_differs_for_different_types` - CRC varía con tipo
4. `test_crc16_differs_for_different_ids` - CRC varía con ID
5. `test_crc16_detects_single_bit_flip_in_payload` - Detecta 1 bit cambiado
6. `test_crc16_detects_burst_error` - Detecta múltiples bits cambiados
7. `test_crc16_stored_in_correct_byte_positions` - Posición correcta
8. `test_crc16_valid_for_maximum_payload` - Válido con payload máximo
9. `test_crc16_is_deterministic` - Resultado consistente

---

## Understanding Test Results

### Success Output

```
Platform	native	[100%]	✔
Test	✔	33 passed in 0.42s
========= [PASSED] =========
```

**Meaning:** Todos los 33 tests pasaron.

### Failure Output

```
Running test_build_frame_sets_correct_magic_bytes ... [FAILED]

Expected 0xAA but was 0xFF
```

**Meaning:** El primer byte del frame no es 0xAA. Revisa `llp_build_frame()`.

### Compilation Error

```
src/llp_protocol.c:15:5: error: conflicting types for 'llp_parser_init'
```

**Meaning:** Hay un error en el código. Verifica los tipos en la declaración vs definición.

---

## Common Test Patterns

### Pattern 1: Basic Functionality Test

```c
void test_feature_works_correctly(void) {
    // Setup
    llp_parser_t parser;
    llp_parser_init(&parser);
    
    // Action
    int result = llp_parser_process_byte(&parser, 0xAA, 0);
    
    // Assert
    TEST_ASSERT_EQUAL_INT(0, result);  // Incompleto
}
```

### Pattern 2: Error Handling Test

```c
void test_feature_handles_error(void) {
    uint8_t buffer[4];  // Muy pequeño
    
    size_t frame_len = llp_build_frame(
        buffer, sizeof(buffer),
        LLP_DATA, 1,
        NULL, 0
    );
    
    TEST_ASSERT_EQUAL_INT(0, frame_len);  // Error
}
```

### Pattern 3: Integration Test

```c
void test_build_and_parse_frame(void) {
    // Construir
    uint8_t buffer[520];
    uint8_t payload[] = {0xAA, 0xBB};
    
    size_t frame_len = llp_build_frame(
        buffer, sizeof(buffer),
        LLP_DATA, 123,
        payload, sizeof(payload)
    );
    
    // Parsear
    llp_parser_t parser;
    llp_parser_init(&parser);
    
    for (size_t i = 0; i < frame_len; i++) {
        llp_parser_process_byte(&parser, buffer[i], 0);
    }
    
    // Verificar
    TEST_ASSERT_EQUAL_INT(LLP_DATA, parser.frame.type);
    TEST_ASSERT_EQUAL_INT(123, parser.frame.id);
}
```

### Pattern 4: Boundary Test

```c
void test_maximum_payload_size(void) {
    uint8_t buffer[1024];
    uint8_t max_payload[LLP_MAX_PAYLOAD];
    
    size_t frame_len = llp_build_frame(
        buffer, sizeof(buffer),
        LLP_DATA, 1,
        max_payload, LLP_MAX_PAYLOAD  // Máximo permitido
    );
    
    TEST_ASSERT_GREATER_THAN_INT(0, frame_len);
}
```

---

## Unity Assertions Reference

Common assertions used in tests:

```c
TEST_ASSERT(condition)                    // condition == true
TEST_ASSERT_EQUAL_INT(expected, actual)   // Integers
TEST_ASSERT_EQUAL_HEX8(expected, actual)  // Hex bytes
TEST_ASSERT_EQUAL_HEX16(expected, actual) // Hex words
TEST_ASSERT_GREATER_THAN_INT(threshold, actual)
TEST_ASSERT_NULL(pointer)
TEST_ASSERT_NOT_NULL(pointer)
TEST_ASSERT_TRUE(condition)
TEST_ASSERT_FALSE(condition)
TEST_ASSERT_NOT_EQUAL_INT(unexpected, actual)
TEST_FAIL_MESSAGE("Custom error message")
```

---

## Compilation for Different Platforms

### Arduino UNO

```bash
platformio run -e arduino_uno
```

Output:
```
Linking .pio/build/arduino_uno/firmware.elf
Checking size .pio/build/arduino_uno/firmware.elf
   text    data     bss     dec     hex filename
   1234     256     100    1590    63a  .pio/build/arduino_uno/firmware.elf
```

### ESP32

```bash
platformio run -e esp32
```

### Multiple Platforms

```bash
platformio run  # Compila todos los entornos
```

---

## Debugging Failed Tests

### Step 1: Run with Verbose Output

```bash
platformio test -e test -vvv
```

### Step 2: Add Debug Printing (Unity)

```c
void test_debug_example(void) {
    llp_parser_t parser;
    llp_parser_init(&parser);
    
    TEST_PRINTF("Parser state: %d\n", parser.state);
    TEST_PRINTF("Parser bytes_received: %d\n", parser.bytes_received);
    
    TEST_ASSERT_EQUAL_INT(0, parser.state);
}
```

### Step 3: Check Values

```bash
platformio test -e test -v 2>&1 | grep "ERROR\|FAILED"
```

### Step 4: Inspect Code

```c
// Verifica que llp_protocol.c implementa:
extern void llp_parser_init(llp_parser_t* parser);
extern int llp_parser_process_byte(llp_parser_t* parser, 
                                    uint8_t byte, 
                                    unsigned long current_ms);
extern size_t llp_build_frame(uint8_t* out_buffer,
                              size_t out_buffer_size,
                              uint8_t type,
                              uint16_t id,
                              const uint8_t* payload,
                              uint16_t payload_len);
```

---

## CI/CD with GitHub Actions

Tests se ejecutan automáticamente en:
- Cada push a `main`, `develop`, o `feature/*`
- Cada pull request a `main` o `develop`

Ver resultados en GitHub → Actions → Workflow runs

---

## Next Steps

1. **Implementar** `src/llp_protocol.c` según v3.0.0
2. **Ejecutar tests** - `platformio test -e test -v`
3. **Iterar** - Arregla fallos, mejora cobertura
4. **Validar hardware** - `platformio run -e arduino_uno`
5. **Commit** - Cuando todo funcione

---

## Useful Commands Cheatsheet

```bash
# Testing
platformio test -e test -v                    # Run all tests verbosely
platformio test -e test -vvv                  # Extra verbose
platformio test -e test --filter test_crc     # Specific test file

# Building
platformio run -e arduino_uno                 # Compile for Arduino UNO
platformio run -e esp32                       # Compile for ESP32
platformio run                                # Compile all environments

# Cleanup
platformio run -t clean                       # Clean build artifacts
pio lib update                                # Update libraries

# Info
platformio boards                             # List available boards
platformio envs                               # List configured environments
```

---

For more details, see `README.md` and `STRUCTURE.md`.
