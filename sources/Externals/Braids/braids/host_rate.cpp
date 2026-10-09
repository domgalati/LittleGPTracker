// LittleGPTracker addition, not part of Mutable Instruments' Braids.
// Licensed under the GPLv3 like the rest of LittleGPTracker.

#include "braids/host_rate.h"

#include <math.h>

namespace braids {

static const int32_t kBraidsSampleRate = 96000;

HostRate host_rate = { kBraidsSampleRate, 0, 65536, 65536, 1.0 };

void SetHostSampleRate(int32_t rate) {
  if (rate <= 0) {
    rate = kBraidsSampleRate;
  }
  double ratio = double(kBraidsSampleRate) / rate;
  host_rate.rate = rate;
  host_rate.ratio = ratio;
  host_rate.pitch_offset = int16_t(floor(
      12.0 * 128.0 * log(ratio) / log(2.0) + 0.5));
  host_rate.ratio_q16 = uint32_t(floor(ratio * 65536.0 + 0.5));
  host_rate.inv_ratio_q16 = uint32_t(floor(65536.0 / ratio + 0.5));
}

uint32_t ScaleDecay(uint32_t decay, uint32_t one) {
  if (host_rate.rate == kBraidsSampleRate || decay == 0 || decay >= one) {
    return decay;
  }
  double d = pow(double(decay) / one, host_rate.ratio);
  return uint32_t(floor(d * one + 0.5));
}

}  // namespace braids
