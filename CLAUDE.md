# CLAUDE.md

This fork of djdiskmachine/LittleGPTracker. Remotes: `origin` = domgalati/LittleGPTracker, `upstream` = djdiskmachine. Adds PIGTAIL, a synth instrument driven by Mutable Instruments Braids. Upstream docs: `docs/`. Everything below was checked on Linux X64 by reading the code or running it; items measured with tooling that no longer exists say so, and code-reading-only claims are labelled.
- Naming: PIGTAIL is the name of the Braids-based instrument only. The fork has no project name yet; "Synthesis" (commit e9c37d9) was a placeholder. Don't present any name as the project's. TODO: the user picks a name.

## Build and run (X64, Linux)
- `cd projects && make PLATFORM=X64`. It must run from `projects/` (the Makefile includes `$(PWD)/Makefile.$(PLATFORM)`). A clean serial build takes about 40 s here. Objects go to `projects/buildX64/`, the binary to `projects/lgpt.x64` (both gitignored). Clean with `make PLATFORM=X64 clean`.
- `make` first runs `python3 ../sources/Resources/mkfont.py`, which needs Pillow (system 10.2.0 is installed) and rewrites the tracked `sources/Resources/font.h` (identical output with the default font). The Makefile puts `/bin` first on PATH, so a venv's `python3` is ignored; use `PYTHONPATH`.
- Needs pkg-config `sdl2` and `alsa`. JACK is optional for X64 (without it the build succeeds; pkg-config only prints errors).
- Run: `config.xml` and `mapping.xml` must sit next to the binary. `bin:` is the executable's directory (`LINUXSystem.cpp:74`), read at `Config.cpp:6` and `Application.cpp:54`. Without them the log says `No (bad?) config.xml` / `No (bad?) mapping file` and no keys are mapped. Projects live in the working directory (`root` = `.`).
  `mkdir -p ~/lgpt-run && cp projects/lgpt.x64 projects/resources/X64/{config,mapping}.xml ~/lgpt-run/ && cd ~/lgpt-run && ./lgpt.x64`
  Headless smoke test (boots, no input): `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 5 ./lgpt.x64`
- Keys (`projects/resources/X64/mapping.xml`): arrows = d-pad, `s` = A, `a` = B, `q` = L, `w` = R, `space` = START. The shipped `config.xml` sets `DUMPEVENT`, `AUTO_LOAD_LAST` and `SCREENMULT=2`; `KEY_*` overrides are in `docs/LittlePiggyTrackerConf.md`.

## Compiler constraints
- `projects/Makefile.X64`: `CXXFLAGS := $(CFLAGS) -std=gnu++03` (with `-Wall`). That rules out `nullptr`, `auto`, lambdas, range-for, `constexpr`, `override` and `<cstdint>`. The ported Braids code compiles warning-free under it.
- `Makefile.X64` sets `OPT_FLAGS := -O3` and then overrides it with `-g`, so default builds are unoptimised and CPU numbers from them aren't representative.
- Braids objects (the `BRAIDSFILES` list in `projects/Makefile`) get `-fwrapv` through a target-specific variable, `$(BRAIDSFILES): CXXFLAGS += -fwrapv`, because Braids' fixed-point code relies on signed overflow wrapping. Global flags are unchanged: in a build log only those 6 compile lines carry `-fwrapv`. Braids also needs `CXXFLAGS += -I$(PWD)/../sources/Externals/Braids`. A new Braids `.cpp` must go in `BRAIDSFILES`.

## Adding or changing an instrument type (PIGTAIL commits cb802a4, 1674229)
1. Enum: add `IT_*` before `IT_LAST` in `sources/Application/Instruments/I_Instrument.h`.
2. Class: `sources/Application/Instruments/<Name>Instrument.{h,cpp}`, implementing every pure virtual in `I_Instrument`.
3. Slots: `MAX_*INSTRUMENT_COUNT`, `*_INSTRUMENT_BASE` and `MAX_INSTRUMENT_COUNT` in `sources/Application/Model/Song.h`; allocate them in the `InstrumentBank` constructor. A slot's type is fixed; there's no type picker.
4. Save/load (`InstrumentBank.cpp`):
   - Add the type name to `InstrumentTypeData[]` (saved as `TYPE`), and to the `switch` in `RestoreContent` and in `Clone()` (whose `default` creates a MIDI instrument).
   - Parameters need nothing extra: each inserted `Variable` is saved as `<PARAM NAME=.. VALUE=..>` and restored by its *name*, not its FourCC. The legacy binary loader at `Project.cpp:386` must stay on `LEGACY_INSTRUMENT_COUNT`.
