# Instrument slots as typed modules: design and plan

Status: approved design, not implemented. Written 2026-10-09 against master `8887975`; every `file:line` below refers to that commit. Read CLAUDE.md first. Work through the stages in section 10 one session at a time, and tick each stage off here when it's committed.

## 1. Goals and non-goals

Goals:
- Instrument types become modules registered in one place, so a new type needs minimal plumbing.
- Every instrument slot starts empty and the user picks a type per slot. Any mix of types can fill the whole range.
- The command popup knows which commands apply to the instrument a row will play.
- Project files saved by upstream LittleGPTracker load unchanged: djdiskmachine/LittleGPTracker and, where practical, Mdashdotdashn/LittleGPTracker (both use the same slot layout).

Non-goals (decided by the owner):
- Projects saved by earlier builds of this fork (including PIGTAIL in slots 0x90-0x9F) need no support. They may break.
- Projects saved by this build don't need to load in upstream or older builds. Nobody will open them there.

## 2. Decisions

| Topic | Decision |
|---|---|
| Slot range | 00-FE (255 slots). 0xFF stays "no instrument" |
| Empty slots | Slots start empty (null). `GetInstrument()` returns a silent per-bank stand-in for empty or out-of-range slots |
| Memory | Allocate instruments on demand |
| Type change UI | Two steps: B,A on the type field clears the slot (with a Yes/No confirmation), then pick a new type |
| Save file | Same name, `lgptsav.dat`, same XML schema. New attribute `LAYOUT="2"` on `INSTRUMENTBANK` (option A) |
| Command popup | Full grid, fixed order; commands that won't do anything for the row's instrument (or in a table vs a phrase) are dimmed, never hidden |
| A,A on an empty phrase instrument cell | Puts in the lowest empty slot number and leaves the slot empty (same semantics as today's "first sample slot without a sample") |
| Compact Instruments | Keeps today's behaviour: unreferenced Sample slots are reset (now: emptied); MIDI and PIGTAIL are left alone |
| New instrument slot choice | Lowest empty slot, no preference for old ranges |
| New MIDI instrument channel | `slot & 0x0F` (matches upstream's 80-8F mapping) |
| Unsupported command at runtime | Ignored, as today. Never reinterpreted, never logged from the audio thread |

PIGTAIL's saved TYPE string (`"Pigtail"`) and shape names only need to stay stable from the first release of the new format. CLAUDE.md's "keep shape names stable" applies from then on, not to fork-era files.

## 3. Where the code assumes a slot range or a type by slot number

| Where | What it assumes today | What changes |
|---|---|---|
| `sources/Application/Model/Song.h:11-21` | Per-type counts, `PIGTAIL_INSTRUMENT_BASE`, `MAX_INSTRUMENT_COUNT` = 0xA0 | 0xFF slots. `MAX_PIGTAILINSTRUMENT_COUNT` and `PIGTAIL_INSTRUMENT_BASE` go. `LEGACY_INSTRUMENT_COUNT` becomes the upstream slot count 0x90 (only used by the registry's legacy rule, if at all) |
| `InstrumentBank.cpp:23-38` | Constructor fills 00-7F Sample, 80-8F MIDI (`SetChannel(i)`), 90-9F PIGTAIL | Slots start empty |
| `InstrumentBank.cpp:44-55` | `AssignDefaults` casts 00-7F to `SampleInstrument` | Create Sample slots only for the samples already in the pool (as today) |
| `InstrumentBank.cpp:63-65` | `GetInstrument` has no bounds check | Never returns null; empty stand-in for empty/out-of-range |
| `InstrumentBank.cpp:58, 69, 205, 267` | Every slot is non-null | Skip empty slots |
| `InstrumentBank.cpp:72, 88` | Save skips `IsEmpty()` slots and entries with no PARAM | Skip null slots; keep the `IsEmpty()` rule; write the entry even if the type has no parameters |
| `InstrumentBank.cpp:128` | An entry with no TYPE is Sample below 0x80, else MIDI | Keep (registry legacy ranges) |
| `InstrumentBank.cpp:130` | IDs 0xA0+ silently dropped | Range 00-FE |
| `InstrumentBank.cpp:14-18, 134-149` | Type-name table and type `switch` | Registry |
| `InstrumentBank.cpp:210-221` | `GetNext` scans only 00-7F and casts to `SampleInstrument` (works on other types only by accident: `FindVariable` returns null) | Lowest empty slot |
| `InstrumentBank.cpp:223-264` | `Clone` targets `GetNext()`, so MIDI/PIGTAIL clones land in sample slots; `default:` creates MIDI; deletes the target without clearing player pointers (existing use-after-free hazard via `lastInstrument_`) | Lowest empty slot, registry factory, safe replace routine |
| `Player.cpp:835-844` | Unchecked `GetInstrument(instr)`; falls back to slot 00 when the channel has no last instrument | Works as-is with the empty stand-in (an empty slot plays nothing, stops the previous note and the table) |
| `PlayerMixer.h:67`, `PlayerChannel.h:28`, `TablePlayback.h:34` | Raw instrument pointers, reset only on project load (`PlayerMixer.cpp:36-38`) | Cleared by the replace routine before an instrument is deleted |
| `InstrumentView.cpp:31-81` | Type `switch`. Lines 48-50 remove the observer from the new instrument instead of the old one | Registry, plus the observer fix |
| `InstrumentView.cpp:303-314` | B+arrows wrap at `MAX_INSTRUMENT_COUNT` | Works with 0xFF (±16 then wraps modulo 255, like phrase and chain numbers) |
| `InstrumentView.cpp:397-420` | B+A purges a Sample when focus is on the *first* field, clears the table on the *last* field | Match by variable ID, because the type field becomes the first field |
| `PhraseView.cpp:178` | Instrument column limit `MAX_INSTRUMENT_COUNT-1` | 0xFE |
| `PhraseView.cpp:833-846, 879-894` | A,A uses `GetNext`; LT+(B,A) uses `Clone` | Lowest empty slot |
| `PhraseView.cpp:1345-1347` | Header calls `GetInstrument(d)->GetName()` unchecked | Shows `--` for an empty slot |
| `PhraseView.cpp` copy/cut/paste (297, 470, 650, 712) | Raw bytes, 0xFF means none | No change |
| `ImportSampleDialog.cpp:133-137` | Assigns only to Sample slots; next target is `GetNext()`, which can be 0x100, so the following import reads past the array | Import into an empty slot creates a Sample there; bounds-checked |
| `FxPrinter.cpp:7-10` | Casts the current slot to Sample | Only reachable from Sample fields; no change |
| `Project.cpp:245-301` | Purge indexes `used[MAX]` with phrase bytes (`NAssert` only), casts Sample by type; `Purge()` is a no-op for MIDI/PIGTAIL | Skip empty slots; emptied Sample slots become null |
| `Project.cpp:363-467` | `LoadFirstGen`, legacy binary loader into 00-8F (`LEGACY_INSTRUMENT_COUNT` at 386) | Delete (see 6.4) |
| `AppWindow.cpp:345-354` | `AssignDefaults` for new projects, then `Init()` on every slot | Post-load step (6.2) goes after `persist->Load()` |
| `SamplePool.h:10` | Pool size = `MAX_SAMPLEINSTRUMENT_COUNT` | Decouple; keep 128 |
| `tools/pt_lab/pt_lab.cpp:580` | Uses `PIGTAIL_INSTRUMENT_BASE` | Create a PIGTAIL in a slot explicitly |
| `README.md:19`, `docs/wiki/What-is-LittlePiggyTracker.md:344`, `docs/wiki/quick_start_guide.md:31` | Types fixed per slot; MIDI clones can't be reverted; 128 slots | Update docs |

Not affected: song and chain views, tables (index-based, global), the phrase save format, `CommandList`.

## 4. Slot count

00-FE, 255 slots.
- 0xFF means "no instrument" everywhere: `PhraseView.cpp:1331` draws `I--`, `Player.cpp:835` skips it, cut writes it, `Project.cpp:228` purges to it.
- `View::updateData` (`View.h:135-147`) turns 0xFF into 0 before stepping, so with a limit of 0xFE editing can't produce 0xFF.
- Phrases and chains already use 00-FE (`PHRASE_COUNT` = `CHAIN_COUNT` = 0xFF).
- No other cap: the saved `ID` is two hex digits, `NO_MORE_INSTRUMENT` is 0x100, the `unsigned char limit` holds 0xFE.
- Side effects: the 128-sample pool cap is tied to the old macro (decouple it); ±16 jumps wrap modulo 255 (F5+16 lands on 06), as phrase and chain numbers already do.

## 5. Design

### 5.1 Registry

One explicit table in a new `sources/Application/Instruments/InstrumentTypes.{h,cpp}` (no self-registering static objects: static-init order and dead-stripping vary by toolchain). C++03 only (CLAUDE.md, compiler constraints).

```cpp
struct InstrumentTypeInfo {
  InstrumentType type;            // existing enum value
  const char *saveName;           // TYPE attribute: "Sample", "Midi", "Pigtail"
  const char *uiName;             // shown in the type field
  I_Instrument *(*create)(int slot);
  int legacyFirst, legacyCount;   // upstream slot range for entries with no TYPE; -1 if none
  const FourCC *commands;         // instrument-level commands it implements, I_CMD_NONE-terminated
  const InstrumentField *fields;  // generic screen layout, or 0 for a custom filler
};
```

Rows: Sample (`"Sample"`, legacy 00-7F), MIDI (`"Midi"`, legacy 80-8F, channel `slot & 0x0F`), PIGTAIL (`"Pigtail"`, no legacy range).

Adding a type later: an enum value in `I_Instrument.h`, the class, one table row, the object in `COMMONFILES` (`projects/Makefile`). With `fields` set, no InstrumentView code is needed. Sample and MIDI keep their existing fill functions, because upstream edits `InstrumentView.cpp` (see section 9). Keep each type's `commands` array in the type's own `.cpp`, next to its `ProcessCommand` switch, so the two don't drift.

### 5.2 Bank

- `I_Instrument *slot_[0xFF]`, null means empty.
- `GetInstrument(i)` returns a per-bank `EmptyInstrument` for empty or out-of-range slots: `Start` returns false, `Render` returns false, name `--`, `GetTable()` returns `VAR_OFF`, no parameters, `ProcessCommand` does nothing. Existing callers can't crash; code that cares calls `IsSlotEmpty(i)`. It is per bank, not static, because views register as observers on whatever `GetInstrument` returns and the bank's lifetime matches the views'.
- `FirstEmpty()`, `Clone(i)` (to the lowest empty slot), `ReferenceCount(i)` (scan of the 4,080 phrase bytes, for the confirmation text).

### 5.3 Replacing a slot's instrument safely

One routine handles every deletion (clear, type change, Compact, Clone's target if ever non-empty):
1. Stop the old instrument on every channel that's playing it (MIDI sends its note-offs from `Stop`).
2. Reset `PlayerMixer::lastInstrument_` entries pointing at it to the bank's empty stand-in (so the slot-00 fallback in `Player.cpp:842-844` doesn't kick in).
3. Stop table playback that it started and clear `TablePlayback::instrument_`.
4. Delete it and install the new one (or null).

Threading: button handlers run under the mixer lock (`AppWindow::onEvent`, `AppWindow.cpp:479-516`) and rendering runs under the same lock (`MixerService::Update`, `MixerService.cpp:129-139`, which calls `AudioOutDriver::Trigger` → `AudioMixer::Render`). SDL mutexes are recursive, so the routine may lock again. Read for the SDL, AudioOutDriver and DummyAudioOut paths only (section 12).

### 5.4 UI

- Instrument screen: the Sample layout already uses rows 4-29, but rows 1-3 are free. A `type:` field goes on row 2. FieldView navigation is by position, so UP from the first field reaches it. Give it its own FourCC (not one used by instrument variables) so `lastFocusID_` works.
- Empty slot: only `type: --`. A+LEFT/RIGHT picks a type, created immediately (nothing is lost).
- Non-empty slot: A+arrows on the type field only notify "B,A to clear first". B,A on the type field opens a Yes/No `MessageBox` such as "Clear I23 (MIDI)? Used in 4 phrases", then empties the slot via 5.3.
- Settings are discarded on clear, not copied by name: "volume" means unity at 0x80 for Sample, −12 dB at 0x40 for PIGTAIL, and velocity for MIDI. The sample stays in the pool. The table stays; only the assignment is lost.
- Phrases keep the slot number, so they play whatever the slot now holds. An empty slot behaves like today's sample slot without a sample: it stops the previous note and table, and following notes without an instrument number stay silent.
- Phrase header shows `I23: --` for an empty slot; R+RIGHT on it opens the type picker.
- Sample import into an empty slot creates a Sample there. Into a non-Sample slot: notify (the sample still goes to the pool, as today).

## 6. Persistence

### 6.1 What an upstream project file contains, and where each part goes

Both upstreams use 00-7F Sample, 80-8F MIDI, `MAX_INSTRUMENT_COUNT` = 0x90, write a TYPE on every entry and always write all MIDI entries (`MidiInstrument::IsEmpty()` returns false since the 2014 import `c712f0a`). Mdashdotdashn's files say `VERSION="1.3"`; djdiskmachine's say `"1"`.

| In the file | Slot-dependent? | Maps to |
|---|---|---|
| `INSTRUMENT ID="hh"`, 00-8F | Yes | Same slot number. No renumbering |
| `TYPE="Sample"` / `"Midi"` | Yes | Registry lookup by save name |
| Entry with no TYPE (older than 2014) | Yes, by ID | Registry legacy ranges: Sample 00-7F, MIDI 80-8F |
| Sample slot without a sample (never written) | Implicit | Empty slot: silent, as upstream's sampleless sample |
| MIDI entries (always written, including `channel`) | Implicit default | Created from the entry |
| Phrase references 80-8F but the file has no entry | Upstream default: MIDI on channel slot-0x80 | Safety net (6.2) |
| `PARAM NAME= VALUE=` | No | Restored by name; the `filter dist` rename and the VERSION < 38 / < 130 fixes stay |
| `SONG` → `INSTRUMENTS` hex buffer, 00-8F or FF | Yes | Unchanged. A byte 0x90+ (only possible by corruption) points at an empty slot and is silent; upstream would read out of bounds |
| Sample `sample` value: a file name in the pool | No (128-sample cap) | Keep the cap at 128, decoupled from the slot macros |
| `PROJECT`, `TABLES`, `GROOVES`, `MIXER` | No | No change |

### 6.2 Load rules

1. Each `INSTRUMENT` entry creates its TYPE through the registry (entries with no TYPE: legacy ranges). Unknown TYPE: log an error, slot stays empty, params skipped.
2. Only for files without `LAYOUT="2"`, after the whole file has loaded: any slot in 80-8F that a phrase references but that has no entry gets a MIDI instrument on channel slot-0x80. This reproduces upstream exactly; it can only matter for files older than 2014.
3. Rule 2 runs in `AppWindow::LoadProject` after `persist->Load()`, not inside `INSTRUMENTBANK`'s restore: top-level node order varies between saves (some files start with `TABLES`, others with `PROJECT`).
4. Then `Init()` on non-empty slots.

### 6.3 Save rules

- Same file (`lgptsav.dat`), same schema. Empty slots aren't written. `IsEmpty()` still skips a Sample with no sample.
- `INSTRUMENTBANK` gets `LAYOUT="2"`. It turns rule 2 off for our own files, so a deliberately empty, referenced slot in 80-8F doesn't come back as MIDI.
- Write the entry even when a type has no parameters (`InstrumentBank.cpp:88` currently drops it).
- Projects saved by this build are not expected to load in upstream (non-goal). For the record, an upstream build would hit its unknown-TYPE use-after-free on a PIGTAIL entry in 00-8F, drop slots 0x90+ and read out of bounds when they're referenced.

### 6.4 Legacy binary loader

`Project::LoadFirstGen` (`Project.cpp:363-467`) has no caller in this repo, in djdiskmachine master, in the 2014 import, or in Mdashdotdashn's `AppWindow.cpp`. The binary files share the `lgptsav.dat` name and `PersistencyService::Load` would treat them as compressed XML, so no upstream build since 2014 can load them either. Delete it. (If first-generation files ever turn up: it read 3 ints per slot for 0x90 slots, into the first three variables in declaration order: Sample `sample, volume, interpol`; MIDI `channel, note length, volume`.)

## 7. Memory

x86-64 is measured against the X64 objects; 32-bit ARM is derived (Appendix A).

| | x86-64 (measured) | 32-bit ARM (derived) |
|---|---|---|
| Sample: sizeof / per slot including heap | 3,840 B / 11.0 KiB (67 blocks) | 3,076 B / 8.0 KiB |
| MIDI | 280 B / 1.8 KiB | 216 B / 1.3 KiB |
| PIGTAIL | 168 B / 1.0 KiB | 116 B / 0.8 KiB |
| Today's bank (0x80 Sample + 0x10 MIDI + 0x10 PIGTAIL) | 1.42 MiB | 1.04 MiB |
| 255 slots preallocated, all Sample | 2.74 MiB | 2.00 MiB |
| On demand: pointer table only | 2 KiB | 1 KiB |
| On demand: 32 Sample + 8 MIDI + 8 PIGTAIL | ≈ 375 KiB | ≈ 274 KiB |

Sample's cost is mostly 26 `Variable` objects (144-184 B each, mostly `std::string` members) plus 8 per-channel render states. Static state is independent of slots and unchanged: Sample feedback buffers 219 KiB, PIGTAIL voices about 135 KiB.

On demand wins: preallocation can't know the type, so it either doubles today's footprint or still reallocates on a type change. Allocation only happens in UI actions and on load, under the existing lock, never in the audio callback.

## 8. Commands

### 8.1 Today

- Popup: A+DOWN or A+UP in a command column calls `enterCommandSelector` (`PhraseView.cpp:132`; `TableView.cpp:530`). `CommandSelectorModal` lays out `CommandList` without `----` in a 5-column grid (27 commands, 6 rows; room for 80 cells before it outgrows the 16-row area). Moving writes the command into the cell live and auditions it; A confirms, B restores. Help text: `HelpLegend.h`, one text per command.
- IDs: `MAKE_FOURCC` in `CommandList.h:7-37`; the 4-character name is the FourCC. Order: `_all[]` in `CommandList.cpp:58`. Saved as raw bytes, so unknown FourCCs load harmlessly.

| Where | Code | Commands |
|---|---|---|
| Phrase, before the note | `updatePhrasePos` (`Player.cpp:793`) | DLAY |
| Phrase, on step advance | `getChannelHop` | HOP jump |
| Phrase | `ProcessChannelCommand` (`Player.cpp:634`) | KILL, TMPO, TABL consumed; GROV, HOP, STOP acted on and also passed to the instrument |
| Phrase, everything else | Instrument currently sounding on the channel; skipped if none | |
| Table | `ProcessLocalCommand` (`TablePlayback.cpp:101`) | HOP, KILL, IRTG, GROV; then all three columns go to the instrument that started the table |
| Sample (15) | `SampleInstrument.cpp:1086` | LPOF PLOF ARPG VOLM PAN FCUT FRES FBMX FBTN PTCH LEGA PFIN RTRG FLTR CRSH; ignores everything without a sample |
| MIDI (6) | `MidiInstrument.cpp:161` | RTRG MVEL VOLM MCHD MDCC MDPG; ignores everything while muted |
| PIGTAIL | stub | none |

- TMPO, TABL, STOP and DLAY do nothing in tables. IRTG does nothing in phrases.
- Interpreted differently by type: VOLM (Sample ramps to `bb` at speed `aa`; MIDI sends CC7 = param/2 immediately, and with `aa` ≠ 00 that exceeds 127, an invalid MIDI data byte) and RTRG (Sample retriggers every `bb` ticks offset by `aa`; MIDI re-sends held notes every `bb` ticks). One help text each, written for Sample.

### 8.2 Design

Instrument for a row (mirrors the player: only a row with a note triggers):
1. Scan upward from the row in the phrase; take the first row with a note *and* an instrument number. A note without one means keep looking further up.
2. Then earlier phrases in the chain (via `viewData_->chainRow_`), then earlier chains in the same song column. At most about 65k byte reads, only when the popup opens.
3. Otherwise unknown: dim nothing, help line says the instrument is unknown.
4. Table view: the instrument the user came from (`VT_TABLE2`), or the phrase row with the TABL command (`VT_TABLE`, passed from PhraseView); from the mixer: unknown.

Ignores HOP and live mode on purpose; it is a hint, not a simulation.

Popup (option A of three considered): full grid, fixed order; dim commands not available for (location phrase/table, resolved type); type-specific help text when the registry provides one (MIDI's VOLM and RTRG). Player-level commands carry location flags: KILL, GROV, HOP in both; TMPO, TABL, STOP, DLAY phrase only; IRTG table only. Dim colour: reuse an existing dim colour first; add a config colour only if none reads well. Rejected: B (filter to relevant commands; cells move, a wrong guess hides commands) and C (relevance ordering; order still changes). Optional later: dim commands in the phrase grid itself.

Keep `CommandList.{h,cpp}` and `HelpLegend.h` structurally unchanged so upstream's command additions merge as-is (plus one line in a type's `commands` array).

Runtime stays as today: unsupported commands are ignored. Natural follow-up: PIGTAIL implements VOLM, PTCH, LEGA, PFIN, ARPG, RTRG.

## 9. Upstream merge cost

- Structural core lives in files upstream hasn't touched since its 2014 import: `InstrumentBank.*`, `Song.h`, `TablePlayback.*`.
- Small edits land in files upstream changes often (commits since 2025): PhraseView 9, Project.cpp 7, TableView 5, AppWindow 5, InstrumentView 4, Player 4, CommandList 2. Expect small mechanical conflicts. Open upstream PR djdiskmachine/LittleGPTracker#266 (Libav) touches InstrumentView and SampleInstrument.
- On every upstream merge, check for new code that treats slots below 0x80 as Samples, casts by slot, or adds instrument types (`grep -n "GetInstrument(\|MAX_.*INSTRUMENT_COUNT\|IT_"`).
- Good upstream candidates on their own: stage 1 fixes and 2035c45 (unknown-TYPE use-after-free).

## 10. Stages

One stage per session; commit at each working stage (CLAUDE.md workflow rules). Tick here when done.

| Done | Stage | Change | How to verify |
|---|---|---|---|
| [ ] | 0 | Tooling: `tools/slot_lab` harness (Appendix A methods): dump slot → type → parameters and the slots phrases reference; load, save, diff. Build upstream/master and `c712f0a` (stand-in for Mdashdotdashn) in a scratch export, with a slot_lab variant linked against upstream's objects. Fixtures in `tools/slot_lab/fixtures/` (section 11) | Baseline dumps of every fixture from upstream's loader |
| [ ] | 1 | Safety only, no behaviour change: bounds-checked `GetInstrument` with the empty stand-in, the replace routine (5.3) used by Clone, the InstrumentView observer fix | ASan run with phrase bytes A0-FE and clone-after-play; fixture dumps unchanged; owner plays a project |
| [ ] | 2 | Registry refactor, no behaviour change: replace `InstrumentTypeData` and the three type switches and the constructor ranges | Warning-free build; dumps and saved XML byte-identical to stage 1; `pt_lab` ident, init, audit unchanged |
| [ ] | 3 | Empty slots, 00-FE, load/save rules (6.2, 6.3), A,A, Clone, Import, header, Compact, `LoadFirstGen` deleted, pool cap decoupled, pt_lab updated | Every upstream fixture loads to the same dump as upstream's loader, except sampleless unreferenced Sample slots, which are now empty; saving an unchanged fixture reproduces it apart from top-level node order and `LAYOUT="2"`; memory probe; owner loads upstream projects and listens; A,A, clone and import flows |
| [ ] | 4 | Type field and clear-with-confirmation through 5.3 | ASan stress that clears and changes playing slots, if slot_lab can drive the player headlessly (otherwise an ASan build of the app while the owner changes types during playback); MIDI monitor shows no stuck notes |
| [ ] | 5 | Command lists in the registry, row resolver, popup dimming, table context | Resolver test cases in slot_lab; owner checks the popup |
| [ ] | 6 (optional) | PIGTAIL screen via `fields`; keep unknown types' XML on save; docs (section 3 list), CLAUDE.md, README | Same tooling |

Riskiest: stage 4. It is the only step that frees objects the audio path holds raw pointers to; mistakes show up as rare crashes during playback, and ASan doesn't run on handhelds. Mitigations: the single replace routine, the existing lock, the stress test.

## 11. Fixtures

This repo has no upstream example projects (only a `Kick5.WAV` in an old `lgpt_ALPHA` packaging folder, no save file). The owner's `~/lgpt-run` projects are fork-era and are not fixtures.

**djdiskmachine projects:** https://github.com/djdiskmachine/lgpt-resources (pin commit `9624b43`, 2026-05-04; about 64 MB, mostly samples). Clone it outside this repo and point slot_lab at it; don't commit it here. Six projects, each with `samples/`, all plain XML, `VERSION="1"`, with a `MIXER` node:

| Project | Entries | Slots phrases use |
|---|---|---|
| `lgpt_1_l3ven` | 16 Sample, 16 MIDI | 16 |
| `lgpt_BA0BAB_D1BBB5` | 9 Sample, 16 MIDI | 9 |
| `lgpt_Bootloop-FastJump` | 32 Sample, 16 MIDI | 32 |
| `lgpt_Float-Fi-WildDope` | 14 Sample, 16 MIDI | 14 |
| `lgpt_Luce-COMPSENTRY` | 12 Sample, 16 MIDI | 7 (5 Sample slots unused) |
| `lgpt_djdiskmachine-SereneDream` | 23 Sample, 16 MIDI | 16 (7 unused) |

All Sample entries are in 00-7F and all MIDI entries in 80-8F. No phrase references a MIDI slot, and every referenced slot has an entry, so the 80-8F safety net (6.2 step 2) isn't exercised.

Still missing, to ask the owner for or hand-craft:
1. A project that plays MIDI instruments, including a MIDI clone in 00-7F.
2. Mdashdotdashn 1.3 saves (`VERSION="1.3"`) and anything older (entries with no TYPE).
3. A compressed (LZ) save, if any build wrote one.
4. Any first-generation binary saves, if they exist.

Generated: projects built and saved through upstream's own save code (slot_lab linked against upstream objects). Hand-crafted and labelled: entries with no TYPE, VERSION below 38 and below 130, phrase bytes 0x90+.

## 12. Not verified

- Upstream load behaviour (including its unknown-TYPE use-after-free) is from reading its code; upstream hasn't been built here. The one load test run so far used this fork's loader.
- Mdashdotdashn's code: four files read through the GitHub API, not built.
- Files older than 2014: whether MIDI entries were ever missing (the only reason for rule 6.2 step 2). Compressed saves: which builds wrote them; untested.
- 32-bit memory numbers are derived, not measured on hardware. Bittboy (musl), Dingoo and RS-97 (uClibc, MIPS) and Caanoo (old GCC strings) may differ by about ±20%.
- The GUI hasn't been run for any of this; UI statements are from reading the code.
- Mixer lock coverage on the JACK, PSP and W32 audio paths.
- Upstream's plans and whether they'd take any of this.

## Appendix A: how the numbers were measured

- Heap cost: a scratch program linked against `projects/buildX64/*.o` minus `LINUXMain.o` (same flags and libs as `tools/pt_lab/Makefile`), replacing global `operator new`/`delete` to count allocations and track live blocks, plus `mallinfo2()` deltas over 64 instances per type. Install `LINUXSystem`, `UnixFileSystem` and a test `Audio` as pt_lab does.
- 32-bit layouts: compile the real headers with `g++ -m32 -malign-double -S` (an ARM EABI layout proxy) and read `sizeof` constants from the assembly. No 32-bit libs are needed: add `-isystem /usr/include/x86_64-linux-gnu/c++/13 -isystem /usr/include/x86_64-linux-gnu` and an empty `gnu/stubs-32.h`. The `-m64` run of the same file matched the runtime measurement exactly. Heap blocks were converted to 32-bit with glibc's 8-byte chunk rounding.
- Loading a project in a harness: call `PersistencyService::GetInstance()` before `new Project()` (Persistent objects register with the service when constructed), install a MIDI service (`RTMidiService`) and `UnixProcessFactory` first, and set the `project` path alias.

## Appendix B: unrelated bugs found while surveying

- `getHelpLegend` (`HelpLegend.h:8`) returns `new std::string[3]` that no caller frees: leaks on every redraw with the cursor on a command and in the popup.
- `Player::updatePhrasePos` (`Player.cpp:801`): DLAY in command column 2 reads PARAM1.
- MIDI VOLM with `aa` ≠ 00 sends CC values above 127.
