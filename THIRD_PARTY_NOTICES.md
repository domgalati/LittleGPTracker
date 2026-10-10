# Third-party notices

LittleGPTracker is licensed under the GPLv3 (see [LICENSE](LICENSE)). It
includes the third-party code listed below, which keeps its own license.

## Braids and stmlib (Émilie Gillet, github.com/pichenettes/eurorack, MIT license)

Used by the PIGTAIL instrument (`sources/Application/Instruments/PigtailInstrument.cpp`)
as its oscillator.

| Component | Upstream | Commit | Location in this repo |
| --- | --- | --- | --- |
| Braids | https://github.com/pichenettes/eurorack (`braids/`) | `08460a69a7e1f7a81c5a2abcc7189c9a6b7208d4` | `sources/Externals/Braids/braids/` |
| stmlib | https://github.com/pichenettes/stmlib (eurorack submodule) | `e3bd7c9cc00e4364166f9905c0509b6ffd0535ec` | `sources/Externals/Braids/stmlib/` |

Files taken from Braids: `macro_oscillator`, `analog_oscillator`,
`digital_oscillator`, `resources` (`.h` and `.cpp`), `parameter_interpolation.h`,
`excitation.h`, `svf.h`, `settings.h`.

Files taken from stmlib: `stmlib.h`, `utils/dsp.h`, `utils/random.h`,
`utils/random.cpp`.

Changes from upstream (every file keeps its original copyright and license
header):

- Source files renamed from `.cc` to `.cpp` to match this project's build rules.
- Braids is built for 96kHz; LittleGPTracker runs it at the audio driver's rate.
  `braids/host_rate.h` and `braids/host_rate.cpp` are LittleGPTracker additions
  (GPLv3, not part of the upstream code) holding the host rate. Rate dependent
  constants in `digital_oscillator.cpp`, `digital_oscillator.h`,
  `analog_oscillator.cpp` and `svf.h` are rescaled from it; each change is
  marked with an `LGPT:` comment. At 96kHz the patched code renders
  bit-identically to upstream.
- Two upstream bug fixes, also marked `LGPT:` in `digital_oscillator.cpp`: an
  out of bounds read of `wave_line` at maximum timbre (WAVE LINE model), and a
  negative shift in `ComputeDelay` for negative pitches.
- All other ported files are unmodified.

Copyright 2012-2013 Emilie Gillet. Released under the MIT License:

```
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```
