# PIGTAIL phrase commands: TIMB, COLR, VOLM

Status: approved by the owner 2026-10-16. Stage A (section 8) done and committed on `feat/pigtail-commands`; stage B not started. Tick each stage here when it's committed. Written 2026-10-16 against master `bd30d84`; every `file:line` below refers to that commit. Apart from what stage A records, nothing here is built or run yet: every other claim about today's behaviour comes from reading the code. Read CLAUDE.md first.

## 1. Goal

Let a phrase (and a table started with TABL) move PIGTAIL's timbre, color and volume while it plays, using the same `aabb` form as the sampler's VOLM, FCUT and FRES: target `bb`, speed `aa`.

These commands are also the groundwork for an envelope (section 9). The envelope will move the same per-voice values, so they're built once, here.

Non-goals:
- Emulating Braids hardware controls that the imported code doesn't have (FM, the modulation attenuverters, bit crusher, and so on). Dropped by the owner on 2026-10-16.
- An envelope or release stage. That's a later spec.
- PIGTAIL's own instrument table (its `table` field). `GetTable()` still returns `VAR_OFF`.
- Loading in upstream builds. Saves from this build don't have to load there (non-goal in `docs/design/instrument-slots.md` section 1).

## 2. The commands

| Command | `bb` (target) | `aa` (speed) | Same scale as the screen field |
|---|---|---|---|
| `TIMB aabb` | timbre 00-FF | 00 = at once, otherwise a ramp of `aa` steps (below) | `timbre` |
| `COLR aabb` | color 00-FF | as above | `color` |
| `VOLM aabb` | volume 00-FF | as above | `volume` |

- **Target.** `bb` is absolute, on the same scale as the instrument screen field. `TIMB 0040` sounds exactly like setting the screen's `timbre` to 40.
- **Start point.** A ramp starts from the voice's current value, which may be mid-ramp from an earlier command, and goes to `bb`.
- **Duration.** The ramp lasts `aa × 4` ticks whatever the distance, the same formula as the sampler's VOLM (`SampleInstrument.cpp:1190-1205`: `4*GetTickSampleCount()` samples per unit of `aa`). At the default groove a phrase row is 6 ticks. At 120 BPM a tick is about 21 ms, so `aa=01` takes about 83 ms and `aa=10` about 1.3 s, which is about 10.7 rows.
- **Instant.** A command with `aa=00` lands within one Braids block (24 samples, about 0.5 ms at 44.1 kHz).
- **VOLM.** VOLM is an existing command, and on PIGTAIL rows it means what it means for the sampler. MIDI rows keep their own VOLM (CC7, `MidiInstrument.cpp:170`).
- **Names.** `TIMB` and `COLR` are new FourCCs. Neither `upstream/master` nor `upstream/1.6.0-candidate` uses them. The instrument variables' IDs (`PTIP_TIMBRE` = `TMBR`, `PTIP_COLOR` = `COLR`) are a separate namespace, so reusing `COLR` there is harmless.

## 3. When a value is reset

This follows the sampler's split between a clean start and a note without an instrument number.

- **What flags&1 means.** `Start()`'s `flags & 1` is set when the row has an instrument number. `Player.cpp:883` passes `newInstrument`; the table's IRTG retrigger at `Player.cpp:560` passes `false`. CLAUDE.md calls it "retrigger", but it is the sampler's `cleanstart` (`SampleInstrument.cpp:170`).
- **Clean start resets.** On a clean start, the voice drops every command value and every ramp. The screen fields are back in control.
- **A note without an instrument number keeps them.** The command values and any running ramp carry over. This is how the sampler treats FCUT and FRES (reset only under `cleanstart`, `SampleInstrument.cpp:340-377`) and its VOLM ramp (cleared only there). It lets a sweep run across several notes.
- **Stop and KILL don't reset.** The next note decides.

## 4. Screen knobs vs commands

Today `Render()` reads `timbre_`, `color_` and `volume_` on every call, so screen edits are heard at once, even during playback (owner-confirmed).

- **No command yet.** Until a command touches a parameter on a voice, that voice keeps reading the screen field live. Nothing changes from today.
- **First command.** The first TIMB, COLR or VOLM on a voice copies the field's current value as the ramp's start point. From then on that voice ignores that field.
- **Back to the screen.** The field takes over again at the voice's next clean start.
- **Per parameter.** TIMB doesn't freeze color or volume.

## 5. Where commands arrive, and what that means

