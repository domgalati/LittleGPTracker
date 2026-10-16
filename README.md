# Little Piggy Tracker: fork with a Braids-based synth instrument

<!-- TODO: this fork has no project name yet ("Synthesis" was a placeholder). PIGTAIL is the instrument's name, not the fork's. -->

A fork of [djdiskmachine/LittleGPTracker](https://github.com/djdiskmachine/LittleGPTracker) (Little Piggy Tracker), which builds on [Marc Nostromo's](https://github.com/Mdashdotdashn/LittleGPTracker) LittleGPTracker. It adds **PIGTAIL**, a synth instrument built on the Braids macro oscillator, and lets any instrument slot hold any instrument type.

Everything else works as in Little Piggy Tracker: see the [upstream README](https://github.com/djdiskmachine/LittleGPTracker#readme) and the [docs](docs) for how to use the tracker.

> Work in progress. Back up your projects before opening them in this build.

## PIGTAIL

PIGTAIL generates its sound instead of playing a sample. Its voice is Émilie Gillet's Braids macro oscillator ([pichenettes/eurorack](https://github.com/pichenettes/eurorack)), which gives you 48 oscillator models in one instrument.

To make one, go to an empty slot's instrument screen, hold A on the `type:` field and press RIGHT until it says `PIGTAIL`. The screen then shows:

| Parameter | Range | What it does |
| --- | --- | --- |
| `shape` | list | Which oscillator model plays |
| `timbre` | `00`-`FF` | First model parameter; what it does depends on the shape |
| `color` | `00`-`FF` | Second model parameter; what it does depends on the shape |
| `volume` | `00`-`FF` | Output level. The default is `40` because the models run hot |

Shapes, in order:

- **Analog-style:** `csaw`, `morph`, `saw/sq`, `fold`, `buzz`
- **Sub / sync / stacked:** `sub sq`, `sub saw`, `sync sq`, `sync saw`, `saw x3`, `sq x3`, `tri x3`, `sine x3`, `ring`, `swarm`, `saw comb`, `toy`
- **Filtered:** `zlpf`, `zpkf`, `zbpf`, `zhpf`
- **Vocal / harmonic:** `vosim`, `vowel`, `vow fof`, `harmonic`
- **FM:** `fm`, `fb fm`, `wt fm`
- **Physical models:** `pluck`, `bowed`, `blown`, `fluted`
- **Percussion:** `bell`, `drum`, `kick`, `cymbal`, `snare`
- **Wavetables:** `wtbl`, `wmap`, `wline`, `wt x4`
- **Noise / digital:** `noise`, `twin q`, `clk noise`, `cloud`, `particle`, `qpsk`, `qmark`

### Commands

In a phrase, or in a table started with TABL:

| Command | Does |
| --- | --- |
| `TIMB aabb` | Moves timbre to `bb` |
| `COLR aabb` | Moves color to `bb` |
| `VOLM aabb` | Moves volume to `bb` |

`aa` is the speed, as with the sampler's VOLM, FCUT and FRES: `00` jumps straight there, and anything else takes `aa`×4 ticks. `bb` is on the same scale as the screen field, so `TIMB 0040` sounds like setting `timbre` to `40`.

Once a command has moved a parameter, that channel ignores the screen field until the next note that has an instrument number. A note without one keeps the command's value, so a sweep can run across several notes.

There's no envelope or release yet, so notes click when they start and stop.

## Instrument slots

In upstream, a slot's number fixes its type: `00`-`7F` are samples and `80`-`8F` are MIDI. Here all 255 slots (`00`-`FE`) start empty and take any type: SAMPLE, MIDI or PIGTAIL.

- **Choose a type:** on an empty slot (`type: --`), hold A on the `type:` field and press RIGHT or LEFT to step through the types.
- **Change a type:** B,A on the `type:` field clears the slot after asking (`Clear I23 (MIDI)? In 4 phrases`). Phrases that use the slot then play nothing until you give it a type again.
- A,A in a phrase's instrument column and cloning (LT+(B,A) on an instrument number) take the lowest empty slot. Importing a sample into an empty slot makes it a SAMPLE slot.
- The command popup (A+UP/DOWN in a command column) dims the commands that won't do anything on that row's instrument.

Projects from upstream load and play as they did there. Projects saved by this build aren't meant to be opened in upstream builds.

## Running it

It has been tested on Linux (x86-64) and on the TrimUI Brick running Knulli. Other platforms may build as they do upstream ([projects/README.md](projects/README.md)), but haven't been tried.

### Linux

You need SDL2, ALSA and Python 3 with Pillow (on Debian or Ubuntu: `sudo apt install libsdl2-dev libasound2-dev python3-pil`). Build from `projects/`:

```bash
cd projects && make PLATFORM=X64
```

`config.xml` and `mapping.xml` have to sit next to the binary. Projects are created in the folder you start it from:

```bash
mkdir -p ~/lgpt && cp projects/lgpt.x64 projects/resources/X64/*.xml ~/lgpt/ && cd ~/lgpt && ./lgpt.x64
```

Keys: arrows, `s` = A, `a` = B, `q` = L, `w` = R, space = START.

### TrimUI Brick (Knulli)

The binary is cross-compiled in Docker:

```bash
docker build -t lgpt-aarch64 tools/brick
```

```bash
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" -w /src/projects -e PWD=/src/projects lgpt-aarch64 make PLATFORM=RG35XXPLUS DEVKIT=/usr/ CROSS_COMPILE=aarch64-linux-gnu- SDL_CFLAGS="-I/usr/include/SDL2 -I/usr/include/aarch64-linux-gnu -D_REENTRANT"
```

To install it, copy the contents of [tools/brick/ports](tools/brick/ports) into `roms/ports/` on the SD card. Then copy `projects/lgpt-rg35xxplus.elf` there as `roms/ports/lgpt/lgpt-aarch64.elf`. LittleGPTracker then shows up in Knulli's Ports menu. Projects live in `roms/ports/lgpt`, and the log goes to `roms/ports/lgpt/log.txt`.

With Wi-Fi and SSH on, `BRICK_HOST=<ip> tools/brick/deploy.sh` builds and replaces the binary on the Brick in one go (it needs `sshpass`).

The included `config.xml` sets `SCREENWIDTH` to 1024, which centers the picture with black bars on each side. The bottom strip of the screen isn't drawn and shows leftover noise; add `<SCREENHEIGHT value='768'/>` to black it out.

## Credits and license

- [Marc Nostromo](https://github.com/Mdashdotdashn/LittleGPTracker), original author of LittleGPTracker
- [djdiskmachine](https://github.com/djdiskmachine/LittleGPTracker) and contributors, maintainers of Little Piggy Tracker
- Émilie Gillet, author of Braids and stmlib ([pichenettes/eurorack](https://github.com/pichenettes/eurorack), MIT license)

Licensed under the [GPLv3](LICENSE), like upstream. Bundled third-party code is listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Contributing: [CONTRIBUTING.md](CONTRIBUTING.md).

This project is not affiliated with or endorsed by Mutable Instruments.
