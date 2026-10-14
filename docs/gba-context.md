# Context: GBA emulator on Tab5 — unknowns and rules

## Purpose

Port and optimize a Game Boy Advance emulator for the M5Stack Tab5
(ESP32-P4 rev v1.0, 16 MB Flash, 32 MB PSRAM). Emphasis on portability,
measured performance, and preservation of earlier knowledge without assuming
that hardware behavior from a previous project transfers unchanged.

## Current state

- No emulator or GBA-specific design is present in this repository.
- No `firmware/`, no build system, no source tree.
- Task tracking is in beads; durable knowledge is in `bd remember`.
- Reusable Tab5 knowledge is in `docs/` and `.agents/skills/`
  (`tab5-linux-build-deploy`, `esp32-p4-bringup`, `beads`).

## Known unknowns (to investigate)

1. Emulator core: the source port / base implementation under consideration.
   No choice has been recorded in this or the source workspace.
2. License and ABI constraints (core under reuse).
3. Target accuracy/performance split and verification inputs (ROMs, test suites).
4. Display pipeline on Tab5: reuse existing MIPI-DSI path versus alternative
   driver usage. The proven hardware findings are in `docs/tab5-hardware.md`.
5. Input mappings (buttons, screen touches, orientation).
6. Audio output path and clock.
7. Save data / filesystem / FAT layout on SD.
8. Benchmarks and golden outputs.

## Rule

Do not record inferred emulator decisions as facts. If a plausible
alternative exists, record it as a hypothesis and test it before promoting it
to accepted policy in beads. Update `docs/migration.md` whenever an assumption
is replaced.