From `Player::Update` (`Player.cpp:479`), one tick runs in this order:
1. `moveToNextStep` and `updatePhrasePos` (`:795`) set `timeToStart_`.
2. `playCursorPosition` (`:824`) starts the row's note.
3. `ProcessCommands` (`:583`) sends the row's commands to `mixer_->GetInstrument(channel)`.

Consequences:
- **Same row as a note.** A note and a command on one row work: the note starts (and resets on a clean start) first, then the command applies to it.
- **Channel not playing.** A command on a row with no note goes to the instrument the channel is playing. If the channel isn't playing (after KILL, or before any note), `GetInstrument` is null and the command is dropped. The sampler loses commands the same way.
- **DLAY.** With DLAY in the other column, the command fires before the delayed note. It goes to whatever the channel played before. If the delayed note has an instrument number, its clean start then wipes the command. The sampler's FCUT behaves the same. This is accepted and not fixed here.
- **Tables.** TABL on a PIGTAIL row already starts a table on that channel (`Player.cpp:667-673`). TablePlayback passes each table command to the instrument (`TablePlayback.cpp:197-199`), so tables of TIMB, COLR and VOLM should work with no table code. This is from reading only; section 8 tests it.
- **Muted channels.** A muted channel still processes commands, so its state is right when it's unmuted.

## 6. Implementation

### 6.1 Shared code
- `CommandList.h`: add `I_CMD_TIMB` (`'T','I','M','B'`) and `I_CMD_COLR` (`'C','O','L','R'`).
- `CommandList.cpp:3` `_all[]`: insert them in alphabetical order (COLR before CRSH, TIMB between TABL and TMPO), since `GetNextAlpha`/`GetPrevAlpha` assume that order.
  - The popup goes from 27 to 29 commands. That's still 6 rows of 5 (`CommandSelectorCommon.h:9`), so it doesn't grow.
- `Utils/HelpLegend.h`: add legend lines for TIMB and COLR in the style of FCUT, e.g. `TIMBre:aabb` / `approach timbre` / `bb at speed aa`.
  - They go there, not in a PIGTAIL `Help[]`, because they have no other meaning. A `Help[]` is for a type that reads a shared command differently (CLAUDE.md, stage 5).
  - VOLM's existing legend already describes PIGTAIL's VOLM.
- No change to `Player`, `PlayerChannel`, `TablePlayback` or saving. Phrases and tables already store any FourCC as raw bytes (`Song.cpp:37-39`).

### 6.2 PigtailInstrument
- **Per-voice state.** Add to `PigtailVoice` (`PigtailInstrument.cpp:43`) one modulated value per parameter: `{ bool active; int32 current; int32 target; int32 step; }`.
  - Values are Q16 on the field's scale: field `v` is `v << 16`, so 0x00-0xFF becomes 0 to 0xFF0000.
  - Q16 matters for slow ramps. The slowest ramp, `aa=FF`, is about 1020 ticks, or about 39 000 blocks at 120 BPM and 44.1 kHz. A one-step change spread over that still has a non-zero step.
- **`ProcessCommand(channel, cc, value)`.** For `I_CMD_TIMB`, `I_CMD_COLR` and `I_CMD_VOLM`:
  - If the value isn't active, set `current` from the screen field and set `active`.
  - Set `target = (value & 0xFF) << 16`.
  - Compute `step = (target - current) / blocks`, where `blocks = max(1, aa*4*GetTickSampleCount() / BRAIDS_BLOCK_SIZE)`. If the step comes out as 0 while `target != current`, use ±1.
  - With `aa=00`, `current = target` at once.
- **`Start()`.** On a clean start (`flags & 1`, next to the existing `Strike()` at `:167`), clear `active` on all three values.
- **Advance once per Braids block, before `renderBlock()`.** Add `step` to `current` and stop at `target`. Then:
  - timbre and color go to Braids as `current >> 9`. That equals today's `v << 7` for whole values and is finer between them.
  - volume is `current >> 16`, so the multiply in `Render()`'s copy loop stays at today's 8-bit width and no new overflow is possible.
  - An inactive value reads the screen field exactly as today.
- **`Commands[]`** (`:215`): `I_CMD_COLR, I_CMD_TIMB, I_CMD_VOLM, I_CMD_NONE`.
- **Threading.** The same thread context as `SampleInstrument::ProcessCommand`, which also writes per-voice render state. No new locking.

