# CLAUDE.md

Fork of djdiskmachine/LittleGPTracker (`upstream` remote) adding a Braids-based synth instrument, PIGTAIL. General docs: `docs/`, `README.md`.
Items marked (unverified) were not confirmed in the repo or by running them.

## Build and run (X64, Linux)
- Run from `projects/` (the Makefile uses `$(PWD)`): `cd projects && make PLATFORM=X64`. Full build ~25s; objects go to `projects/buildX64/`, binary to `projects/lgpt.x64` (both gitignored).
- `make` runs `python3 ../sources/Resources/mkfont.py` (needs Pillow; 10.2.0 is installed here). SDL2, ALSA and JACK dev packages via pkg-config are needed.
- `bin:` resolves to the directory of the executable (`LINUXSystem.cpp:74`). `config.xml` and `mapping.xml` are read from there (`Config.cpp:6`, `Application.cpp:54`). `projects/resources/X64/` holds `config.xml` and `mapping.xml`; presumably copy them next to the binary (unverified, the binary was never run in this session).
- Default keys (`projects/resources/X64/mapping.xml`): arrows = d-pad, `s` = A, `a` = B, `space` = start, `q` = left shoulder, `w` = right shoulder. `config.xml` can override with `KEY_*` (see `docs/LittlePiggyTrackerConf.md`); `DUMPEVENT` is on in the shipped config.

## Compiler constraints
- `projects/Makefile.X64`: `CXXFLAGS` has `-std=gnu++03`, so no `nullptr`, `auto`, range-for, `<cstdint>`/`std::` C++11 features, `override`, brace-init. Braids uses `stmlib.h` typedefs (`uint16_t` etc.) instead.
- `Makefile.X64` sets `OPT_FLAGS := -O3` then overrides it with `-g`. Default builds are unoptimised, so CPU/timing numbers from them are not representative.
- No `-fwrapv` or other special flags exist for the Braids objects; the only Braids flag is `CXXFLAGS += -I.../sources/Externals/Braids` in `projects/Makefile`.

## Adding or changing an instrument (checklist, from the PIGTAIL commits cb802a4, 1674229)
- Type enum: `sources/Application/Instruments/I_Instrument.h` (`IT_*`, before `IT_LAST`).
- Class: `sources/Application/Instruments/<Name>Instrument.{h,cpp}` implementing every pure virtual in `I_Instrument`.
- Slots: `sources/Application/Model/Song.h` (`MAX_*INSTRUMENT_COUNT`, `*_INSTRUMENT_BASE`, `MAX_INSTRUMENT_COUNT`); constructed in `InstrumentBank.cpp` constructor.
- Save/load: `InstrumentBank.cpp` `RestoreContent` (type string table at the top, `switch (it)`) and the copy switch near line 242 (its `default` falls back to MIDI); `Project.cpp:386` reads the legacy binary format using `LEGACY_INSTRUMENT_COUNT`.
- View: `sources/Application/Views/InstrumentView.cpp` (`switch` on type, add `fill<Name>Parameters()`; declare in `InstrumentView.h`).
- Build: add `<Name>Instrument.o` to the object list in `projects/Makefile` (~line 268).
- Upstream-only: `projects/lgpt.vcxproj`, `lgpt.vcxproj.filters`, `lgpt.vcproj`, `lgptest.dev`, `lgpt.xcodeproj` and `lgpt64.xcodeproj` list MidiInstrument but not PigtailInstrument. Update them when upstreaming; X64 builds don't need them. `CONTRIBUTING.md` also asks for a `BUILD_COUNT` bump in `Model/Project.h`, a `CHANGELOG` entry and `docs/wiki/What-is-LittlePiggyTracker.md`.
- Render contract (`I_Instrument.h`, `PlayerChannel.cpp:48`): `Render(channel, fixed *buffer, size, flags)` fills `size` interleaved stereo Q15 frames (`buffer[2*size]`), and `PlayerChannel` only processes/mixes the buffer when it returns true. Overwrite the buffer, don't accumulate (inferred from PIGTAIL; unverified for sample instruments). Voice state is per song channel (PIGTAIL keeps a static `voices_[SONG_CHANNEL_COUNT]`), since a channel plays one instrument at a time.

