# Project Structure - LLP Protocol v3.0.0

## Directory Tree

```
llp-protocol/
│
├── 📄 platformio.ini                    # Configuración central de PlatformIO
│                                         # Define entornos, compiladores, flags
│
├── 📁 src/                              # Código fuente principal
│   └── llp_protocol.c                  # Implementación del protocolo LLP
│
├── 📁 include/                          # Headers públicos
│   └── llp_protocol.h                  # API pública del protocolo
│
├── 📁 test/                             # Suite de tests con Unity
│   ├── test_parser_init.c              # 5 tests - Inicialización
│   ├── test_frame_builder.c            # 10 tests - Construcción de frames
│   ├── test_parser_process.c           # 9 tests - Procesamiento byte-a-byte
│   └── test_crc.c                      # 9 tests - Validación de CRC16-CCITT
│
├── 📁 lib/                              # Librerías externas
│   └── (unity instalado automáticamente)
│
├── 📁 examples/                         # Sketches y ejemplos
│   ├── minimal_uart/
│   │   └── minimal_uart.ino            # Ejemplo: UART simple
│   └── request_response/
│       └── request_response.ino        # Ejemplo: Request-Response
│
├── 📁 docs/                             # Documentación
│   ├── (specs detalladas del protocolo)
│
├── 📁 .github/
│   └── workflows/
│       └── platformio.yml              # CI/CD automatizado en GitHub Actions
│
├── 📄 .gitignore                        # Archivos ignorados por Git
├── 📄 README.md                         # Documentación principal
├── 📄 TESTING.md                        # Guía de testing (este archivo)
├── 📄 STRUCTURE.md                      # Explicación de estructura
├── 📄 CHANGELOG.md                      # Historial de cambios
├── 📄 LICENSE                           # MIT License
└── 📄 library.properties                # Metadata para Arduino Library Manager
```

## File Purposes

### Core Files

| Archivo | Propósito | Tamaño Típico |
|---------|-----------|---------------|
| `platformio.ini` | Configuración central | ~2.5 KB |
| `src/llp_protocol.c` | Implementación | ~3-5 KB |
| `include/llp_protocol.h` | API pública | ~1-2 KB |
| `lib/unity/` | Framework de testing | ~100 KB |

### Test Files

| Archivo | Descripción | Tests | Cobertura |
|---------|-------------|-------|-----------|
| `test/test_parser_init.c` | Inicialización del parser | 5 | Estado inicial, variables |
| `test/test_frame_builder.c` | Construcción de frames | 10 | Magic, tipo, ID, payload, CRC |
| `test/test_parser_process.c` | Procesamiento byte-a-byte | 9 | Parsing, state machine, recovery |
| `test/test_crc.c` | Validación CRC16-CCITT | 9 | Checksum, detección de errores |

**Total: 33 tests**

### GitHub Actions

| Archivo | Propósito |
|---------|-----------|
| `.github/workflows/platformio.yml` | Ejecuta tests en cada push/PR |

### Documentation

| Archivo | Contenido |
|---------|----------|
| `README.md` | Documentación principal, instalación, API |
| `TESTING.md` | Guía completa de testing |
| `STRUCTURE.md` | Este archivo - explicación de estructura |
| `CHANGELOG.md` | Historial de versiones |
| `docs/` | Especificaciones detalladas del protocolo |

## Configuration: platformio.ini

### Section: [platformio]

Define comportamiento global de PlatformIO:

```ini
[platformio]
src_dir = src                    # Dónde está el código principal
lib_dir = lib                    # Dónde van las librerías externas
include_dir = include            # Path de headers
test_dir = test                  # Dónde están los tests
default_envs = test              # Entorno por defecto
```

### Environments

Cada `[env:nombre]` define cómo compilar para una plataforma:

```ini
[env:test]
platform = native               # Compilar con gcc nativo (no Arduino)
test_framework = unity          # Usar Unity para tests
build_flags =                   # Flags del compilador C
    -Wall
    -DLLP_MAX_PAYLOAD=512
```

### Available Environments

```bash
platformio test -e test         # Tests (compilación nativa)
platformio run -e arduino_uno   # Arduino UNO
platformio run -e arduino_nano  # Arduino Nano
platformio run -e arduino_mega  # Arduino Mega
platformio run -e esp8266       # ESP8266
platformio run -e esp32         # ESP32
platformio run -e stm32f103     # STM32
platformio run -e attiny85      # ATtiny85
```

## Build Process

### Step 1: Compilation

```
Source Files (.c, .h) 
    ↓
Preprocessor (macros, includes)
    ↓
C Compiler (gcc/arm-gcc)
    ↓
Object Files (.o)
```

### Step 2: Linking

```
Object Files (.o)
    ↓
Unity Framework Library
    ↓
Linker
    ↓
Executable
```

### Step 3: Execution (Tests)

```
Executable
    ↓
Run Tests
    ↓
Compare Results
    ↓
Pass/Fail Report
```

## Memory Layout (Arduino UNO)

