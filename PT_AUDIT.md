# PIGTAIL audit record

**Provenance.** This file was reconstructed from the final report of the session that ported Braids into PIGTAIL and audited it. The test program that produced the numbers (`pt_lab`) was never committed and no longer exists, so none of the figures below can currently be regenerated. Treat them as a record of what was measured, not as a live test suite. Anything marked **(unverified)** has not been re-checked against the code since.

**Conditions.** All measurements were taken on an x86-64 Linux desktop with the default `-g` (unoptimised) X64 build. The reference for every comparison is Braids rendered natively at 96 kHz, the rate its lookup tables are built for. PIGTAIL was measured at 44.1 kHz, with 48 kHz added for the tuning sweep. No handheld hardware was involved.

## 0. Checked against the code (2026-10-09, master at 5976738)

These were re-checked by reading the code and git history. The measured numbers in sections 3, 4, 6 and 8 could not be re-checked this way and are unchanged.

- **Shapes:** `shapeNames[]` (`sources/Application/Instruments/PigtailInstrument.cpp:18`) has 48 names. They map one-to-one, in order, onto `braids::MacroOscillatorShape` in `sources/Externals/Braids/braids/settings.h`, from `csaw` (`MACRO_OSC_SHAPE_CSAW`) to `qmark` (`MACRO_OSC_SHAPE_QUESTION_MARK`). A compile-time check (`PigtailInstrument.cpp:35`) enforces the count against `MACRO_OSC_SHAPE_LAST`. Full order: csaw, morph, saw/sq, fold, buzz, sub sq, sub saw, sync sq, sync saw, saw x3, sq x3, tri x3, sine x3, ring, swarm, saw comb, toy, zlpf, zpkf, zbpf, zhpf, vosim, vowel, vow fof, harmonic, fm, fb fm, wt fm, pluck, bowed, blown, fluted, bell, drum, kick, cymbal, snare, wtbl, wmap, wline, wt x4, noise, twin q, clk noise, cloud, particle, qpsk, qmark.
- **Default volume:** 0x40 (`PigtailInstrument.cpp:123`).
- **DC blocker:** `PIGTAIL_DC_CUTOFF 15.0` Hz (`PigtailInstrument.cpp:61`), pole `exp(-2*pi*15/rate)` computed at line 72.
- **Host-rate files:** `sources/Externals/Braids/braids/host_rate.h` and `host_rate.cpp`, added in 1674229.
- **`LGPT:` markers** (`grep -c "LGPT:"`): `digital_oscillator.cpp` 38, `digital_oscillator.h` 2 (lines 145, 171), `analog_oscillator.cpp` 2 (48, 62), `svf.h` 1 (73). `digital_oscillator.cpp` also has 30 more lines marked `// LGPT` without the colon (68 lines contain `LGPT` in all), so grep for `LGPT`, not `LGPT:`, to see every change. `git diff f43d554 -- sources/Externals/Braids` shows all of them.
- **Upstream bug fixes** (`digital_oscillator.cpp`): WAVE LINE's index clamp at lines 1770-1773 (comment at 1770, `wave_line[` read at 1772-1773); `ComputeDelay`'s shift clamp at lines 126-131 (comment at 126, `delay >>= 12 - num_shifts` at 131).
- **Slot range:** `0x90`-`0x9F` (`PIGTAIL_INSTRUMENT_BASE` = 0x80 + 0x10, `MAX_PIGTAILINSTRUMENT_COUNT` 0x10, `sources/Application/Model/Song.h:13-21`).

## 1. Outcome

- All 48 Braids models (the 47 upstream plus the hidden `qmark`) are selectable in PIGTAIL.
- No 96 kHz resampling path was needed. Rate dependence was handled by rescaling constants in the Braids code, with edits marked `LGPT:`.
- At 96 kHz the patched code is bit-identical to upstream for all 48 models (checksum comparison against pristine upstream).
- Five models still differ audibly from the 96 kHz reference (section 5).
  - *Note (2026-10-09): section 5 lists more than five entries (BLOWN, BOWED, FLUTED, SNARE, VOWEL_FOF, PLUCK, the aliasing group, CLOUD/PARTICLE). The count here doesn't match that list.*