5. View: add `fill<Name>Parameters()` and a case in the type `switch` of `onInstrumentChange()` (`sources/Application/Views/InstrumentView.{h,cpp}`). B+A clears `SIP_TABLE` on the last field; there's a null guard for instruments without a table.
6. Build: add `<Name>Instrument.o` to `COMMONFILES` in `projects/Makefile` (line 268).
- Render contract (`I_Instrument.h`, `PlayerChannel.cpp:51`, `AudioMixer.cpp:78-88`):
  - Fill `size` interleaved stereo Q15 frames (`buffer[2*size]`; int16 sample `s` is `i2fp(s)`). Overwrite, don't accumulate: the mixer passes the first module the output buffer directly and sums the others itself.
  - Return true only if you produced audio; otherwise `PlayerChannel` ignores the buffer. `size` is about rate × 2.5 / tempo and varies by call. `flags & 1` = table tick, `flags & 2` = muted.
  - One instrument can play on several channels, so keep voice state per song channel.
- Only when upstreaming: `projects/lgpt.vcxproj`, `lgpt.vcxproj.filters`, `lgpt.vcproj`, `lgptest.dev`, `lgpt.xcodeproj` and `lgpt64.xcodeproj` have no PIGTAIL or Braids entries. `CONTRIBUTING.md` also asks for a `BUILD_COUNT` bump in `sources/Application/Model/Project.h`, a `CHANGELOG` entry and an update to `docs/wiki/What-is-LittlePiggyTracker.md`.

## PIGTAIL (`sources/Application/Instruments/PigtailInstrument.{h,cpp}`)
- Slots `0x90`–`0x9F` (`PIGTAIL_INSTRUMENT_BASE`, 16 slots), saved with `TYPE="Pigtail"`.
- Parameters (saved by name):
  - `shape`: 48 names in `braids::MacroOscillatorShape` order, checked by a compile-time count. Saved as the name; an unknown name loads as `csaw`; the index is clamped when rendering.
  - `timbre` and `color`: 0x00–0xFF, sent to Braids as value << 7; default 0x80.
  - `volume`: default 0x40 (about −12 dB) because Braids models peak near full scale.
- Pitch: Braids' tables assume 96 kHz. Braids runs at the driver rate (`Audio::GetInstance()->GetSampleRate()`), and `Start()` sets pitch = `note*128 + braids::host_rate.pitch_offset`, where the offset is `12*128*log2(96000/rate)` (1724 at 44.1 kHz). Chosen over resampling because a 96 → 44.1 kHz resampler needs an anti-alias filter and costs about 2.2× the CPU.
- Host-rate state lives in `sources/Externals/Braids/braids/host_rate.{h,cpp}` (our code, GPLv3). `SetHostSampleRate()` runs only when the rate changes.
- Per voice (static `voices_[SONG_CHANNEL_COUNT]`): Braids renders 24-sample blocks into a FIFO that serves any `Render` size. A 15 Hz one-pole DC blocker (`PIGTAIL_DC_CUTOFF`, pole recomputed from the driver rate) runs in `renderBlock()`, before volume is applied.
- Stubs so far: `ProcessCommand`, table automation, and any release stage (so notes click on start and stop).

## Braids (`sources/Externals/Braids/`)
- Sources: pichenettes/eurorack `braids/` @ `08460a69a7e1f7a81c5a2abcc7189c9a6b7208d4` and pichenettes/stmlib @ `e3bd7c9cc00e4364166f9905c0509b6ffd0535ec`, with `.cc` renamed to `.cpp`.
- Commit f43d554 is the pristine import, so `git diff f43d554 -- sources/Externals/Braids` shows every local change. There's no `patches/` directory.
- Our edits are marked `LGPT:` (`grep -rn "LGPT:" sources/Externals/Braids`), in `digital_oscillator.{h,cpp}`, `analog_oscillator.cpp` and `svf.h`. They include two upstream bug fixes: WAVE LINE reading `wave_line[64]`, and a negative shift in `ComputeDelay`.
- Rule: at 96 kHz the output must stay bit-identical to upstream, so any rate-dependent change must be an exact identity at 96000.
- `THIRD_PARTY_NOTICES.md` records the commits, files and changes. Update it whenever a file under `sources/Externals/Braids` changes.

