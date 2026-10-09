// LittleGPTracker addition, not part of Mutable Instruments' Braids.
// Licensed under the GPLv3 like the rest of LittleGPTracker.
//
// Braids' tables and constants assume a 96kHz sample rate. LittleGPTracker
// runs Braids at the audio driver's rate and shifts pitch by
// 12*log2(96000/rate) semitones. That fixes oscillator pitch but not the
// models that use absolute frequencies, sample counted times or per sample
// decays. Those parts read the values below to rescale themselves.
//
// At 96kHz every value is an exact identity, so Braids renders exactly as
// upstream does.

#ifndef BRAIDS_HOST_RATE_H_
#define BRAIDS_HOST_RATE_H_

#include "stmlib/stmlib.h"

namespace braids {

struct HostRate {
  int32_t rate;            // host sample rate in Hz
  int16_t pitch_offset;    // 1/128 semitones, 12*128*log2(96000/rate)
  uint32_t ratio_q16;      // 96000/rate, Q16
  uint32_t inv_ratio_q16;  // rate/96000, Q16
  double ratio;            // 96000/rate
};

extern HostRate host_rate;

// Recomputes host_rate. Call when the host rate changes, not per block.
void SetHostSampleRate(int32_t rate);

// A per step multiplicative decay `decay/one`, raised to the power
// 96000/rate so it decays at the same speed in seconds. Returns `decay`
// unchanged at 96kHz.
uint32_t ScaleDecay(uint32_t decay, uint32_t one);

// A sample count or duration scaled by rate/96000 (at least 1).
inline uint32_t ScaleSamples(uint32_t samples) {
  uint32_t scaled = static_cast<uint32_t>(
      (static_cast<uint64_t>(samples) * host_rate.inv_ratio_q16) >> 16);
  return scaled ? scaled : 1;
}

// An increment, density or probability scaled by 96000/rate.
inline uint32_t ScaleRate(uint32_t value) {
  return static_cast<uint32_t>(
      (static_cast<uint64_t>(value) * host_rate.ratio_q16) >> 16);
}

}  // namespace braids

#endif  // BRAIDS_HOST_RATE_H_