```
Total RAM: 2 KB (2048 bytes)

llp_parser_t struct:        ~600 bytes
llp_frame_t struct:         ~520 bytes
Local variables:            ~100 bytes
Stack overhead:             ~100 bytes
───────────────────────────
Available for application:  ~730 bytes (36% of RAM)
```

## Build Artifacts

PlatformIO crea automáticamente:

```
.pio/
├── build/
│   ├── test/                    # Compilación de tests
│   │   ├── firmware.elf
│   │   └── (archivos objeto)
│   ├── arduino_uno/
│   │   ├── firmware.hex        # Imagen para cargar en Arduino
│   │   └── (archivos objeto)
│   └── ...
├── build_cache/
└── ...
```

## Adding New Features

### Scenario: Añadir nueva función `llp_validate_frame()`

#### 1. Declare en Header

```c
// include/llp_protocol.h
bool llp_validate_frame(const llp_frame_t* frame);
```

#### 2. Implement en Source

```c
// src/llp_protocol.c
bool llp_validate_frame(const llp_frame_t* frame) {
    if (!frame) return false;
    if (frame->payload_len > LLP_MAX_PAYLOAD) return false;
    return true;
}
```

#### 3. Write Tests

```c
// test/test_validation.c
void test_validate_frame_returns_true_for_valid_frame(void) {
    llp_frame_t frame = {
        .type = LLP_DATA,
        .id = 1,
        .payload_len = 10
    };
    TEST_ASSERT_TRUE(llp_validate_frame(&frame));
}

void test_validate_frame_returns_false_for_null(void) {
    TEST_ASSERT_FALSE(llp_validate_frame(NULL));
}
```

#### 4. Run Tests

```bash
platformio test -e test -v
```

#### 5. Commit

```bash
git add include/llp_protocol.h src/llp_protocol.c test/test_validation.c
git commit -m "Add frame validation function"
```

## Version Management

### Current Version Structure

```
Version 3.0.0
│
├── Major (3)  - Breaking changes
├── Minor (0)  - New features (backwards compatible)
└── Patch (0)  - Bug fixes
```

Update in:
1. `library.properties` - Arduino Library Manager
2. `README.md` - Documentation
3. `CHANGELOG.md` - Release notes
4. GitHub Tags - `git tag v3.0.0`

## Git Workflow

### Feature Branch

```bash
git checkout -b feature/v3.0.0
# ... hacer cambios ...
git add .
git commit -m "Add testing infrastructure"
git push origin feature/v3.0.0
```

### Create PR

- Push to GitHub
- Open Pull Request
- CI/CD runs automatically
- Review and merge

### Release

```bash
git checkout main
git merge feature/v3.0.0
git tag v3.0.0
git push origin main --tags
```

## Performance Considerations

### Code Size

| Component | Flash | RAM |
|-----------|-------|-----|
| llp_protocol.c | ~1-2 KB | ~600 B |
| Unity tests | ~0 B | ~1 KB |
| Arduino Framework | ~2 KB | ~400 B |

### Execution Time

- **Parsing 1 byte**: ~50 µs
- **Building frame**: ~100 µs
- **CRC calculation**: ~15 µs per byte

## Troubleshooting

### Issue: Tests don't compile

```bash
# Verificar path de includes
platformio test -e test -vvv

# Limpiar y reconstruir
platformio run -t clean
platformio test -e test
```

### Issue: Can't find Unity framework

```bash
# Reinstalar dependencias
pio lib update
platformio run --update-all
```

### Issue: Code compiles for test but not for Arduino

Verificar:
1. Includes en `platformio.ini`
2. Archivos de configuración de Arduino no compilados en test
3. Usar `#ifdef` para código específico de plataforma

```c
#ifdef ARDUINO
    Serial.println("Arduino only");
#endif
```

## Best Practices

### 1. Organize Tests Logically

Separate test files por funcionalidad:
- `test_parser_*.c` para parser
- `test_frame_*.c` para frame building
- `test_crc_*.c` para CRC

### 2. Keep Tests Independent

Cada test debe funcionar sin depender de otros.

✅ Bueno:
```c
void test_feature_a(void) {
    // Setup completo
    llp_parser_t parser;
    llp_parser_init(&parser);
    // Test
}
```

### 3. Use Meaningful Names

```c
// ✅ Descriptive
void test_parser_recovers_from_corrupted_magic_bytes(void) { }

// ❌ Vague
void test_parser_recovery(void) { }
```

### 4. Document Complex Tests

```c
// Test que verifica sincronización RF con ruido simulado
void test_parser_syncs_with_rf_noise(void) {
    // Enviar bytes de ruido
    llp_parser_process_byte(&parser, 0xFF, 0);
    llp_parser_process_byte(&parser, 0xFF, 0);
    
    // Enviar frame válido
    // Debe recuperarse y parsear correctamente
}
```

## Next Steps

1. **Implementar `src/llp_protocol.c`** según v3.0.0
2. **Ejecutar tests** - `platformio test -e test -v`
3. **Compilar ejemplos** - `platformio run -e arduino_uno`
4. **Validar hardware** - Cargar en Arduino/ESP32
5. **Crear PR** - Abrir pull request a `main`

---

**For more details, see `TESTING.md` and `README.md`**
