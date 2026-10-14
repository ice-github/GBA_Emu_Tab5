# Knowledge migration log — Tab5 → GBA_Emu_Tab5

## Scope

This repository was created to separate an emulator project from `../Tab5`, which
carried media-player work. Reusable Tab5 knowledge was selected and transformed.
Only documentation and Beads state were migrated in this step; no firmware or
tool was copied.

## Sources considered

- `../Tab5/AGENTS.md`, `agents.md`, `CLAUDE.md`
- `../Tab5/.agents/skills/`:
  - `beads/SKILL.md`
  - `esp32-p4-bringup/SKILL.md`
  - `tab5-linux-build-deploy/SKILL.md`
  - `tab5-media-bundle/SKILL.md`
- `../Tab5/firmware/main/main.c`, `ina_diag.c`
- `../Tab5/firmware/sdkconfig.defaults`
- `../Tab5/firmware/main/idf_component.yml`
- `../Tab5/.beads` issue/-memory store (read via `bd --readonly --sandbox`)

## Search for GBA material

The following searches were performed without matches:
- `grep -RniE "gba|game ?boy|mGBA|gpsp"` in `../Tab5` (incl. hidden files, excluding `.git`).
- `git log --all -G 'GBA|gba|...|エミュレータ' -- .` in `../Tab5`.
- Beads full issue history (100+ issues) by title/description.
- `bd memories` (16 prefixes) search.
- `git log --all --oneline` for beams with GBA wording.

Conclusion: no GBA-specific design notes, plans, tasks, or history were found. A
follow-up recovery task is left open in Beads to search chat exports or other
locations.

## Transferred as facts

- CPU/revision support for Tab5 display, IO, peripherals, SD, backlight PWM,
  and boot verification.
- Linux-first dev flow, allowlist permissions, backup procedure, `sdkconfig`
  verification rules.
- Obligatory USB-Serial-JTAG usage rules (blocking/unblocking semantics, LF-RX).
- Build verification gates and performance metrics policy.

## Transferred as deprecated/corrected

- Older notes said "IDF v5.4 requires rev1"; prior firmware subsequently used
  5.5.4 with `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` and rev 1.0 set. The newer
  explanation is the one used here.
- Touch/LCD rotation per earlier task was replaced by a mirroring/touch
  transformation report in later work. Keep the latest documentation of the
  source code as authoritative.
- Tab5 video decode / 30fps is a product of that project, not of an emulator.

## Explicitly not transferred

- Player firmware and media pipeline (`main.c`, send tools, JPEG/A/V sync).
- Media generation and rotation tools do not transfer.
- Video, bundle, frames, ROMs, or device backup binaries.
- Old beads database, repository ID, remote, issue graph, tasks for the player.
- Toolchain custom directories.
- Various tooling/config for unsupported editors.

## Notes on extracted lessons

- Beads KD funds via `bd remember` are re-created selectively in this project.
  Some prior bullets, such as "`TAB5_BL_PWM_FIX`", are referenced directly in
  the prior flash image/temporary logs; some of that may have been removed from
  the final firmware, so the reported behavior's persistence is a property of
  that source revision.
- All transferred constraints should cite original location and explored
  revision (`../Tab5` at `13fd862` or the current at migration time).
- Any new assumption should be annotated and then either validated or removed.
  No GBA design is claimed here yet.