- Two crash-class upstream bugs were found and fixed (section 7).

## 2. Approach

**Pitch.** Braids' pitch tables assume 96 kHz. PIGTAIL runs Braids at the driver rate and raises the note by `12*128*log2(96000/rate)` pitch units (about +13.5 semitones at 44.1 kHz, exactly +12 at 48 kHz). Resampling 96 kHz down to 44.1 kHz was rejected: it needs an anti-alias filter and costs about 2.2x the oscillator CPU.

**Rescaling.** Anything in Braids that depends on absolute frequency or on time counted in samples was patched:

- Absolute frequencies get the pitch offset.
- Sample counts and per-sample decays are scaled by the rate ratio.
- PLUCK uses a matched weighted average.
- BOWED and FLUTED have their filter poles and delay compensation remapped.
- The TWIN Q resonator gain table clips at 256, so the gain correction is applied after the clip (this also fixed PARTICLE, which shares the tables).
- SNARE, DRUM, BOWED and FLUTED noise gain is scaled by 1/sqrt(ratio) to correct a noise power mismatch.
  - *Note (2026-10-09): the code applies `host_noise_gain` in RenderStruckDrum, RenderBlown, RenderFluted and RenderSnare (`digital_oscillator.cpp` lines 1127, 1450, 1546, 2583). That is DRUM, BLOWN, FLUTED and SNARE; BOWED has no noise gain.*
- SAW COMB's delay buffer is capped so it holds the same time as upstream.
- The pitch ceiling was removed for notes above about MIDI 114 and for top formants.

Host-rate state lives in `sources/Externals/Braids/braids/host_rate.{h,cpp}` (our code, GPLv3).

## 3. Rate-dependence audit (C3, timbre/color 80/80 and 30/D0)

Metrics: spectral centroid shift, band-level distance, decay time, event rate. Analog and macro models, filter models, FM, wavetables, CLK NOISE and QPSK were already within the noise floor (about -0.8 semitone centroid shift, under 1.5 dB band distance, from aliasing and band-limit differences). These were off before the fixes:

| Model | Before fix | After fix |
|---|---|---|
| VOSIM, VOWEL, VOWEL_FOF | formants 10-13 semitones low | within +/-0.5 semitones |
| CYMBAL | filters and base pitch about 7 semitones low | within +0.9 semitones |
| TWIN Q | -23 semitones at 30/D0 | about 0 |
| TOY | about +5 semitones | about 0 |
| FB FM | -2 semitones | 0 |
| BELL, DRUM | decays 2.0-2.2x too long | 1.0-1.3x |
| SNARE | +24 semitones brighter, decay 2x | +4 to +9 semitones brighter, decay 1.2x |
| CLOUD, PARTICLE | grains 2x longer, density 0.3-0.6x | about 1.0 |
| QMARK | 5.5x slower | 1.0 |
| PLUCK | decay mismatch | per-harmonic decay within 0.2 dB over 2 s |
| BOWED | -6 to -13 semitones | +/-2 semitones, upper harmonics still differ 10-30 dB |
| BLOWN | -5 to -11 semitones | not fixed |

Notes on the method:

- Several early flags were metric noise. PLUCK showed a 26-semitone centroid spread between reference seeds, so the noise-driven models were re-measured with 5 seeds and judged against their own run-to-run spread.
- The apparent SAW/SQ x3 shift was a unison-phase measurement artefact. MORPH's was aliasing from low-rate harmonics folding back, not a fixed 96 kHz effect.
- SNARE's apparent decay mismatch was integer-zero clipping at -60 dB. The real difference was a +3.4 dB level boost at 44.1 kHz, matching 10*log10(ratio).

## 4. Tuning sweep

**Method.** 36 pitched models, C1-C7, at 44.1 and 48 kHz, with timbre and color each at 0, 128 and 255. Fundamental measured with YIN, refined with a harmonic-weighted FFT. A cell counts as a PIGTAIL error only where the 96 kHz reference shows a clean fundamental at the requested note, because many Braids models place energy away from the fundamental by design (sub-octave modes, chords in the TRIPLE modes, FM ratios).

