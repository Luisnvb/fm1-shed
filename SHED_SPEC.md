# Shed — Entrenador de oído jazzístico para el M-VAVE FM-1

Documento de especificación y traspaso para continuar el desarrollo en Claude Code.
Colócalo en la raíz del repositorio `fm1_open` y pide a Claude Code que lo lea antes de empezar.

> *Shed* viene de *woodshedding*, la jerga del jazz para practicar a solas.

---

## 1. Objetivo

Una aplicación que corre en el FM-1 (y en su simulador web) para **entrenar el oído en contexto de jazz**: intervalos, grados funcionales, calidades de acorde, tensiones, arpegios, escalas y progresiones.

Principios:

1. **El oído es lo primero.** Las funciones de teoría y el modo improvisación son secundarios y se construyen después.
2. **Siempre en contexto tonal.** Antes de cada pregunta suena una cadencia que fija la tonalidad. Se entrena a oír *funciones*, no sonidos aislados.
3. **Se responde tocando.** Siempre que tenga sentido, la respuesta se da en el teclado del FM-1 (el aparato sabe qué tecla pulsas). Selección con encoder solo cuando tocar no aplica (por ejemplo, nombrar una calidad de acorde).
4. **Las 12 tonalidades.** Rotación por tonalidades (ciclo de cuartas o aleatoria).
5. **Sin hardware.** Todo el desarrollo y las pruebas se hacen en el simulador web (workbench local). El hardware es un destino futuro opcional.

---

## 2. Plataforma (hechos verificados en el código, 22-oct-2026)

Repositorio: <https://github.com/m-vave/fm1_open> (FM1 Open V14). Workbench oficial: <https://open.m-vave.com/fm1-workbench>.

| Tema | Detalle | Dónde |
|---|---|---|
| Pantalla | 240×240 px, RGB565 | `sdk/include/fm1.h` (`FM1_SCREEN_WIDTH/HEIGHT`) |
| Teclas de nota | 27 teclas, **F3..G5**, bits 0..26 de `fm1_controls.keys` | `sdk/include/fm1.h` (`struct fm1_controls`) |
| Botones | 14 botones, `fm1_controls.buttons` bits 0..13 | ídem |
| Encoders | 7 encoders, `fm1_controls.encoders[7]` (detents desde la última lectura) | ídem |
| LEDs | `fm1_led_set(key, on)`: 0..13 botones, **14..40 teclas de nota** | ídem |
| Reloj | `fm1_millis()` (comparar por resta sin signo) | ídem |
| Disparar notas | `fm1_midi_post(msg, bytes)` (SDK) encola MIDI en el instrumento. En la práctica la app no lo llama directo: usa `groove_midi_local(msg, bytes)` (`app/instrument.h`), que añade validación/canal y es lo que usan tanto `screen.c` para las teclas físicas como `app/shed/player.c`. Solo desde la tarea de control, nunca desde ISR | `sdk/include/fm1.h`, `app/instrument.h`, ejemplo en `app/screen.c` (~l. 2550, función estática `post()`) |
| Pistas | 8 pistas; canales MIDI 1–7 → pistas 1–7, canal 10 → pista 8 | `app/instrument.h` (`groove_channel`), README |
| Bucle de control | `groove_screen_control()` lee controles y dibuja | `app/screen.c` (~l. 2564) |
| Primitivas de dibujo | `text()`, `frame()`, `line()`, `paint_mark()` (estáticas en `screen.c`) | `app/screen.c` |
| Simulador web | `web/engine/Simulator.c` compila `app/` + `web/` a WebAssembly | `web/engine/` |
| Motor FM | DX7 de 6 operadores; banco de fábrica de 128 voces (incluye pianos eléctricos FM) | `app/fm_factory.inc`, `web/synth/` |

