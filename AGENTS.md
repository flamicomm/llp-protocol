# AGENTS.md — llp-protocol

Librería **C header-only** del wire LLP v4 y del perfil R1 (canal de comandos
confiable), más la referencia Python que genera y auto-verifica los vectores de
conformidad. Implementa la norma de `llp-spec`; **no la define**.

**Este archivo es normativo**: solo invariantes y comandos. API, integración y
máquinas de estado: `README.md` y `docs/`.

## Comandos

```bash
pio test -e test                              # suite Unity en el host (env nativo)
python3 scripts/gen_vectors_v4.py --check     # vectores vs referencia
sh scripts/check_robustness.sh                # fuzzer + canal con ruido
sh scripts/check_examples.sh                  # compila y corre los ejemplos
python3 scripts/measure_v4.py --budget        # presupuesto de RAM/flash
```

`platformio.ini` define además entornos de compilación cruzada (`arduino_uno`,
`arduino_nano`, `arduino_mega`, `esp8266`, `esp32`, `stm32f103`, `attiny85`) para
verificar que el header compile en cada arquitectura. Los tamaños vigentes los
imprime `scripts/measure_v4.py`; no los cites de memoria.

## Invariantes

- **Header-only, C99, sin dependencias de runtime**: `#include "llp_v4.h"`
  (helpers opcionales: `llp_v4_ring.h` y el perfil R1).
- **Sin heap, sin recursión, O(1) por byte.** Buffers del llamador: el parser
  escribe en el buffer de la aplicación y el encoder hace streaming a un sink,
  sin buffer de trama.
- **Tiempo inyectado** (`now_ms`): el parser nunca lee un reloj. Aritmética
  wrap-safe a 16 bits; el timeout no supera el máximo representable.
- **Errores son eventos**, no estados fatales: tras `LLP4_ERR_*` el parser queda
  listo para la próxima trama.
- **Presupuesto de memoria**: `measure_v4.py --budget` es el gate de CI y
  `src/llp_v4_check.c` lleva el static assert de tamaño del parser. Medí antes de
  agregar código; no relajes ninguno de los dos sin justificarlo.
- **`test/v4_vectors_generated.h` es generado** por
  `scripts/gen_vectors_v4.py`; no se edita a mano. El corpus vive en
  `spec/vectors/v4/` y se origina en `llp-spec`.
- **El wire cambia primero en `llp-spec`**: acá solo se implementa. Un cambio de
  comportamiento va en el C **y** en la referencia Python (`scripts/llp_v4.py`)
  en el mismo cambio.
- **`src/` es un smoke test de compilación** para cada entorno: no agregues
  lógica de aplicación ahí.
- **`docs/SPEC-v4.md` es un puntero a la norma**; el perfil R1 se define en
  `llp-spec/docs/RELIABILITY.md`. No dupliques ni redefinas la norma acá.

## Documentación

| archivo | contenido |
|---|---|
| `README.md` | API, formato, canales, perfil R1, ejemplos y checks |
| `docs/SPEC-v4.md` | puntero a la norma del wire en `llp-spec` |
| `docs/INTEGRATION.md` | cómo conectar el parser/encoder a un enlace |
| `docs/DECISIONS.md` | ADR log (incluye los cambios de wire) |
| `docs/CONFORMANCE.md` | schema de vectores y checklist |
| `docs/ROBUSTNESS.md` | comportamiento medido bajo ruido |
| `docs/WHY-LLP.md` | comparación con COBS/SLIP/HDLC/Modbus |
| `docs/PROTOCOL.md` | spec histórica v3.1.0 (no se actualiza) |

## Git

- `dev` y `main` son **ramas protegidas**: solo PR, nunca commits directos.
- Ramas `feature/<scope>-<slug>`, `fix/<slug>`, `docs/<slug>`, `hotfix/<slug>`.
- Un PR = un tema; squash a `dev`; release `dev → main` con tag `vX.Y.Z`.
- Commits Conventional Commits **en español**; código y comentarios en inglés
  (estilo del repo).
