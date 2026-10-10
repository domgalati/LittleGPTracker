// Bit-identity check: renders every Braids model at 96 kHz (several notes,
// parameter sets and strikes) and prints one checksum per model. Built twice by
// the Makefile: against pristine upstream (commit f43d554) and against the
// current tree (-DPATCHED). At 96 kHz the two outputs must be identical.
#include "braids/macro_oscillator.h"
#ifdef PATCHED
#include "braids/host_rate.h"
#endif
#include <stdio.h>
#include <string.h>

int main() {
#ifdef PATCHED
  braids::SetHostSampleRate(96000);
#endif
  static braids::MacroOscillator osc;
  uint8_t sync[24];
  memset(sync, 0, sizeof(sync));
  int16_t buf[24];
  const int notes[5] = { 24, 48, 69, 96, 120 };
  unsigned long long total = 1469598103934665603ULL;
  for (int s = 0; s < braids::MACRO_OSC_SHAPE_LAST; s++) {
    unsigned long long h = 1469598103934665603ULL;  // FNV-1a
    osc.Init();
    osc.set_shape(braids::MacroOscillatorShape(s));
    for (int n = 0; n < 5; n++) for (int pv = 0; pv < 3; pv++) {
      osc.set_pitch(notes[n] << 7);
      osc.Strike();
      for (int b = 0; b < 2000; b++) {  // 0.5 s per setting
        osc.set_parameters(pv * 16000 + (b & 7) * 30, 32767 - pv * 16000);
        osc.Render(sync, buf, 24);
        for (int i = 0; i < 24; i++) {
          h ^= (unsigned short)buf[i];
          h *= 1099511628211ULL;
        }
      }
    }
    printf("%2d %016llx\n", s, h);
    total ^= h;
  }
  printf("all %016llx\n", total);
  return 0;
}
