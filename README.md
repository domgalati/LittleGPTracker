# Little Piggy Tracker: Synthesis fork

This is a fork of [djdiskmachine/LittleGPTracker](https://github.com/djdiskmachine/LittleGPTracker) (Little Piggy Tracker, f.k.a. LittleGPTracker), which in turn builds on the original work of [Marc Nostromo (m-.-n)](https://github.com/Mdashdotdashn/LittleGPTracker).

**For everything general (what the tracker is, supported platforms, releases, build instructions, configuration, usage docs) see the [original README](https://github.com/djdiskmachine/LittleGPTracker#readme).** This README only covers what this fork adds. The upstream documentation in [docs](docs) still applies.

This fork is licensed under the [GPLv3](LICENSE), like upstream. Bundled third-party code is listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

> This is a work-in-progress development fork. Expect rough edges, and back up your projects before opening them in this build.

## What's added

### PIGTAIL: a synth instrument

LGPT's instruments have so far been sample players (and MIDI out). PIGTAIL adds a new instrument type that generates sound instead of playing back a sample. Its voice is the macro oscillator from [Mutable Instruments Braids](https://pichenettes.github.io/mutable-instruments-documentation/modules/braids/), which gives you 48 oscillator models in one instrument.

Pick the PIGTAIL type in the instrument view and you get these parameters:

| Parameter | Range | Description |
| --- | --- | --- |
| `shape` | list | Which Braids oscillator model to play |
| `timbre` | `00`-`FF` | First model-specific parameter (its meaning changes with the shape) |
| `color` | `00`-`FF` | Second model-specific parameter (its meaning changes with the shape) |
| `volume` | `00`-`FF` | Output level (default `40`, as Braids models run hot) |

Available shapes, in order:

- **Analog-style:** `csaw`, `morph`, `saw/sq`, `fold`, `buzz`
- **Sub / sync / stacked:** `sub sq`, `sub saw`, `sync sq`, `sync saw`, `saw x3`, `sq x3`, `tri x3`, `sine x3`, `ring`, `swarm`, `saw comb`, `toy`
- **Filtered (z-filter):** `zlpf`, `zpkf`, `zbpf`, `zhpf`
- **Vocal / harmonic:** `vosim`, `vowel`, `vow fof`, `harmonic`
- **FM:** `fm`, `fb fm`, `wt fm`
- **Physical models:** `pluck`, `bowed`, `blown`, `fluted`
- **Percussion:** `bell`, `drum`, `kick`, `cymbal`, `snare`
- **Wavetables:** `wtbl`, `wmap`, `wline`, `wt x4`
- **Noise / digital:** `noise`, `twin q`, `clk noise`, `cloud`, `particle`, `qpsk`, `qmark`

Notes on the current implementation:

- It is monophonic per song channel, like other LGPT instruments.
- Braids is natively a 96 kHz design. Here it runs at the audio driver's sample rate, with its rate-dependent constants adjusted (see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for details).
- Shape names are what gets saved in a project, so they are kept stable across versions.
- There is no envelope or release stage yet. A note plays until the channel stops or another note triggers.
- Table automation is not wired up for PIGTAIL yet.

The code lives in [PigtailInstrument.cpp](sources/Application/Instruments/PigtailInstrument.cpp), with the vendored Braids sources under [sources/Externals/Braids](sources/Externals/Braids).

## Roadmap

These are plans, not features that exist today.

### More synth instruments

PIGTAIL is the first of several planned synth instrument types. Next up:

- **FM synth**
- More to follow

### Synth parameter control from the phrase editor

Right now PIGTAIL parameters are set per instrument in the instrument view and can't change during playback. The plan is to expose them as phrase commands, in the same way sample instruments already respond to commands, so `shape`, `timbre`, `color` and `volume` can be changed per step. The hook for this (`PigtailInstrument::ProcessCommand`) exists but is currently a stub. The same mechanism should carry over to the upcoming synth instruments.

## Building

Build as for upstream; see the [projects README](projects/README.md) for per-platform instructions. For example, on Linux:

```bash
cd projects
make PLATFORM=DEB
```

(Platform names and prerequisites are listed in that README.) Only tagged upstream releases are considered stable; this fork tracks `master` and is experimental.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Upstream changes can be pulled in from the `upstream` remote (`https://github.com/djdiskmachine/LittleGPTracker.git`).

## Credits

- [Marc Nostromo](https://github.com/Mdashdotdashn/LittleGPTracker), original author of LittleGPTracker
- [djdiskmachine](https://github.com/djdiskmachine/LittleGPTracker) and contributors, maintainers of Little Piggy Tracker
- [Émilie Gillet / Mutable Instruments](https://github.com/pichenettes/eurorack), author of Braids and stmlib (MIT licensed)