**Skipped as unpitched:** bell, drum, kick, cymbal, snare, noise, twin q, clk noise, cloud, particle, qpsk, qmark.

**Result.** Over 2,730 qualifying cells the median error is 0.18 cents and the 95th percentile is 3.8 cents. Worst error in cents per octave, then the count of other cells over 5 cents, then the count of octave flips:

| Model | C1 | C2 | C3 | C4 | C5 | C6 | C7 | >5c | Octave |
|---|---|---|---|---|---|---|---|---|---|
| csaw, morph, sync sq, sine/tri/saw/sq x3, ring, vosim, vowel, vow fof, harmonic, fb fm, wmap, wline | <=4 | <=0.2 | <=0.2 | <=0.2 | <=0.2 | <=0.2 | <=0.8 | <=2 | <=1 |
| saw/sq | 0.2 | 0.2 | 0.2 | 0.2 | 0.2 | 0.2 | 0.5 | 0 | 3 |
| fold | 2.8 | 0.2 | 0.2 | 0.2 | 0.2 | 0.2 | 2.6 | 0 | 0 |
| buzz | 2.4 | 99 | 0.2 | 50 | 50 | 0.2 | 0.2 | 5 | 1 |
| sync saw | 0.2 | 0.9 | 1.4 | 3.5 | 0.2 | 0.2 | 1.0 | 0 | 0 |
| swarm | 2.6 | 6.5 | 1.9 | 4.8 | 7.3 | 23.7 | 4.7 | 8 | 3 |
| saw comb | 3.5 | 2.3 | 0.2 | 0.2 | 137 | 0.2 | 0.2 | 1 | 6 |
| toy | 3.2 | 2.5 | 0.2 | 0.2 | 0.2 | 0.3 | 1.5 | 0 | 0 |
| zlpf/zpkf/zbpf/zhpf | <=3.7 | 0.2 | 0.3 | 0.3 | 0.2 | 0.2 | <=5.3 | <=2 | 4-10 |
| fm | 387 | 0.2 | 0.2 | 0.2 | 0.2 | 0.2 | 0.8 | 2 | 2 |
| wt fm | 0.2 | 106 | 0.3 | 105 | 0.2 | 0.2 | 0.8 | 2 | 0 |
| pluck | 1.2 | 0.5 | 0.6 | 1.1 | 0.3 | 0.4 | 0.3 | 2 | 3 |
| bowed | 4.8 | 4.5 | 6.5 | - | - | - | - | 16 | 0 |
| blown | 2.7 | 4.7 | 5.6 | 10.6 | - | - | - | 23 | 0 |
| fluted | - | - | 1.2 | - | 3.6 | - | - | 0 | 3 |
| wtbl | 1.3 | 2.4 | 0.2 | 0.2 | 0.2 | 0.5 | 1.2 | 0 | 0 |
| wt x4 | 3.1 | 3.6 | 2.8 | 2.7 | 3.0 | 3.4 | 5.5 | 1 | 0 |

A "-" means the 96 kHz reference itself had no clean fundamental at that octave.

**Interpretation recorded at the time.**

- BUZZ's 50 and 99 cent cells come from its second oscillator, which is detuned by color. The estimator sometimes latches onto it.
- FM and WT FM produce inharmonic ratios at low notes.
- The octave flips on SAW/SQ, VOWEL, the z*PF filters and PLUCK were checked by spectrum. The fundamental had not moved. Aliasing products or a weak fundamental fooled the estimator.
- SAW COMB had a real error from its delay buffer holding more time at 44.1 kHz. It was fixed and now matches the reference almost everywhere, but **the final table still shows 137 cents at C5, which was never explained. Check this by ear or with a tuner.**
- FM at C1 (387 cents) and WT FM at C2 and C4 (105-106 cents) were also not explained individually and are probably estimator artefacts. **(unverified)**
- Real errors: BLOWN (5-11 cents sharp), BOWED (up to 6.5 cents, sometimes does not start), FLUTED (can jump register), SWARM (up to 24 cents, from aliasing and a filter limit).
- The agent cannot hear audio. A tuner check on A3 and A5 for several models is still outstanding.