**Verificado (Fase 0, 22-oct-2026):**
- Los LEDs de las teclas de nota se ven en el simulador web: al pulsar una tecla en el workbench local, el borde se resalta visualmente (confirmado con captura). `groove_screen_web_mask` efectivamente expone esto al front-end.
- El workbench local (`build/preview/index.html`, generado con `tools\Build-Web.ps1 -LocalPreview`) es visualmente idéntico al oficial (<https://open.m-vave.com/fm1-workbench>): mismo layout, versión V14, mismo estado inicial de pantalla y controles, sin errores de consola, motor de audio arranca ("Sound enabled") al interactuar.

**Pendiente de verificar** (no es bloqueante para Fase 0; abordar en Fase 2/3):
- Cuál es la mejor voz de fábrica para piano eléctrico y para bajo.
- Cómo cambiar la voz de una pista por código sin tocar el proyecto del usuario.

### Compilación del workbench local

Según el README, `.\tools\Build-Web.ps1 -LocalPreview` genera `build/preview/index.html`, que se abre en Chrome o Edge. El script está escrito para Windows (PowerShell, `clang.exe`, descarga del WASI SDK 27 para Windows).

En macOS/Linux habrá que adaptarlo: instalar `pwsh` o reescribir el script en bash/Node, usar el WASI SDK 27 de la plataforma correspondiente (`FM1_WASI_SDK`) y corregir las rutas `*.exe`. **Primera tarea técnica:** conseguir que el workbench local sin modificar compile y funcione igual que el oficial.

**✅ Hecho (Fase 0, 22-oct-2026).** `tools\Build-Web.ps1 -LocalPreview` genera `build/preview/index.html`, verificado visualmente idéntico al oficial (ver sección 2, "Verificado"). Nota práctica: la descarga automática del WASI SDK dentro del script (`curl.exe` con barra de progreso) puede fallar en PowerShell 5.1 con `NativeCommandError` porque `$ErrorActionPreference = 'Stop'` convierte la salida de progreso de `curl` por stderr en una excepción, aunque la descarga en sí sea correcta. Solución: descargar y extraer el WASI SDK 27 a mano (`curl.exe --silent --show-error`, luego `tar.exe -xzf`, verificando el SHA-256 que indica el propio script) y pasar `FM1_WASI_SDK` como variable de entorno apuntando al directorio extraído; el script detecta que ya existe y salta la descarga.

El firmware para hardware requiere la toolchain JieLi 2.5.3 (solo Windows). Fuera de alcance por ahora.

---

## 3. Arquitectura

```
app/shed/
  theory.h / theory.c    # Núcleo de teoría. C puro, sin dependencias del FM-1.
  exercises.h / .c       # Generador de preguntas y evaluación de respuestas.
  progress.h / .c        # Estadísticas y repetición espaciada (en RAM).
  player.h / .c          # Secuencia eventos MIDI en el tiempo vía fm1_midi_post.
  ui.h / ui.c            # Pantallas, lectura de controles, LEDs.
  shed.h                 # Punto de entrada: shed_enter(), shed_exit(), shed_tick().
tests/shed/              # Tests de host (gcc/clang nativo) para theory y exercises.
```

Reglas:

- **Aislamiento.** Todo el código nuevo vive en `app/shed/`. Los cambios en archivos existentes se limitan a unos pocos ganchos (entrar/salir del modo y delegar el bucle de control). `screen.c` tiene casi 3000 líneas: no refactorizarlo.
- **Núcleo testeable.** `theory` y `exercises` no incluyen cabeceras del FM-1 y se prueban en el host con tests unitarios.
- **Sin escrituras en flash en el MVP.** El progreso vive en RAM. La persistencia es una fase posterior (y conlleva riesgo en hardware).
- **No pisar el proyecto del usuario.** Al entrar en Shed se guardan voces, roles y estado de las pistas que se usen; al salir se restauran.
- **Memoria.** Tablas compactas y `const`. Nada de `malloc`.
- **Licencia.** La APP es GPLv3 con excepción de enlace del SDK. Mantener cabeceras SPDX y el código fuente publicado si se distribuye.

### Entrada al modo

Proponer una combinación de botones que no choque con las existentes (por ejemplo, mantener HOME + otro botón). Revisar `groove_screen_control()` para elegirla. En el simulador puede añadirse también un control web.

### Audio

- Pista A (p. ej. 1): piano eléctrico FM para acordes y cadencias.
- Pista B (p. ej. 2): bajo FM para fundamentales y vamps.
- Pista C (opcional): voz clara para notas sueltas y melodías.
- Voicings de piano: sin fundamental, tipo A/B (3-5-7-9 / 7-9-3-5), en el registro medio. El bajo toca la fundamental.
- El `player` trabaja con una cola de eventos `{t_ms, canal, nota, velocidad, on/off}` que avanza en cada `shed_tick()` según `fm1_millis()`. Siempre enviar note-off; en `shed_exit()` enviar all-notes-off.

---

## 4. Núcleo de teoría

Clases de altura 0..11 (C = 0). Intervalos en semitonos desde la fundamental.

### Tipos de acorde

| Símbolo | Intervalos | Notas |
|---|---|---|
| maj7 | 0 4 7 11 | |
| 6/9 | 0 4 7 9 14 | |
| m7 | 0 3 7 10 | |
| m6 | 0 3 7 9 | |
| m(maj7) | 0 3 7 11 | |
| 7 | 0 4 7 10 | |
| 7sus4 | 0 5 7 10 | |
| m7♭5 | 0 3 6 10 | |
| dim7 | 0 3 6 9 | |
| maj7♯11 | 0 4 7 11 18 | |
| 7♭9 | 0 4 7 10 13 | |
| 7♯11 | 0 4 7 10 18 | |
| 7alt | 0 4 10 + ♭9/♯9/♭13 (13 15 20) | Quinta omitida |

### Acorde → escala

| Acorde (función) | Escala por defecto | Alternativas | Nota a evitar (tiempo fuerte) |
|---|---|---|---|
| maj7 (I) | Jónica | Lidia | 4 |
| maj7 (IV) | Lidia | — | — |
| m7 (II) | Dórica | — | — |
| m7 (III) | Frigia | — | ♭2, ♭6 |
| m7 (VI) | Eólica | — | ♭6 |
| 7 (V → mayor) | Mixolidia | Bebop dominante | 4 |
| 7♯11 | Lidia dominante | — | — |
| 7alt | Alterada | — | — |
| 7♭9 (V → menor) | Mixolidia ♭9 ♭13 (frigia dominante) | Disminuida semitono-tono | — |
| m7♭5 | Locria | Locria ♮2 | ♭2 (en locria) |
| dim7 | Disminuida tono-semitono | — | — |
| m(maj7), m6 | Menor melódica | — | — |

### Escalas (intervalos)

Jónica 0 2 4 5 7 9 11 · Dórica 0 2 3 5 7 9 10 · Frigia 0 1 3 5 7 8 10 · Lidia 0 2 4 6 7 9 11 · Mixolidia 0 2 4 5 7 9 10 · Eólica 0 2 3 5 7 8 10 · Locria 0 1 3 5 6 8 10 · Menor melódica 0 2 3 5 7 9 11 · Lidia dominante 0 2 4 6 7 9 10 · Locria ♮2 0 2 3 5 6 8 10 · Alterada 0 1 3 4 6 8 10 · Disminuida tono-semitono 0 2 3 5 6 8 9 11 · Disminuida semitono-tono 0 1 3 4 6 7 9 10 · Tonos enteros 0 2 4 6 8 10 · Bebop dominante 0 2 4 5 7 9 10 11 · Frigia dominante 0 1 4 5 7 8 10.

Las tablas deben estar cubiertas por tests.

---

## 5. Ejercicios de oído

Formato común de cada pregunta:

1. **Contexto:** cadencia en la tonalidad (por defecto II-V-I con séptimas; opción I-IV-V-I en tríadas para niveles iniciales).
2. **Estímulo:** lo que hay que reconocer.
3. **Respuesta:** tocando en el teclado o eligiendo con un encoder.
4. **Feedback:** acierto/fallo en pantalla, LEDs sobre la respuesta correcta y reproducción de la respuesta correcta tras un fallo.

Las respuestas tocadas se evalúan **por clase de altura** (independientes de la octava), salvo que el ejercicio indique lo contrario. El teclado solo cubre F3..G5.

| # | Ejercicio | Estímulo | Respuesta | Progresión de dificultad |
|---|---|---|---|---|
| 1 | **Grado funcional** | Cadencia + una nota | Tocar la nota | Diatónicos → cromáticos; registro fijo → variable |
| 2 | **Intervalo melódico** | Nota de referencia + segunda nota | Tocar la segunda nota | Ascendentes → descendentes; dentro de la octava → compuestos (9, 11, 13) |
| 3 | **Intervalo armónico** | Dos notas a la vez | Tocar la nota superior | Ídem |
| 4 | **Calidad de acorde** | Cadencia + acorde | Encoder: lista de tipos | Tríadas → cuatríadas → con tensiones |
| 5 | **Tensión sobre dominante** | Acorde 7 + una nota encima | Encoder: 9, ♭9, ♯9, ♯11, 13, ♭13 | Pocas opciones → todas |
| 6 | **Arpegio** | Arpegio de un acorde | Tocar el arpegio (orden y notas) | Fundamental arriba → inversiones → desde la 3ª/7ª |
| 7 | **Escala/modo** | Escala sobre un bajo pedal | Encoder: lista de modos | Modos de la mayor → menor melódica → simétricas |
| 8 | **Movimiento del bajo** | Progresión de 2–4 acordes | Tocar las fundamentales en orden | II-V-I → turnarounds → blues, rhythm changes |
| 9 | **Dictado melódico** | Frase de 3–8 notas sobre un vamp | Tocarla de vuelta | Diatónica → cromatismos, aproximaciones |

Controles comunes durante un ejercicio (mapeo final a decidir tras revisar la UI existente):

- **Repetir estímulo** · **Repetir cadencia** · **Revelar** · **Siguiente**
- Encoders: tonalidad (fija / ciclo de cuartas / aleatoria), tempo, nivel, tipo de ejercicio, volumen relativo de la cadencia.

### Pedagogía

- **Repetición espaciada:** cada elemento (por ejemplo, "grado ♭3 en tonalidad de E♭") tiene una puntuación; los fallos aparecen más a menudo.
- **Dificultad adaptativa:** el nivel sube tras N aciertos seguidos y baja tras varios fallos.
- **Sesiones cortas:** bloques de 10–20 preguntas con resumen final (porcentaje de aciertos, elementos más fallados).

---

## 6. Modo vamp (fase posterior)

El aparato toca un vamp (bajo + comping) y el usuario improvisa en el teclado.

- Vamps iniciales: modal Dm7; Dm7–G7; II-V-I menor; Cmaj7–A7–Dm7–G7; blues en F.
- Los **LEDs de las teclas** muestran las opciones: encendido fijo = notas del acorde; parpadeo = tensiones disponibles. Se cambian una pulsación antes del cambio de acorde.
- La pantalla muestra el acorde actual, el siguiente y la escala sugerida.
- Vistas: escala, arpegio, notas guía (3ª y 7ª), ninguna (solo escuchar).
- **Variante de oído:** el aparato toca una frase sobre el vamp y el usuario identifica qué tensión o escala usó.

---

## 7. Pantallas (boceto 240×240)

```
┌────────────────────────┐   ┌────────────────────────┐
│ SHED        KEY: B♭    │   │ GRADO FUNCIONAL   7/20 │
│                        │   │                        │
│ > Grado funcional      │   │    ¿Qué grado es?      │
│   Intervalos           │   │                        │
│   Calidad de acorde    │   │        ♪ ♪ ♪ → ?       │
│   Tensiones            │   │                        │
│   Arpegios             │   │  Toca la respuesta     │
│   Escalas              │   │                        │
│   Bajo / progresiones  │   │ [REP] [CAD] [VER] [>]  │
│   Dictado              │   │ Racha: 4   Nivel: 2    │
└────────────────────────┘   └────────────────────────┘
```

Tras responder: "✓ ♭7 (A♭)" o "✗ Era la 6 (G)", con LEDs sobre la tecla correcta.

---

## 8. Plan por fases

**Fase 0 — Entorno** ✅ 22-oct-2026
- Compilar el workbench local sin cambios en la plataforma del usuario (adaptar `Build-Web.ps1` si hace falta).
- Verificar los puntos marcados en la sección 2.
- *Aceptación:* el `index.html` local funciona como el workbench oficial. **Cumplida** (ver sección 2).

**Fase 1 — Núcleo** ✅ 22-oct-2026
- `theory` y `exercises` con tests de host.
- *Aceptación:* tests en verde; las tablas de la sección 4 cubiertas. **Cumplida:** `.\tools\Test-Shed.ps1` → `shed tests: 4320 checks, 0 failures`.
- `app/shed/theory.h` / `.c`: las 13 calidades de acorde, las 16 escalas y las 13 filas de la tabla acorde→escala de la sección 4, transcritas y testeadas una a una contra el documento (no contra sí mismas). `fm1_shed_chord_scale_lookup(tipo, contexto)` resuelve filas que dependen de la función (p. ej. maj7 en I vs. IV) con fallback a la fila genérica del acorde cuando no hay una fila específica para ese contexto, y devuelve `NULL` cuando ni siquiera existe la genérica (p. ej. m7 en un contexto V, que no está en la tabla).
- `app/shed/exercises.h` / `.c`: RNG determinista (xorshift32), rotación de tonalidad (`FM1_SHED_KEY_FIXED/FOURTHS/RANDOM`), y el generador/evaluador de los ejercicios **#1 grado funcional** y **#4 calidad de acorde** (los dos que integra la Fase 2), con bandas de dificultad — grado: diatónico (nivel 0) → cromático (nivel ≥1); acorde: 6 calidades básicas → 9 → las 13 completas. Los ejercicios #2,3,5–9 quedan para la Fase 3, tal como indica el plan.
- `progress`, `player`, `ui`, `shed.h` **no** se tocan en esta fase (según sección 3, son de Fase 2 en adelante).
- **Toolchain de tests de host:** esta máquina no tiene gcc/clang/cl nativos. `tools\Test-Shed.ps1` compila `tests/shed/*.c` + `app/shed/*.c` a wasm32-wasi con el mismo clang del WASI SDK 27 que ya usa `Build-Web.ps1` (`--sysroot` apunta al sysroot wasi, sin flag `--target`: el clang del SDK ya tiene wasm32-unknown-wasi por defecto) y lo ejecuta con el runtime WASI integrado en Node (`tools/wasi-run.mjs`, vía `node:wasi`) — sigue siendo una compilación C11 real con `-Wall -Wextra -Werror`, solo que corre en sandbox WASI en vez de nativo. Reutiliza el SDK ya descargado en `build/tools/wasi-sdk-27.0-x86_64-windows` (o `$env:FM1_WASI_SDK`).
- **Verificado que no rompe el build web:** `Build-Web.ps1` recorre `app/` recursivamente (salvo `app/board/`) y añade **todo** `.c` que encuentre al compilador, así que `app/shed/theory.c` y `exercises.c` ya se compilan dentro de `build/preview/index.html` aunque todavía no haya ningún `shed_enter()` que los llame. Se confirmó re-ejecutando `Build-Web.ps1 -LocalPreview` tras añadir estos archivos: compila limpio y el `.wasm` resultante pesa exactamente igual que antes (295723 bytes), es decir el enlazador descarta el código aún no referenciado — buena señal de que no hay colisión de símbolos ni efectos secundarios, y de que la Fase 2 solo necesita *llamar* a este núcleo, no registrarlo en ningún sitio nuevo del build.

**Fase 2 — MVP en el simulador** ✅ 22-oct-2026
- Entrar/salir de Shed; `player`; menú; ejercicios 1 (grado funcional) y 4 (calidad de acorde).
- *Aceptación:* **cumplida y verificada en vivo en el simulador** (sesión completa de 20 preguntas, tonalidad rotando por ciclo de cuartas, cadencia audible, respuesta por teclado y por encoder, feedback, resumen; proyecto del usuario intacto al salir — ver detalle abajo).

**Gesto de entrada/salida:** `HOME + SCL` (mantener HOME, pulsar SCL), simétrico para entrar y salir. Ninguno de los dos aparece junto en ningún atajo existente de `screen.c` (confirmado por inspección: los combos ya usados con HOME son PLAY, SAVE, SEQ, GLO y el encoder 6). El botón que el código interno llama `SCL` se etiqueta "SEL" en el panel/simulador — mismo botón, dos nombres.

**`app/shed/shed.h` / `.c` (punto de entrada):**
- `shed_enter()` / `shed_exit()` / `shed_tick()` tal como pide la sección 3. `shed_tick()` detecta el gesto con su propio edge-detector (no el `previous_buttons` de `screen.c`), y tras `shed_exit()` sigue devolviendo 1 (consumiendo el frame) **hasta que botones y teclas estén todos sueltos**, para que el `previous_buttons`/`previous_keys` de `screen.c` —que las llamadas activas de Shed se saltan por completo— se resincronice desde un estado "nada pulsado" en vez de uno obsoleto del momento en que Shed arrancó. Verificado en vivo: con HOME fijado (latched) tras el gesto de salida, la pantalla se queda en Shed hasta soltar HOME, y entonces sí vuelve a la UI normal en el frame siguiente, sin ningún input fantasma.
- Pequeña máquina de estados (MENU → QUESTION → FEEDBACK → SUMMARY) que genera preguntas vía `exercises.h`, programa audio vía `player.h` y registra respuestas vía `progress.h`. La tonalidad rota por ciclo de cuartas en **cada pregunta** (no por sesión), para cubrir variedad dentro de las 20 preguntas.

**`app/shed/player.h` / `.c` (audio):** reclama las pistas 0 (piano) y 1 (bajo) vía `groove_midi_local` (el mismo mecanismo que usa `post()` en `screen.c` para las teclas físicas — más preciso que la mención a `fm1_midi_post` de la sección 2, que es la primitiva de más bajo nivel que `groove_midi_local` envuelve con validación/canal; corregido abajo). Antes de tocar nada, guarda y restaura por pista: tipo de fuente, voz (`program`), rol, canal de entrada MIDI, mute, y **las 64 celdas de secuenciador** (`groove_sequence_read`/`_replace`), porque `groove_source_set` puede borrar celdas "incompatibles" al cruzar entre fuente de batería y melódica — sin este respaldo se habría perdido el patrón de la pista si el usuario tenía un 808/sampler ahí. También para el transporte si estaba sonando (MIDI Stop/Continue) y el flag de proyecto "sin cambios" (`groove_project_dirty`/`groove_project_clean`). Voces elegidas por nombre de fábrica: **PIANO 1** (programa 0) para piano, **BASS 1** (programa 34) para bajo — confirmado con una herramienta aparte que vuelca los 128 nombres de `app/fm_factory.inc`; sigue sin verificarse de oído cuál suena mejor (pregunta abierta de la sección 2). Voicings tipo A/B rotos (sin fundamental, ascendente / la nota superior bajada una octava) usando directamente los intervalos de `theory.c`.
- **Verificado en vivo:** tras una sesión completa (20 preguntas, ambos ejercicios, incluyendo cruces de fuente DX7↔303), al salir la vista de pistas (`HOME`) muestra exactamente `1 PIANO 1 · 2 303 SAW · 3 PSG VM · 4 PLUCK · 5 SAMPLER · 6 PIANO 1 MUTE · 7 303 SAW MUTE · 8 808 KIT` — idéntica al estado inicial, y el panel lateral vuelve a decir "Workspace unchanged".

**`app/shed/ui.h` / `.c` (pantallas, controles, LEDs):** dibuja con `fm1_screen_blit` en bandas de 16 filas (sin reutilizar el pipeline interno `text()`/`paint_fill()` de `screen.c`, que es `static` y no se exporta) y una tabla de fuente 5×7 propia (el patrón público-dominio clásico, igual que el de `screen.c` pero una copia propia, no compartida). Las teclas de nota se leen/iluminan con `fm1_controls.keys`/`fm1_led_set` directamente (API pública de `fm1.h`); los botones de panel (`HOME`, `SCL`, `PLAY`, `SEQ`, `EDIT`, `ARP`, `ENV`, `GLO`) se identifican por los mismos bits que usa el `enum` privado de `screen.c` — **no hay otra fuente de verdad para esos bits en el SDK**, así que se duplican con un comentario explícito en `ui.c` y `shed.c` en vez de exportar el enum (para no tocar `screen.c` más de lo imprescindible).
- Mapeo de controles durante una pregunta: `PLAY` repetir estímulo, `SEQ` repetir cadencia, `EDIT` revelar (cuenta como fallo), `ARP` siguiente (saltar, sin puntuar), `ENV` cambiar de ejercicio, dial `PRESETS` (↔ `encoders[6]`, confirmado contra `sdk/Simulator.js`) elegir calidad de acorde, `GLO` confirmar. Nivel de dificultad fijo (Fase 3 trae la dificultad adaptativa); las 12 tonalidades y ambos ejercicios sí rotan/alternan ya en la Fase 2.

**`app/shed/progress.h` / `.c`:** contador de sesión en RAM (`asked`, `correct`, `streak`, `best_streak`), sin repetición espaciada (Fase 3). Con tests de host.

**Gancho en `app/screen.c`:** un `#include "shed/shed.h"` y, al principio de `groove_screen_control()`, `if (shed_tick(&controls, now)) return;` (más partir una línea de declaración en dos, sin cambiar su lógica). Nada más se tocó de ese archivo.

**Bugs reales encontrados probando en vivo en el simulador** (ver metodología abajo) y corregidos:
1. El selector de calidad de acorde leía `encoders[0]`, que en el panel real es el dial **SELECT** (selector de pista), no un mando libre. Corregido a `encoders[6]`, que es el dial **PRESETS** — confirmado en `sdk/Simulator.js` (`const encoders=[['MASTER',-1,...],['SELECT',0,...],['PRESETS',6,...],...]`) y es además el mismo dial que ya usa el resto de la app para "recorrer una lista" (`groove_control_edit(track, encoders[6], 0, 0)` para las voces).
2. `append_uint()` no añade terminador nulo (por diseño, para poder componer cadenas); dos sitios en `ui.c` ("RACHA n" en feedback, "MEJOR RACHA n" en el resumen) lo llamaban sin añadir el `\0` después, dejando basura del `strcpy` anterior en el buffer — visto en vivo como "RACHA 07" en vez de "RACHA 0". Corregido en ambos sitios.
3. Los LEDs de teclas quedaban encendidos de una sesión/revelación anterior: no se limpiaban en `shed_enter()` (se veía una tecla iluminada en el propio menú) ni al pasar de FEEDBACK a SUMMARY. Corregido con `fm1_shed_ui_clear_leds()`/`fm1_shed_ui_show_answer_leds(-1)` en ambos puntos.

**Metodología de la prueba en vivo:** el simulador soporta "clavar" un botón sin mantener el ratón pulsado — Mayús+clic (o clic derecho) lo deja fijado (aparece un punto en la esquina del botón) hasta un segundo Mayús+clic, mientras un clic normal en otro botón genera un pulso de ~55 ms. Clavando `HOME` y haciendo clic en `SEL` se simula el combo `HOME+SCL` sin necesitar dos punteros. Así se verificó entrada/salida, las dos pantallas de ejercicio, el dial de calidad de acorde, los botones de acción, el recuento exacto de 20 preguntas, la pantalla de resumen, los LEDs de respuesta correcta (contra las posiciones reales de F3/F4/F5, deducidas de `whites=[0,2,4,6,7,9,11,12,14,16,18,19,21,23,24,26]` en `sdk/Simulator.js`) y la restauración completa del proyecto.

**Fase 3 — Más ejercicios**
- Ejercicios 2, 3, 5, 6, 7, 8 y 9; repetición espaciada; dificultad adaptativa.

**Fase 4 — Modo vamp**

**Fase 5 (opcional) — Hardware y persistencia**
- Compilar firmware con la toolchain JieLi; guardar progreso en flash.

---

## 9. Preguntas abiertas

- ¿Qué combinación de botones usar para entrar en Shed sin romper los atajos existentes?
- ¿Usar pistas existentes temporalmente o reservar una configuración propia?
- ¿Qué voces de fábrica suenan mejor como piano eléctrico y bajo jazz?
- ¿Hace falta un control web extra en el simulador para entrar en el modo?