### 6.3 Unchanged behaviour to keep
- With no TIMB, COLR or VOLM in a song, the output is bit-identical to today's: an inactive value is read exactly as now.
- At 96 kHz, Braids itself is untouched, so `make ident` stays as it is.

## 7. Tests that must change
- `tools/slot_lab/slot_lab.cpp`, resolve scenario:
  - `:1517`: 7 lit becomes 10 for a PIGTAIL phrase row.
  - `:1522`: 4 lit becomes 7 for a PIGTAIL table.
  - `:1533`: expects VOLM to be "not for I05 PIGTAIL"; switch it to a command PIGTAIL still lacks, e.g. FCUT.
  - The Sample and MIDI lit counts are unchanged: TIMB and COLR are dimmed for them, so only the total goes up.
  - The unknown-slot counts each go up by 2 (stage A): with no slot known, every command not tied to the other location stays lit, TIMB and COLR included. `:1519` and `:1525` become 28 for phrases, `:1524` 25 for tables.
- `tools/slot_lab` `make cmdlists`: PIGTAIL's list must match its new `case` labels. Its entry is already in `CHECKS`.

## 8. Verification
1. Build X64 (`-Wall`, no new warnings). `make cmdlists`, then slot_lab `make check` (saves unchanged), `make compare` and `make play` (ASan + UBSan) with section 7's new expectations.
2. pt_lab: add `make cmds`, driving `PigtailInstrument` directly:
   - `TIMB 00xx`, `COLR 00xx` and `VOLM 00xx` give output bit-identical to the screen field set to `xx`, for a few shapes and seeds.
   - A ramp reaches its target within one block of `aa × 4` ticks.
   - A note without an instrument number keeps the values; a clean start drops them.
   - With no commands, output is bit-identical to the build before the change.
   - Run it under `make stress`'s sanitizers too.
3. gui_lab: a screenshot of the popup on a PIGTAIL row showing TIMB, COLR and VOLM lit, with their legend lines. Then the same on a Sample row with TIMB and COLR dimmed.
4. Owner, by ear, on the desktop build and then on the Brick:
   - a TIMB sweep and a COLR sweep on CSAW and on two other shapes;
   - a VOLM fade-in from volume 00;
   - a sweep across notes without instrument numbers;
   - a table of TIMB steps started with TABL.

Stages, each committed when it passes:
- [x] **A.** The FourCCs, `_all[]` and the legends. The popup shows TIMB and COLR, dimmed everywhere except where the slot is unknown. slot_lab passes with its total and its unknown-slot counts changed (section 7).
  - Done 2026-10-16. X64 build: no warnings in the changed files. slot_lab: `make cmdlists` all match; `make check` same as `baseline/fork` for every fixture (the one "loads back differently" line is the known `hand-midi-notype`); `make compare` differs only in `hand-notype`, as expected; `make play` passes with no sanitizer reports, with the three unknown-slot counts updated.
  - gui_lab, on a copy of `up-basic`, phrase 00 row 00 (I00, Sample): the popup still has 6 rows (29 commands), COLR and TIMB dimmed. Selecting each shows its legend (`COLoR:aabb` / `approach color`, `TIMBre:aabb` / `approach timbre`, then `bb at speed aa`) and the status line `not for I00 SAMPLE`.
  - Not run: a PIGTAIL row (TIMB and COLR stay dimmed there until stage B) and the owner's GUI check.
- [ ] **B.** PIGTAIL's state, `ProcessCommand`, `Commands[]`, section 7's expectations, pt_lab `make cmds`, and then the owner's listening test.
- **Afterwards.** Update CLAUDE.md: the PIGTAIL "stubs" line, and `flags & 1` described as "instrument number on the row".

## 9. Later: the envelope
The envelope adds its output to the same per-voice values, which become `current + envelope`, clamped. That means it needs only new per-voice state and new parameters, with no new plumbing. A release stage needs shared-code work and gets its own spec: `PlayerChannel::StopInstrument` (`PlayerChannel.cpp:41`) drops the instrument right after `Stop()`, so it can't render a tail, and `ForgetInstrument` relies on that stop being a hard cut.

## 10. Owner's decisions (2026-10-16)
- Commands work like the sampler's: VOLM's `aa × 4` ticks timing (section 2), and FCUT/FRES's reset rule (section 3).
- Values carry across notes without an instrument number; a clean start resets them (section 3).
- VOLM is included on PIGTAIL.
- The names are `TIMB` and `COLR`.