## PIGTAIL (`PigtailInstrument.{h,cpp}`)
- Slots `0x90-0x9F` (`PIGTAIL_INSTRUMENT_BASE`, 16 slots). Type string `"Pigtail"`.
- Variables, saved by FourCC through the normal Variable save path: `SHAP` (list `shape`, saved by name, order must match `braids::MacroOscillatorShape`; a compile-time check enforces the count of 48), `TMBR` and `COLR` (default 0x80), `VOLM` (default 0x40, ~-12dB since Braids peaks near full scale).
- Pitch: Braids is built for 96 kHz. `Start()` sets pitch `= note*128 + braids::host_rate.pitch_offset` (1/128 semitone; offset = `12*128*log2(96000/rate)`), shifting pitch instead of resampling. Rate comes from `Audio::GetInstance()->GetSampleRate()`.
- Host-rate state: `sources/Externals/Braids/braids/host_rate.{h,cpp}` (ours, GPLv3). `SetHostSampleRate()` runs only when the rate changes and rescales rate-dependent constants.
- Per-voice DC blocker (one-pole high pass, 15 Hz, pole recomputed from the driver rate) runs in `renderBlock`. Braids renders 24-sample blocks into a per-voice FIFO that serves variable-size `Render` calls.
- No `ProcessCommand` handling, no release stage, no table automation yet (all stubs).

## Braids (`sources/Externals/Braids/`)
- Upstream: pichenettes/eurorack `braids/` at `08460a69a7e1f7a81c5a2abcc7189c9a6b7208d4`; stmlib at `e3bd7c9cc00e4364166f9905c0509b6ffd0535ec`. Files renamed `.cc` to `.cpp`.
- Our edits are marked `LGPT:` (`grep -rn "LGPT:" sources/Externals/Braids`): in `digital_oscillator.{h,cpp}`, `analog_oscillator.cpp`, `svf.h`; plus two upstream bug fixes (WAVE LINE `wave_line[64]` read, negative shift in `ComputeDelay`). No `patches/` directory exists.
- Rule: at 96 kHz output must stay bit-identical to upstream. Any rate-dependent change must be an identity at 96000.
- `THIRD_PARTY_NOTICES.md` records commits, files and changes. Update it whenever you touch or add a file under `Externals/Braids`.

## Known issues
- Which Braids models still differ from the 96 kHz reference at other rates: unknown, nothing in the repo records it (unverified).
- Upstream signed-overflow warnings: a `-g -Wall` build shows none from Braids (only unused-variable and nonnull-compare warnings elsewhere). Any overflow reports would come from UBSan/other flags (unverified).
- Unknown-TYPE crash in `InstrumentBank::RestoreContent`: fixed in 2035c45 (unknown type logs an error and leaves the slot default).
- Hardcoded 44100: `PlayerChannel.cpp:132` and `:152` (HPF/LPF alpha) and `SoundFontPreset.cpp:36`. `Filters.cpp` has no 44100 (I grepped). These filters are wrong at other driver rates.

## Test tooling
- `pt_lab` (tuning sweep, rate audit, sanitizer stress test) is not in this repo and not found on this machine, and no `PT_AUDIT.md` or `ROADMAP.md` exists. I can't give commands or run times. If it exists elsewhere, track it in-repo (suggest `tools/pt_lab/`) and document it here.

## Workflow rules
- Survey the code before editing. One change at a time. Commit at each working stage.
- Never claim something works without running it (build, run, or test it).
- The agent cannot hear audio. The user is the listening test; ask them to verify sound.

## Not yet verified
- Running the binary / audio output in this session; any handheld hardware; an optimised (`-O3`) build; non-X64 platforms; project files for MSVC/Xcode.