## Known issues
- At 44.1 kHz these still differ from the 96 kHz reference (`make audit`/`make tune` in `tools/pt_lab`; re-run 2026-10-09):
  - BLOWN: fixed 2-tap reflection filter can't be rescaled; darker, about 4 dB louder, 5–11 cents sharp. BOWED: upper harmonics differ by 10–30 dB, up to about 6.5 cents off, may not start at low notes with timbre/color 0.
  - FLUTED can jump register. SNARE noise burst brighter (+4 to +9 semitones of centroid). VOWEL_FOF top formant about 10 dB too strong. PLUCK at high timbre: fundamental about 15 dB weaker.
  - SWARM, MORPH at high color and FM at high index alias more.
- Without `-fwrapv`, UBSan reports 83 signed-integer overflows in upstream Braids' fixed-point code (`stmlib/utils/dsp.h` lines 97/110/128/154, `svf.h:99`, 7 lines in `digital_oscillator.cpp`). The build's `-fwrapv` makes this defined. A `-Wall` build shows no warnings from Braids.
- The unknown-TYPE use-after-free in `InstrumentBank::RestoreContent` is fixed (2035c45): an unknown type logs an error and the slot keeps its default.
- Hardcoded rates: `PlayerChannel.cpp:132` and `:152` compute the channel HPF/LPF coefficients with 44100, so those filters are wrong at other driver rates. `Filters.cpp:54` uses `1/22050`; `Audio.h:16` defaults `GetSampleRate()` to 44100.
- Older builds read past the end of their instrument bank when a phrase uses instrument 0x90 or higher. Reproduced under ASan at 2035c45 (the commit before PIGTAIL): the phrase byte survives save/load unclamped (`Song.cpp` `restoreHexBuffer`), and `InstrumentBank::GetInstrument(0x90)` overflows the 0x90-entry `instrument_[]` (heap-buffer-overflow at `InstrumentBank.cpp:59`). Old builds skip the `INSTRUMENT` entries themselves (`id < MAX_INSTRUMENT_COUNT`). From reading the code, the callers at `Player.cpp:836` and `PhraseView.cpp:1346` then call into that garbage pointer.

## Test tooling (`tools/pt_lab/`, not part of the default build)
- Needs the X64 objects first (`cd projects && make PLATFORM=X64`); then from `tools/pt_lab/`: `make ident` (2.5 s, 96 kHz bit identity vs pristine f43d554), `make init` (1.6 s), `make audit` (8.5 s, 96 kHz reference vs PIGTAIL at 44.1 kHz, 5 seeds), `make stress` (8 s, ASan+UBSan), `make cpu` (2 s), `make tune` (~2 min 20 s, YIN sweep), or `make all-tests`. Output goes to `tools/pt_lab/build/` (gitignored).
- `pt_lab` links the real `projects/buildX64/*.o` (minus `LINUXMain.o`), so rebuild X64 after source changes. The sanitizer build recompiles Braids and `PigtailInstrument.cpp` instrumented, with `-fwrapv` like the real build; `make stress BRAIDS_WRAPV=` drops it to see upstream's 83 signed-overflow reports.
- Results and their history: `PT_AUDIT.md` (section 11 is the latest re-run). `ROADMAP.md` doesn't exist yet (checked 2026-10-09).

## Workflow rules
- Survey the code before editing. Make one change at a time. Commit at each working stage.
- Never claim something works without building, running or testing it, and say what you didn't run.
- The agent can't hear audio. The user is the listening test, so ask them to listen.

## Not yet verified
- Any handheld hardware or non-X64 platform; an optimised (`-O3`) build; the MSVC and Xcode project files.
- By ear, the user has confirmed PIGTAIL on CSAW: it plays, is in tune, responds to timbre, color and volume (including sweeps during playback), and saves and loads. The other 47 shapes haven't been listened to.
- Driver rates other than 44.1 kHz in the real app (48 kHz only in `make tune`); PIGTAIL CPU on handhelds.