## 5. Models that still differ from the 96 kHz reference

- **BLOWN:** not fixed. The reflection filter is a fixed 2-tap average whose response moves with sample rate and can't be rescaled without changing tuning. Darker, about 4 dB louder, 5-11 cents sharp.
- **BOWED:** upper harmonics differ by 10-30 dB, because stick-slip self-oscillation amplifies small differences. Up to about 6.5 cents off. May not start at low notes with timbre/color 0.
- **FLUTED:** can jump to the twelfth at settings where the module wouldn't.
- **SNARE:** noise burst brighter (+4 to +9 semitones of centroid). Residual brightness near Nyquist.
- **VOWEL_FOF:** top formant about 10 dB too strong.
- **PLUCK at high timbre:** fundamental about 15 dB weaker.
- **SWARM, MORPH at high color, FM at high index:** more aliasing than at 96 kHz.
- **CLOUD and PARTICLE:** random, so only checked on averages.

## 6. DC blocker and default volume

- CSAW has up to about 11% of full scale of DC offset depending on timbre and color (upstream behaviour). Every model now runs through a per-voice 15 Hz one-pole high-pass, with the pole computed from the driver rate.
- Default volume lowered from 0x80 to 0x40. New instruments peak around -12 dBFS, about -8 dBFS at worst. Saved instruments keep their saved volume.

## 7. Robustness

- Every model initialises and produces sound from a fresh voice. None is silent.
- Shape-switching stress test under AddressSanitizer and UBSan: 288 shape changes on one held note, then 5,242 random shape, parameter and note changes across 8 channels over 60 s of audio each. No crash, no hang.
- Unknown saved shape names load as `csaw`. Out-of-range indices are clamped at render time.
- **Upstream defect 1:** WAVE LINE read past the end of its 64-entry table (`digital_oscillator.cpp`, near line 1765) at maximum timbre. Fixed.
  - *Note (2026-10-09): the fix is now at lines 1770-1773 (upstream `digital_oscillator.cc` line 1637).*
- **Upstream defect 2:** `ComputeDelay` used a negative shift at negative pitches. Fixed.
- UBSan still reports signed-integer overflow in upstream fixed-point code (`stmlib/utils/dsp.h`, `svf.h`, several lines in `digital_oscillator.cpp`). GCC wraps these in practice. `-fwrapv` on the Braids objects would make that guaranteed.
- The tuning sweep also exposed a real PIGTAIL bug: the pitch offset pushed notes past Braids' table clamp. Fixed by extending the table range through octave shifts.

## 8. CPU

Per voice, as a fraction of real time on the x86 `-g` build: 0.05-0.23%. Heaviest: FOLD (0.23%), HARMONIC (0.19%), then CYMBAL, WMAP, WLINE and BELL (about 0.17-0.18%). Lightest: CLK NOISE (0.05%).

These show headroom on the desktop only. They say nothing reliable about handheld ARM devices, and they come from an unoptimised build.

## 9. Listen to these first

1. BLOWN 2. BOWED 3. FLUTED 4. SNARE 5. VOWEL_FOF 6. PLUCK at high timbre 7. SWARM, MORPH at high color, FM at high index 8. CLOUD and PARTICLE

## 10. Open items

- Listening pass over all 48 shapes (only CSAW has been heard and confirmed in tune by the user).
- Tuner spot check on A3 and A5 for several models, especially SAW COMB, WT FM, FM and BUZZ.
- Click at note start and stop (PIGTAIL has no envelope or release stage).
- Optimised build and handheld CPU measurement.
- Driver rates other than 44.1 kHz in the real app (48 kHz was only tested in the harness).
- The hardcoded 44100 values in the channel HPF and LPF and in `Filters.cpp` are separate from PIGTAIL and still wrong at other driver rates.
  - *Note (2026-10-09): `PlayerChannel.cpp:132` and `:152` hardcode 44100. `Filters.cpp` has no 44100; it hardcodes `1/22050.0f` at line 54, which is the same 44.1 kHz assumption.*