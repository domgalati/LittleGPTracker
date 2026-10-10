# Little Piggy Tracker: fork with a Braids-based synth instrument

<!-- TODO: this fork has no project name yet ("Synthesis" was a placeholder). PIGTAIL is the instrument's name, not the fork's. -->

This is a fork of [djdiskmachine/LittleGPTracker](https://github.com/djdiskmachine/LittleGPTracker) (Little Piggy Tracker, f.k.a. LittleGPTracker), which in turn builds on the original work of [Marc Nostromo (m-.-n)](https://github.com/Mdashdotdashn/LittleGPTracker).

**For everything general (what the tracker is, supported platforms, releases, build instructions, configuration, usage docs) see the [original README](https://github.com/djdiskmachine/LittleGPTracker#readme).** This README only covers what this fork adds. The upstream documentation in [docs](docs) still applies.

This fork is licensed under the [GPLv3](LICENSE), like upstream. Bundled third-party code is listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

> This is a work-in-progress development fork. Expect rough edges, and back up your projects before opening them in this build.

## What's added

### Instrument slots: any type in any slot

Upstream fixes each instrument slot's type by its number: `00`–`7F` are samples, `80`–`8F` MIDI. Here the 255 slots `00`–`FE` all start empty, and each one takes any type: SAMPLE, MIDI or PIGTAIL.

- **Picking a type:** the instrument screen has a `type:` field above the parameters. On an empty slot (`type: --`), hold A and press RIGHT to step through `SAMPLE`, `MIDI` and `PIGTAIL` (LEFT steps back). The instrument is created as you step; release A to keep it.
- **Changing a type:** clear the slot first. B,A on the `type:` field asks before it empties the slot (`Clear I23 (MIDI)? In 4 phrases`). The slot's settings go with it; its sample stays in the project and its table stays as it is. Phrases keep the slot number and play whatever the slot holds next. An empty slot plays nothing.
- **New instruments:** A,A in a phrase's instrument column gives the lowest empty slot. Cloning an instrument (LT+(B,A) on its number in a phrase) copies it into the lowest empty slot, whatever its type. Importing a sample into an empty slot creates a sample instrument there. A new MIDI instrument starts on the MIDI channel given by its slot's last hex digit (`80` and `90` on channel 1, `8F` on 16), as upstream's MIDI slots do.
- **Compact Instruments** empties the sample slots no phrase uses. MIDI and PIGTAIL slots are left alone.
- **Command popup:** in the phrase and table screens, the command popup (A+UP/DOWN in a command column) dims the commands that won't do anything for the instrument that row plays, or in that screen, and a status line under the grid says why.

Project files:

- Projects saved by upstream LittleGPTracker load and play as they do upstream, including files older than 2014. Sample slots without a sample load as empty slots.
- Projects saved by this build use the same file name and format, with `LAYOUT="2"` on the instrument bank. They aren't meant to be opened in upstream builds.
- An instrument whose type this build doesn't know (one added by a newer build, say) is kept. Its slot shows `type: Granular (unknown)`, plays nothing, and is saved back unchanged. Clear it to reuse the slot.

### PIGTAIL: a synth instrument

LGPT's instruments have so far been sample players (and MIDI out). PIGTAIL adds a new instrument type that generates sound instead of playing back a sample. Its voice is the Braids macro oscillator by Émilie Gillet ([github.com/pichenettes/eurorack](https://github.com/pichenettes/eurorack), MIT license), which gives you 48 oscillator models in one instrument.

A PIGTAIL instrument can go in any slot: pick `PIGTAIL` in an empty slot's `type:` field (see above). Put that slot's number in a phrase's instrument column, and its instrument view shows these parameters:

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
- Émilie Gillet, author of Braids and stmlib ([github.com/pichenettes/eurorack](https://github.com/pichenettes/eurorack), MIT license)

This project is not affiliated with or endorsed by Mutable Instruments.
