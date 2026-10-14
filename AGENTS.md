# GBA_Emu_Tab5 — Agent context

This project ports and optimizes a Game Boy Advance emulator on the M5Stack Tab5
(ESP32-P4). It reuses Tab5 hardware/bring-up knowledge originally accumulated in
`../Tab5`; see `docs/migration.md` for provenance.

## Issue tracking

This repository uses **beads (`bd`)** for all task tracking. Run `bd prime` for
full workflow context.

```bash
bd ready              # Find available work
bd show <id>          # View issue details
bd update <id> --claim  # Claim work atomically
bd close <id>         # Complete work
bd dolt push          # Push beads data to the Dolt remote
bd remember "... "    # Persist project knowledge
```

## Rules

- Use `bd` for all task tracking; do not create markdown TODO lists.
- Run `bd prime` when Beads context is missing or stale.
- Use `bd remember` for persistent knowledge; do not create MEMORY.md files.
- Do not commit or push without the explicit user request or an applicable
  opt-in profile; this repo's automated-session default is conservative.
- `firmware/` is a bench-app skeleton (M1), not yet built or run on
  device. Do not claim device results until M2 measures them.
- ROMs, BIOS images, flash backups, and measurement artifacts are never
  committed. See `.gitignore`.

## Documentation

- `docs/gba-context.md` — purpose and unknowns
- `docs/core-survey.md` — mGBA/gpSP portability and license survey
- `docs/emulator-adapter.md` — thin core boundary (draft)
- `docs/tab5-hardware.md` — Tab5 hardware facts/provenance
- `docs/development.md` — Linux-first environment workflow
- `docs/troubleshooting.md` — proven failure modes
- `docs/performance.md` — performance measurement rules
- `docs/migration.md` — knowledge transfer log

## Source-notes

- Reused Tab5 knowledge is fact-filtered. Older notes may contradict current
  verification; `docs/migration.md` records rejected or corrected statements.
- When a provenance is known, record it as `docs/...: original location, source`.
- No GBA-specific design or emulator core details have been recovered yet.
