# slot_lab fixtures

Inputs for `tools/slot_lab` (see its Makefile). Each directory holds an `lgptsav.dat` and a `samples/` folder; the WAVs are 512-frame square waves written by slot_lab. `make fixtures` regenerates all of them except `owner-midi`, a real save it leaves alone; the output is deterministic (checked by regenerating and comparing). Regenerate only when a definition changes, because the baselines in `../baseline/` were made from these files.

| Fixture | Made by | Contents | What it exercises |
|---|---|---|---|
| `up-basic` | upstream `c7258ff`, `slot_lab gen basic` | Sample slots 00-02 (from `AssignDefaults`), 40 and 7F with changed parameters; MIDI 80 (channel 9) and 8F changed, 85 left at its default; sampleless 03 and 7E referenced by a phrase; a row with an instrument but no note, and one with a note but no instrument | Upstream's own save output; slots at 7F, 80 and 8F |
| `up-clones` | upstream, `gen clones` | Upstream's `Clone()` of MIDI 80 and 81, which lands in 02 and 03 (`TYPE="Midi"` below 0x80), and of Sample 00 into 04, changed afterwards | MIDI clones in the sample range |
| `up-tables` | upstream, `gen tables` | Tables 00, 05 and 10 with commands; Sample 00 and MIDI 80 with table automation; phrase TABL, VOLM, PTCH, MDCC and GROV; groove 01; mixer bus, volume, HPF and LPF | Tables, automation, mixer |
| `owner-midi` | The owner, with the upstream 1.6.0-bacon20 X64 release (`lgpt_MIDI`, 2026-10-09) | No samples. Phrase 10 plays MIDI 80 (5 rows) and 83 (3 rows, channel 13, note length 17, volume 222) and a MIDI clone of 80 that landed in 00 (1 row). The project's MIDI device was saved as `Midi Through:0` | A real save with MIDI in use and a MIDI clone below 0x80. `samples/` holds only `.gitkeep`, so git keeps the folder |
| `hand-notype` | `up-basic` edited by `handmade.py` | No `TYPE` attributes; MIDI entries 81-8F removed while 85 and 8F stay referenced; `filter mode` of 00 and 01 saved under the old name `filter dist` (`none`, `scream`) | Files older than 2014: legacy slot ranges, missing MIDI entries, the old parameter name |
| `hand-v037` | `up-basic` edited | `VERSION="0.37"` | The volume/crushdrive swap below version 38 (02 has crush 8) |
| `hand-noversion` | `up-basic` edited | `PROJECT` without `VERSION` | Version 32: table ratio 2, plus the swap |
| `hand-missing-sample` | `up-basic` edited | Slot 7F names `gone.wav`, which isn't in `samples/` | A sample name the pool doesn't have |
| `hand-phrase90` | `up-basic` edited | Phrase 02 instrument bytes 90 (row 0), A0 (row 2, no note) and FE (row 8) | Phrase references past upstream's last slot (8F) |
| `hand-lz` | `slot_lab lz` of `up-basic` | LZ-compressed save | The loader's decompression path. The layout is a guess (an int holding the length including the NUL, then LZ data): nothing in this repository's history writes compressed saves |

Apart from `owner-midi`, real projects aren't stored here. The Makefile also runs every `lgpt_*` project of [djdiskmachine/lgpt-resources](https://github.com/djdiskmachine/lgpt-resources) at `9624b43` as `res-<project>`, from a clone outside this repository (`RESOURCES`, default `~/lgpt-resources`).
