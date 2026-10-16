// pt_lab: measurement harness for the PIGTAIL instrument (Braids port).
//
// Links against the X64 build's objects (projects/buildX64), so it measures
// exactly what ships. Not part of the default build: see tools/pt_lab/Makefile.
//
// Modes:
//   audit   96 kHz native Braids (reference) vs PIGTAIL at 44.1 kHz, C3,
//           timbre/color 80/80 and 30/D0, 5 RNG seeds: spectral centroid
//           shift, semitone-band distance, decay time ratio, event rate ratio
//   tune    YIN pitch sweep over the pitched models, C1-C7, 44.1 and 48 kHz,
//           timbre/color 0/128/255, judged against the 96 kHz reference
//           ("tune raw" also prints every cell)
//   init    every model from a fresh voice: peak and RMS, flags silence
//   cpu     per model render cost, one voice, 10 s of audio at 44.1 kHz
//   stress  shape switching on a held note and on 8 channels with random
//           parameters, notes and block sizes (build with sanitizers)
//   cmds    TIMB, COLR and VOLM: aa=00 against the screen field, ramp timing,
//           what a note with and without instrument number keeps
//   nocmd   a hash per shape with no commands, to compare across builds

#include "Application/Instruments/InstrumentBank.h"
#include "Application/Instruments/PigtailInstrument.h"
#include "Application/Instruments/CommandList.h"
#include "Application/Player/SyncMaster.h"
#include "Services/Audio/Audio.h"
#include "Adapters/Unix/FileSystem/UnixFileSystem.h"
#include "Adapters/LINUX/System/LINUXSystem.h"
#include "braids/macro_oscillator.h"
#include "braids/host_rate.h"
#include "stmlib/utils/random.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#include <math.h>
#include <complex>
#include <vector>
#include <algorithm>
#include <string>

typedef std::complex<double> cpx;

static int gRate = 44100;
class TestAudio : public Audio {
public:
  TestAudio(AudioSettings &s) : Audio(s) {}
  virtual void Init() {}
  virtual void Close() {}
  virtual int GetSampleRate() { return gRate; }
};

static const int kNumShapes = 48;
static const char *kNames[kNumShapes] = {
  "csaw","morph","saw/sq","fold","buzz","sub sq","sub saw","sync sq","sync saw",
  "saw x3","sq x3","tri x3","sine x3","ring","swarm","saw comb","toy",
  "zlpf","zpkf","zbpf","zhpf","vosim","vowel","vow fof","harmonic",
  "fm","fb fm","wt fm","pluck","bowed","blown","fluted",
  "bell","drum","kick","cymbal","snare","wtbl","wmap","wline","wt x4",
  "noise","twin q","clk noise","cloud","particle","qpsk","qmark" };

// Models with no clear fundamental, skipped by the tuning sweep
static bool unpitched(int s) {
  static const char *list[] = { "bell","drum","kick","cymbal","snare","noise",
    "twin q","clk noise","cloud","particle","qpsk","qmark" };
  for (size_t i = 0; i < sizeof(list) / sizeof(list[0]); i++)
    if (!strcmp(kNames[s], list[i])) return true;
  return false;
}

// ------------------------------------------------------------- rendering

static PigtailInstrument *gPig = 0;

static void setParams(int shape, int t, int c, int vol) {
  gPig->FindVariable(PTIP_SHAPE)->SetInt(shape);
  gPig->FindVariable(PTIP_TIMBRE)->SetInt(t);
  gPig->FindVariable(PTIP_COLOR)->SetInt(c);
  gPig->FindVariable(PTIP_VOLUME)->SetInt(vol);
}

// PIGTAIL path, mono, in int16 units (volume FF compensated)
static std::vector<double> renderPigtail(int ch, int shape, int note, int t, int c,
                                         double secs, int rate) {
  gRate = rate;
  static fixed tmp[2 * 64];
  // Switch through another shape first so the model starts from Init()
  setParams((shape + 1) % kNumShapes, t, c, 0xFF);
  gPig->Start(ch, note, 1);
  gPig->Render(ch, tmp, 24, 1);
  gPig->Render(ch, tmp, 24, 1);
  setParams(shape, t, c, 0xFF);
  gPig->Start(ch, note, 1);
  int n = int(secs * rate);
  std::vector<double> out;
  out.reserve(n);
  static fixed buf[2 * 1024];
  int k = 0;
  while ((int)out.size() < n) {
    int b = 798 + (k++ % 3);                 // uneven, LGPT-like block sizes
    if ((int)out.size() + b > n) b = n - out.size();
    gPig->Render(ch, buf, b, 1);
    for (int i = 0; i < b; i++) out.push_back(double(buf[2 * i]) / 32768.0 * 256.0 / 255.0);
  }
  return out;
}

// Native Braids at 96 kHz (upstream behaviour), same 15 Hz DC removal
static braids::MacroOscillator gNative;
static std::vector<double> renderNative(int shape, int note, int t, int c, double secs) {
  braids::SetHostSampleRate(96000);
  gNative.Init();
  gNative.set_shape(braids::MacroOscillatorShape((shape + 1) % kNumShapes));
  gNative.set_shape(braids::MacroOscillatorShape(shape));
  gNative.set_pitch(int16_t(note << 7));
  gNative.set_parameters(int16_t(t << 7), int16_t(c << 7));
  gNative.Strike();
  int n = int(secs * 96000);
  std::vector<double> out;
  out.reserve(n);
  int16_t block[24];
  uint8_t sync[24];
  memset(sync, 0, sizeof(sync));
  double pole = exp(-2 * M_PI * 15.0 / 96000), x1 = 0, y1 = 0;
  while ((int)out.size() < n) {
    gNative.set_pitch(int16_t(note << 7));
    gNative.set_parameters(int16_t(t << 7), int16_t(c << 7));
    gNative.Render(sync, block, 24);
    for (int i = 0; i < 24 && (int)out.size() < n; i++) {
      double y = block[i] - x1 + pole * y1;
      x1 = block[i];
      y1 = y;
      out.push_back(y);
    }
  }
  braids::SetHostSampleRate(gRate);
  return out;
}

// ------------------------------------------------------------- analysis

static void fft(std::vector<cpx> &a) {
  int n = a.size();
  for (int i = 1, j = 0; i < n; i++) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(a[i], a[j]);
  }
  for (int len = 2; len <= n; len <<= 1) {
    double ang = -2 * M_PI / len;
    cpx wl(cos(ang), sin(ang));
    for (int i = 0; i < n; i += len) {
      cpx w(1);
      for (int j = 0; j < len / 2; j++) {
        cpx u = a[i + j], v = a[i + j + len / 2] * w;
        a[i + j] = u + v;
        a[i + j + len / 2] = u - v;
        w *= wl;
      }
    }
  }
}

// Welch power spectrum over [t0, t1), Hann windows, 50% overlap
static std::vector<double> welch(const std::vector<double> &x, int rate, double t0, double t1, int N) {
  std::vector<double> p(N / 2 + 1, 0);
  int frames = 0;
  int a = int(t0 * rate), b = std::min<int>(int(t1 * rate), x.size());
  for (int s = a; s + N <= b; s += N / 2) {
    std::vector<cpx> f(N);
    for (int i = 0; i < N; i++) f[i] = x[s + i] * (0.5 - 0.5 * cos(2 * M_PI * i / (N - 1)));
    fft(f);
    for (int i = 0; i <= N / 2; i++) p[i] += norm(f[i]);
    frames++;
  }
  if (frames) for (size_t i = 0; i < p.size(); i++) p[i] /= frames;
  return p;
}

static double centroid(const std::vector<double> &p, int rate, int N, double lo, double hi) {
  double num = 0, den = 0;
  for (size_t i = 0; i < p.size(); i++) {
    double f = double(i) * rate / N;
    if (f < lo || f > hi) continue;
    num += f * p[i];
    den += p[i];
  }
  return den > 0 ? num / den : 0;
}

// Semitone band levels (dB re in-band total), smoothed over +-2 bands
static std::vector<double> semitoneBands(const std::vector<double> &p, int rate, int N, double lo, int count) {
  std::vector<double> e(count, 1e-30);
  double tot = 0;
  for (size_t i = 0; i < p.size(); i++) {
    double f = double(i) * rate / N;
    if (f < lo) continue;
    int b = int(floor(12 * log(f / lo) / log(2.0)));
    if (b < 0 || b >= count) continue;
    e[b] += p[i];
    tot += p[i];
  }
  std::vector<double> db(count), sm(count);
  for (int b = 0; b < count; b++) db[b] = 10 * log10(e[b] / (tot + 1e-30));
  for (int b = 0; b < count; b++) {
    double s = 0;
    int n = 0;
    for (int k = -2; k <= 2; k++)
      if (b + k >= 0 && b + k < count) { s += pow(10, db[b + k] / 10); n++; }
    sm[b] = 10 * log10(s / n + 1e-30);
  }
  return sm;
}

// Mean absolute band difference (dB) at zero shift
static double bandDistance(const std::vector<double> &a, const std::vector<double> &b) {
  double s = 0;
  int m = 0;
  for (size_t i = 0; i < a.size(); i++) {
    if (a[i] < -50 && b[i] < -50) continue;
    s += fabs(std::max(a[i], -60.0) - std::max(b[i], -60.0));
    m++;
  }
  return m ? s / m : 0;
}

// RMS envelope (dB), 5 ms frames
static std::vector<double> envelope(const std::vector<double> &x, int rate) {
  int w = rate / 200;
  std::vector<double> e;
  for (size_t s = 0; s + w <= x.size(); s += w) {
    double acc = 0;
    for (int i = 0; i < w; i++) acc += x[s + i] * x[s + i];
    e.push_back(10 * log10(acc / w + 1e-9));
  }
  return e;
}

// Seconds from the envelope peak until it drops `drop` dB below it; -1 if never
static double decayTime(const std::vector<double> &e, double drop) {
  int pk = 0;
  for (size_t i = 0; i < e.size(); i++) if (e[i] > e[pk]) pk = i;
  for (size_t i = pk; i < e.size(); i++) if (e[i] < e[pk] - drop) return (i - pk) * 0.005;
  return -1;
}

// Onsets per second: envelope rising 10 dB above its lower quartile
static double eventRate(const std::vector<double> &e) {
  if (e.size() < 4) return 0;
  std::vector<double> s(e);
  std::sort(s.begin(), s.end());
  double floor_ = s[s.size() / 4];
  int count = 0;
  bool high = false;
  for (size_t i = 0; i < e.size(); i++) {
    if (!high && e[i] > floor_ + 10) { count++; high = true; }
    else if (high && e[i] < floor_ + 5) high = false;
  }
  return count / (e.size() * 0.005);
}

// YIN period estimate with an FFT difference function
static double yinPeriod(const std::vector<double> &x, int start, int W, int tauMax) {
  if (start + W + tauMax > (int)x.size()) return -1;
  int N = 1;
  while (N < 2 * (W + tauMax)) N <<= 1;
  std::vector<cpx> A(N), B(N);
  for (int i = 0; i < W; i++) A[i] = x[start + i];
  for (int i = 0; i < W + tauMax; i++) B[i] = x[start + i];
  fft(A);
  fft(B);
  for (int i = 0; i < N; i++) A[i] = conj(conj(A[i]) * B[i]);
  fft(A);  // inverse via conjugation
  std::vector<double> r(tauMax + 1), e2(W + tauMax + 1, 0), d(tauMax + 1), dn(tauMax + 1);
  for (int t = 0; t <= tauMax; t++) r[t] = A[t].real() / N;
  for (int i = 0; i < W + tauMax; i++) e2[i + 1] = e2[i] + x[start + i] * x[start + i];
  for (int t = 0; t <= tauMax; t++) d[t] = (e2[W] - e2[0]) + (e2[t + W] - e2[t]) - 2 * r[t];
  dn[0] = 1;
  double run = 0;
  for (int t = 1; t <= tauMax; t++) {
    run += d[t];
    dn[t] = run > 0 ? d[t] * t / run : 1;
  }
  int tau = -1;
  for (int t = 2; t < tauMax; t++) {
    if (dn[t] < 0.15) {
      while (t + 1 < tauMax && dn[t + 1] < dn[t]) t++;
      tau = t;
      break;
    }
  }
  if (tau < 0) {
    double m = 1e300;
    for (int t = 2; t < tauMax; t++) if (dn[t] < m) { m = dn[t]; tau = t; }
    if (m > 0.5) return -1;
  }
  double a = dn[tau - 1], b = dn[tau], c = dn[tau + 1], den = a - 2 * b + c;
  return tau + (den != 0 ? 0.5 * (a - c) / den : 0);
}

// Refine f0 with harmonic-weighted, parabolically interpolated FFT peaks
static double refinePitch(const std::vector<double> &x, int rate, double f0, double t0, double t1) {
  int a = int(t0 * rate), b = std::min<int>(int(t1 * rate), x.size()), L = b - a, N = 1;
  while (N < 4 * L) N <<= 1;
  std::vector<cpx> f(N);
  for (int i = 0; i < L; i++) f[i] = x[a + i] * (0.5 - 0.5 * cos(2 * M_PI * i / (L - 1)));
  fft(f);
  double num = 0, den = 0;
  for (int h = 1; h <= 8; h++) {
    double fh = f0 * h;
    if (fh > rate * 0.45) break;
    int lo = int(fh * 0.98 * N / rate), hi = int(fh * 1.02 * N / rate) + 1, pk = lo;
    for (int i = lo; i <= hi; i++) if (abs(f[i]) > abs(f[pk])) pk = i;
    if (pk <= 0 || pk >= N / 2) continue;
    double l = log(abs(f[pk - 1]) + 1e-12), m = log(abs(f[pk]) + 1e-12), r = log(abs(f[pk + 1]) + 1e-12);
    double dd = l - 2 * m + r, off = dd != 0 ? 0.5 * (l - r) / dd : 0;
    num += abs(f[pk]) * (pk + off) * rate / N / h;
    den += abs(f[pk]);
  }
  return den > 0 ? num / den : f0;
}

static double measureF0(const std::vector<double> &x, int rate, double t0, double t1) {
  int W = 4096, tauMax = rate / 25;
  std::vector<double> periods;
  for (double t = t0; t + double(W + tauMax) / rate < t1; t += 0.1) {
    double p = yinPeriod(x, int(t * rate), W, tauMax);
    if (p > 0) periods.push_back(p);
  }
  if (periods.empty()) return -1;
  std::sort(periods.begin(), periods.end());
  return refinePitch(x, rate, rate / periods[periods.size() / 2], t0, t1);
}

static const double kSilent = 1e9, kNoPeriod = 2e9;

// Cents vs the requested note (analysis window 0.3-1.2 s)
static double centsOf(const std::vector<double> &x, int rate, int note) {
  int a = int(0.3 * rate);
  double rms = 0;
  for (size_t i = a; i < x.size(); i++) rms += x[i] * x[i];
  rms = sqrt(rms / (x.size() - a));
  if (rms < 10) return kSilent;
  double f = measureF0(x, rate, 0.3, 1.2);
  if (f <= 0) return kNoPeriod;
  return 1200 * log(f / (440.0 * pow(2.0, (note - 69) / 12.0))) / log(2.0);
}

struct Stat {
  std::vector<double> v;
  void add(double x) { v.push_back(x); }
  double mean() const {
    double s = 0;
    for (size_t i = 0; i < v.size(); i++) s += v[i];
    return v.empty() ? 0 : s / v.size();
  }
  double sd() const {
    double m = mean(), s = 0;
    for (size_t i = 0; i < v.size(); i++) s += (v[i] - m) * (v[i] - m);
    return v.size() > 1 ? sqrt(s / (v.size() - 1)) : 0;
  }
  double median() const {
    if (v.empty()) return -1;
    std::vector<double> w(v);
    std::sort(w.begin(), w.end());
    return w[w.size() / 2];
  }
};

// ------------------------------------------------------------- modes

static void modeAudit() {
  int tcs[2][2] = { {0x80, 0x80}, {0x30, 0xD0} };
  printf("%-9s %-7s | %-13s | %5s | %-12s | %5s | %5s | %s\n", "model", "t/c", "dSt mean+-sd",
         "dist", "seed-spread", "T20r", "evR", "verdict");
  int off = 0;
  for (int s = 0; s < kNumShapes; s++) {
    for (int k = 0; k < 2; k++) {
      int t = tcs[k][0], c = tcs[k][1];
      Stat dst, dist, tr, er;
      std::vector<double> natC;
      for (int seed = 1; seed <= 5; seed++) {
        stmlib::Random::Seed(seed * 7919);
        std::vector<double> nat = renderNative(s, 48, t, c, 3.0);
        stmlib::Random::Seed(seed * 7919);
        std::vector<double> pig = renderPigtail(s % 8, s, 48, t, c, 3.0, 44100);
        std::vector<double> pn = welch(nat, 96000, 0.05, 0.55, 16384);
        std::vector<double> pp = welch(pig, 44100, 0.05, 0.55, 8192);
        double cn = centroid(pn, 96000, 16384, 50, 16000), cp = centroid(pp, 44100, 8192, 50, 16000);
        natC.push_back(cn);
        dst.add(12 * log(cp / cn) / log(2.0));
        dist.add(bandDistance(semitoneBands(pn, 96000, 16384, 150, 84),
                              semitoneBands(pp, 44100, 8192, 150, 84)));
        std::vector<double> en = envelope(nat, 96000), ep = envelope(pig, 44100);
        double tn = decayTime(en, 20), tp = decayTime(ep, 20);
        if (tn > 0.02 && tp > 0) tr.add(tp / tn);
        double vn = eventRate(en), vp = eventRate(ep);
        if (vn > 1.0 && vp > 0) er.add(vp / vn);
      }
      double cmin = *std::min_element(natC.begin(), natC.end());
      double cmax = *std::max_element(natC.begin(), natC.end());
      double spread = 12 * log(cmax / cmin) / log(2.0);
      bool bad = fabs(dst.mean()) > std::max(1.5, spread) || dist.mean() > 3.0 ||
                 (tr.median() > 0 && (tr.median() < 0.75 || tr.median() > 1.33)) ||
                 (er.median() > 0 && (er.median() < 0.7 || er.median() > 1.4));
      if (bad) off++;
      char trs[16] = "  -  ", ers[16] = "  -  ";
      if (tr.median() > 0) sprintf(trs, "%5.2f", tr.median());
      if (er.median() > 0) sprintf(ers, "%5.2f", er.median());
      printf("%-9s t%02X/c%02X | %+5.1f +- %4.1f | %5.1f | %4.1f st     | %s | %s | %s\n", kNames[s],
             t, c, dst.mean(), dst.sd(), dist.mean(), spread, trs, ers, bad ? "OFF" : "ok");
    }
  }
  printf("flagged OFF: %d of %d model/setting rows\n", off, 2 * kNumShapes);
}

static void modeTune(bool raw) {
  const int notes[7] = { 24, 36, 48, 60, 72, 84, 96 };
  const char *oct[7] = { "C1", "C2", "C3", "C4", "C5", "C6", "C7" };
  const int rates[2] = { 44100, 48000 };
  const int vals[3] = { 0, 128, 255 };
  printf("skipped (unpitched):");
  for (int s = 0; s < kNumShapes; s++) if (unpitched(s)) printf(" %s", kNames[s]);
  printf("\n");
  std::vector<double> all;
  printf("| model | C1 | C2 | C3 | C4 | C5 | C6 | C7 | >5c | octave |\n|---|---|---|---|---|---|---|---|---|---|\n");
  for (int s = 0; s < kNumShapes; s++) {
    if (unpitched(s)) continue;
    double worst[7];
    bool any[7];
    for (int i = 0; i < 7; i++) { worst[i] = 0; any[i] = false; }
    int over = 0, flips = 0;
    for (int ti = 0; ti < 3; ti++) for (int ci = 0; ci < 3; ci++) {
      double nat[7];
      for (int n = 0; n < 7; n++) {
        stmlib::Random::Seed(4242);
        nat[n] = centsOf(renderNative(s, notes[n], vals[ti], vals[ci], 1.2), 96000, notes[n]);
      }
      for (int r = 0; r < 2; r++) {
        if (raw) printf("RAW %s|%d|%d|%d", kNames[s], rates[r], vals[ti], vals[ci]);
        for (int n = 0; n < 7; n++) {
          stmlib::Random::Seed(4242);
          double h = centsOf(renderPigtail(s % 8, s, notes[n], vals[ti], vals[ci], 1.2, rates[r]),
                             rates[r], notes[n]);
          if (raw) {
            if (h >= kSilent) printf("|%s", h == kSilent ? "silent" : "none");
            else printf("|%.2f", h);
          }
          // Only cells where the reference has a clean fundamental at the note
          if (nat[n] >= kSilent || fabs(nat[n]) > 5) continue;
          if (h >= kSilent) { over++; continue; }
          double e = fabs(h);
          all.push_back(e);
          if (e > 600) flips++;
          else {
            if (e > 5) over++;
            worst[n] = std::max(worst[n], e);
            any[n] = true;
          }
        }
        if (raw) {
          printf("#");
          for (int n = 0; n < 7; n++) {
            if (nat[n] >= kSilent) printf("|%s", nat[n] == kSilent ? "silent" : "none");
            else printf("|%.2f", nat[n]);
          }
          printf("\n");
        }
      }
    }
    printf("| %s |", kNames[s]);
    for (int n = 0; n < 7; n++) {
      if (any[n]) printf(" %.1f |", worst[n]);
      else printf(" - |");
    }
    printf(" %d | %d |\n", over, flips);
    fflush(stdout);
  }
  std::sort(all.begin(), all.end());
  int over5 = 0, fl = 0;
  for (size_t i = 0; i < all.size(); i++) {
    if (all[i] > 600) fl++;
    else if (all[i] > 5) over5++;
  }
  if (!all.empty())
    printf("clean cells n=%d median %.2fc p95 %.2fc; >5c (same octave) %d; octave flips %d\n",
           (int)all.size(), all[all.size() / 2], all[int(all.size() * 0.95)], over5, fl);
  (void)oct;
}

static void modeInit() {
  int silent = 0;
  for (int s = 0; s < kNumShapes; s++) {
    std::vector<double> x = renderPigtail(s % 8, s, 60, 0x80, 0x80, 1.0, 44100);
    double pk = 0, rms = 0;
    for (size_t i = 0; i < x.size(); i++) { pk = std::max(pk, fabs(x[i])); rms += x[i] * x[i]; }
    rms = sqrt(rms / x.size());
    if (rms < 30) silent++;
    printf("%-9s peak %7.0f  rms %6.0f (%5.1f dBFS)%s\n", kNames[s], pk, rms,
           20 * log10(rms / 32768 + 1e-12), rms < 30 ? "  <-- SILENT" : "");
  }
  printf("silent models: %d of %d (volume FF, C4, timbre/color 80)\n", silent, kNumShapes);
}

static void modeCpu() {
  gRate = 44100;
  static fixed buf[2 * 1024];
  std::vector<std::pair<double, int> > cost;
  for (int s = 0; s < kNumShapes; s++) {
    setParams(s, 0x80, 0x80, 0x40);
    gPig->Start(0, 60, 1);
    for (int i = 0; i < 4; i++) gPig->Render(0, buf, 799, 1);
    int frames = 0;
    clock_t t0 = clock();
    while (frames < 10 * 44100) { gPig->Render(0, buf, 799, 1); frames += 799; }
    double secs = double(clock() - t0) / CLOCKS_PER_SEC;
    cost.push_back(std::make_pair(100 * secs / (frames / 44100.0), s));
  }
  for (int s = 0; s < kNumShapes; s++)
    printf("%-9s %6.2f%% of real time per voice\n", kNames[cost[s].second], cost[s].first);
  std::sort(cost.begin(), cost.end());
  printf("heaviest:");
  for (int i = kNumShapes - 1; i >= kNumShapes - 6; i--)
    printf(" %s (%.2f%%)", kNames[cost[i].second], cost[i].first);
  printf("\nlightest: %s (%.2f%%)\n", kNames[cost[0].second], cost[0].first);
}

static void modeStress() {
  gRate = 44100;
  static fixed buf[2 * 4000];
  srand(1234);
  double worst = 0;
  setParams(0, 0x80, 0x80, 0x40);
  gPig->Start(0, 60, 1);
  long long samples = 0;
  for (int pass = 0; pass < 6; pass++) for (int s = 0; s < kNumShapes; s++) {
    gPig->FindVariable(PTIP_SHAPE)->SetInt((pass & 1) ? kNumShapes - 1 - s : s);
    int b = 1 + rand() % 1500;
    gPig->Render(0, buf, b, 1);
    for (int i = 0; i < 2 * b; i++) worst = std::max(worst, fabs(double(buf[i])));
    samples += b;
  }
  printf("held-note switching: %d shape changes, %lld samples, max |out| %.0f\n",
         6 * kNumShapes, samples, worst / 32768);
  for (int ch = 0; ch < 8; ch++) gPig->Start(ch, 36 + 5 * ch, 1);
  int frames = 0, changes = 0;
  while (frames < 60 * 44100) {
    int b = 1 + rand() % 2000;
    for (int ch = 0; ch < 8; ch++) {
      if (rand() % 4 == 0) { gPig->FindVariable(PTIP_SHAPE)->SetInt(rand() % kNumShapes); changes++; }
      if (rand() % 6 == 0) gPig->FindVariable(PTIP_TIMBRE)->SetInt(rand() % 256);
      if (rand() % 6 == 0) gPig->FindVariable(PTIP_COLOR)->SetInt(rand() % 256);
      if (rand() % 10 == 0) gPig->Start(ch, rand() % 128, rand() % 2);
      gPig->Render(ch, buf, b, rand() % 4);
      for (int i = 0; i < 2 * b; i++) worst = std::max(worst, fabs(double(buf[i])));
    }
    frames += b;
  }
  printf("8-channel random switching: %d shape changes over %.0f s of audio per channel, max |out| %.0f\n",
         changes, frames / 44100.0, worst / 32768);
  gPig->FindVariable(PTIP_SHAPE)->SetString("not-a-shape");
  printf("unknown saved name -> index %d (%s)\n", gPig->FindVariable(PTIP_SHAPE)->GetInt(),
         gPig->FindVariable(PTIP_SHAPE)->GetString());
  gPig->FindVariable(PTIP_SHAPE)->SetInt(999);
  gPig->Render(0, buf, 799, 1);
  gPig->FindVariable(PTIP_SHAPE)->SetInt(-5);
  gPig->Render(0, buf, 799, 1);
  printf("index 999 and -5 rendered without fault\n");
}

// ------------------------------------------------------------- commands
// TIMB, COLR and VOLM (docs/design/pigtail-commands.md). Each run is a
// script played in a forked child, so every run starts from fresh voices and
// the same RNG state, and two runs can be compared sample for sample.

enum { SC_START, SC_RENDER, SC_CMD, SC_FIELD, SC_STOP, SC_TEMPO };
struct ScriptStep { int kind, ch, a, b; };

struct Script {
  int ch, shape, t, c, vol;
  uint32_t seed;
  std::vector<ScriptStep> steps;
  Script(int shape_, int t_, int c_, int vol_, uint32_t seed_ = 1)
    : ch(0), shape(shape_), t(t_), c(c_), vol(vol_), seed(seed_) {}
  Script &add(int kind, int ch_, int a, int b) {
    ScriptStep s = { kind, ch_, a, b };
    steps.push_back(s);
    return *this;
  }
  Script &start(int note, int flags) { return add(SC_START, ch, note, flags); }
  Script &render(int frames) { return add(SC_RENDER, ch, frames, 0); }
  Script &cmd(FourCC cc, int value) { return add(SC_CMD, ch, int(cc), value); }
  Script &cmdOn(int ch_, FourCC cc, int value) { return add(SC_CMD, ch_, int(cc), value); }
  Script &field(FourCC id, int value) { return add(SC_FIELD, ch, int(id), value); }
  Script &stop() { return add(SC_STOP, ch, 0, 0); }
  Script &tempo(int bpm) { return add(SC_TEMPO, ch, bpm, 0); }
};

// Left channel of every rendered frame, in int16 units (exact: i2fp is a shift)
static void playScript(const Script &s, std::vector<int> &out) {
  static fixed buf[2 * 1024];
  static const int sizes[] = { 7, 100, 799, 24, 1, 333, 800 };
  int k = 0;
  setParams(s.shape, s.t, s.c, s.vol);
  stmlib::Random::Seed(s.seed);
  for (size_t i = 0; i < s.steps.size(); i++) {
    const ScriptStep &st = s.steps[i];
    switch (st.kind) {
      case SC_START: gPig->Start(st.ch, st.a, st.b); break;
      case SC_STOP: gPig->Stop(st.ch); break;
      case SC_CMD: gPig->ProcessCommand(st.ch, FourCC(st.a), ushort(st.b)); break;
      case SC_FIELD: gPig->FindVariable(FourCC(st.a))->SetInt(st.b); break;
      case SC_TEMPO: SyncMaster::GetInstance()->SetTempo(st.a); break;
      case SC_RENDER:
        for (int left = st.a; left > 0;) {
          int b = std::min(left, sizes[k++ % 7]);
          gPig->Render(st.ch, buf, b, 0);
          for (int j = 0; j < b; j++) out.push_back(buf[2 * j] >> FIXED_SHIFT);
          left -= b;
        }
        break;
    }
  }
}

static std::vector<int> run(const Script &s) {
  int fd[2];
  if (pipe(fd) != 0) { perror("pipe"); exit(1); }
  fflush(stdout);
  pid_t pid = fork();
  if (pid == 0) {
    close(fd[0]);
    std::vector<int> out;
    playScript(s, out);
    const char *p = (const char *)&out[0];
    size_t left = out.size() * sizeof(int);
    while (left > 0) {
      ssize_t w = write(fd[1], p, left);
      if (w <= 0) _exit(1);
      p += w;
      left -= w;
    }
    _exit(0);
  }
  close(fd[1]);
  std::vector<int> out;
  int chunk[4096];
  std::string rest;
  for (;;) {
    ssize_t r = read(fd[0], chunk, sizeof(chunk));
    if (r <= 0) break;
    rest.append((const char *)chunk, r);
  }
  close(fd[0]);
  int status = 0;
  waitpid(pid, &status, 0);
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    printf("FAIL: script child died (status %d)\n", status);
    exit(1);
  }
  out.resize(rest.size() / sizeof(int));
  if (!out.empty()) memcpy(&out[0], rest.data(), out.size() * sizeof(int));
  return out;
}

static int gFails = 0, gChecks = 0;
static void check(bool ok, const char *what) {
  gChecks++;
  if (!ok) gFails++;
  printf("  %s: %s\n", ok ? "ok" : "FAIL", what);
}

// Same output, and not silence (a silent pair would prove nothing)
static void expectSame(const Script &a, const Script &b, const char *what) {
  std::vector<int> x = run(a), y = run(b);
  size_t diff = x.size() == y.size() ? x.size() : 0;
  for (size_t i = 0; i < x.size() && i < y.size(); i++)
    if (x[i] != y[i]) { diff = i; break; }
  bool same = x.size() == y.size() && diff == x.size();
  int peak = 0;
  for (size_t i = 0; i < x.size(); i++) peak = std::max(peak, abs(x[i]));
  char line[300];
  if (same) snprintf(line, sizeof(line), "%s (%d frames, peak %d)", what, int(x.size()), peak);
  else snprintf(line, sizeof(line), "%s: first difference at frame %d of %d", what, int(diff), int(x.size()));
  check(same && (peak > 0 || strstr(what, "silent")), line);
}

// The volume each 24-frame Braids block was scaled by, from a run at volume
// 256 (the raw block: (src*256)>>8 == src): -1 where the block can't tell
static std::vector<int> blockVolumes(const std::vector<int> &raw, const std::vector<int> &got) {
  std::vector<int> vols;
  for (size_t b = 0; (b + 1) * 24 <= got.size(); b++) {
    int found = -1, matches = 0;
    for (int v = 0; v < 256; v++) {
      bool ok = true;
      for (int i = 0; i < 24 && ok; i++) ok = ((raw[b * 24 + i] * v) >> 8) == got[b * 24 + i];
      if (ok) { found = v; matches++; }
    }
    vols.push_back(matches == 1 ? found : -1);
  }
  return vols;
}

static int tickBlocks(int aa) {
  return std::max(1, int(4 * SyncMaster::GetInstance()->GetTickSampleCount()) * aa / 24);
}

// A VOLM aabb ramp from `from`, with an optional note without instrument
// number at frame `restrikeAt` (0: none). Block k (1-based) must carry the
// straight line from `from` to bb (within 1), and exactly bb from block
// aa*4 ticks/24 on
static void checkRamp(int from, int aa, int bb, int bpm, int restrikeAt) {
  SyncMaster::GetInstance()->SetTempo(bpm);
  int blocks = tickBlocks(aa);
  int frames = (blocks + 40) * 24;
  Script raw(0, 0x80, 0x80, 256), ramp(0, 0x80, 0x80, from);
  raw.tempo(bpm).start(48, 1);
  ramp.tempo(bpm).start(48, 1).cmd(I_CMD_VOLM, (aa << 8) | bb);
  if (restrikeAt) {
    raw.render(restrikeAt).start(55, 0).render(frames - restrikeAt);
    ramp.render(restrikeAt).start(55, 0).render(frames - restrikeAt);
  } else {
    raw.render(frames);
    ramp.render(frames);
  }
  std::vector<int> vols = blockVolumes(run(raw), run(ramp));
  int unknown = 0, worst = 0, reached = -1;
  for (int k = 1; k <= (int)vols.size(); k++) {
    int v = vols[k - 1];
    if (v < 0) { unknown++; continue; }
    int want = k >= blocks ? bb : from + (bb - from) * k / blocks;
    worst = std::max(worst, abs(v - want));
    if (reached < 0 && v == bb) reached = k;
  }
  // Rising, the block before the last still reads below bb; falling, it may
  // already round down to it
  bool atEnd = vols[blocks - 1] == bb && (bb < from || blocks < 2 || vols[blocks - 2] != bb);
  char line[300];
  snprintf(line, sizeof(line),
           "VOLM %02X%02X from %02X at %d BPM%s: %d blocks expected, first at %02X: block %d, "
           "worst off-line %d, %d of %d blocks unreadable",
           aa, bb, from, bpm, restrikeAt ? ", note without instrument number midway" : "",
           blocks, bb, reached, worst, unknown, int(vols.size()));
  check(worst <= 1 && atEnd && unknown < int(vols.size()) / 20, line);
  SyncMaster::GetInstance()->SetTempo(120);
}

static void modeCmds() {
  gRate = 44100;
  SyncMaster::GetInstance()->SetTempo(120);
  char what[200];
  static const int shapes[] = { 0, 3, 14, 22, 28, 36, 44 };  // csaw fold swarm vowel pluck snare cloud
  static const FourCC cmds[] = { I_CMD_TIMB, I_CMD_COLR, I_CMD_VOLM };
  static const FourCC fields[] = { PTIP_TIMBRE, PTIP_COLOR, PTIP_VOLUME };
  static const char *names[] = { "TIMB", "COLR", "VOLM" };

  printf("aa=00 sounds like the screen field\n");
  for (size_t si = 0; si < sizeof(shapes) / sizeof(shapes[0]); si++)
    for (int p = 0; p < 3; p++)
      for (int seed = 1; seed <= 2; seed++) {
        int xx = seed == 1 ? 0x37 : 0xFF, other = 0xA0;
        int s = shapes[si];
        // From the note's start
        Script ref(s, 0x80, 0x80, 0x80, seed), cmd(s, 0x80, 0x80, 0x80, seed);
        ref.field(fields[p], xx).start(48, 1).render(9000);
        cmd.field(fields[p], other).start(48, 1).cmd(cmds[p], xx).render(9000);
        snprintf(what, sizeof(what), "%-6s %s 00%02X at the note's start, seed %d", kNames[s], names[p], xx, seed);
        expectSame(ref, cmd, what);
        // Midway, on a block boundary (2400 = 100 blocks)
        Script ref2(s, 0x80, 0x80, 0x80, seed), cmd2(s, 0x80, 0x80, 0x80, seed);
        ref2.field(fields[p], other).start(48, 1).render(2400).field(fields[p], xx).render(6000);
        cmd2.field(fields[p], other).start(48, 1).render(2400).cmd(cmds[p], xx).render(6000);
        snprintf(what, sizeof(what), "%-6s %s 00%02X midway, seed %d", kNames[s], names[p], xx, seed);
        expectSame(ref2, cmd2, what);
      }
  {
    Script cmd(0, 0x80, 0x80, 0x00);
    cmd.start(48, 1).cmd(I_CMD_VOLM, 0x0000).render(4800);
    Script silent(0, 0x80, 0x80, 0x00);
    silent.start(48, 1).render(4800);
    expectSame(silent, cmd, "VOLM 0000 from 00: silent");
  }

  printf("ramps land on time (VOLM, read back block by block)\n");
  checkRamp(0x00, 0x01, 0xFF, 120, 0);
  checkRamp(0x00, 0x04, 0xFF, 120, 0);
  checkRamp(0x00, 0x10, 0xFF, 120, 0);
  checkRamp(0xFF, 0x02, 0x00, 120, 0);
  checkRamp(0x30, 0x03, 0xC0, 150, 0);
  checkRamp(0x40, 0xFF, 0x41, 120, 0);  // the slowest, smallest move
  checkRamp(0x00, 0x04, 0xFF, 120, 4800);

  printf("notes without an instrument number keep the values; a clean start drops them\n");
  for (int p = 0; p < 3; p++) {
    Script ref(0, 0x80, 0x80, 0x80), cmd(0, 0x80, 0x80, 0x80);
    ref.field(fields[p], 0xE0).start(48, 1).render(2400).start(55, 0).render(4800);
    cmd.field(fields[p], 0x20).start(48, 1).cmd(cmds[p], 0xE0).render(2400).start(55, 0).render(4800);
    snprintf(what, sizeof(what), "%s 00E0, then a note without instrument number: kept", names[p]);
    expectSame(ref, cmd, what);
    Script ref2(0, 0x80, 0x80, 0x80), cmd2(0, 0x80, 0x80, 0x80);
    ref2.field(fields[p], 0xE0).start(48, 1).render(2400).stop().start(55, 0).render(4800);
    cmd2.field(fields[p], 0x20).start(48, 1).cmd(cmds[p], 0xE0).render(2400).stop().start(55, 0).render(4800);
    snprintf(what, sizeof(what), "%s 00E0, stop, then a note without instrument number: kept", names[p]);
    expectSame(ref2, cmd2, what);
    Script ref3(0, 0x80, 0x80, 0x80), cmd3(0, 0x80, 0x80, 0x80);
    ref3.field(fields[p], 0xE0).start(48, 1).render(2400).field(fields[p], 0x20).start(55, 1).render(4800);
    cmd3.field(fields[p], 0x20).start(48, 1).cmd(cmds[p], 0xE0).render(2400).start(55, 1).render(4800);
    snprintf(what, sizeof(what), "%s 00E0, then a clean start: back to the field", names[p]);
    expectSame(ref3, cmd3, what);
    // After the reset, the field is live again
    Script ref4(0, 0x80, 0x80, 0x80), cmd4(0, 0x80, 0x80, 0x80);
    ref4.field(fields[p], 0xE0).start(48, 1).render(2400).field(fields[p], 0x20).start(55, 1).render(2400)
        .field(fields[p], 0x90).render(2400);
    cmd4.field(fields[p], 0x20).start(48, 1).cmd(cmds[p], 0xE0).render(2400).start(55, 1).render(2400)
        .field(fields[p], 0x90).render(2400);
    snprintf(what, sizeof(what), "%s: after the clean start, screen edits are heard again", names[p]);
    expectSame(ref4, cmd4, what);
  }
  {
    // One command doesn't freeze the other fields
    Script ref(0, 0x80, 0x80, 0x80), cmd(0, 0x80, 0x80, 0x80);
    ref.field(PTIP_TIMBRE, 0x10).start(48, 1).render(2400).field(PTIP_COLOR, 0xF0).render(2400);
    cmd.start(48, 1).cmd(I_CMD_TIMB, 0x10).render(2400).field(PTIP_COLOR, 0xF0).render(2400);
    expectSame(ref, cmd, "after TIMB, the color field is still live");
    // While a command holds timbre, its field is ignored
    Script ref2(0, 0x80, 0x80, 0x80), cmd2(0, 0x80, 0x80, 0x80);
    ref2.field(PTIP_TIMBRE, 0x10).start(48, 1).render(4800);
    cmd2.start(48, 1).cmd(I_CMD_TIMB, 0x10).render(2400).field(PTIP_TIMBRE, 0xF0).render(2400);
    expectSame(ref2, cmd2, "after TIMB, timbre field edits are ignored");
  }

  printf("other channels and other commands\n");
  {
    Script ref(0, 0x80, 0x80, 0x80), cmd(0, 0x80, 0x80, 0x80);
    ref.start(48, 1).render(4800);
    cmd.start(48, 1).cmdOn(1, I_CMD_TIMB, 0x00E0).cmdOn(3, I_CMD_VOLM, 0x1000).render(4800);
    expectSame(ref, cmd, "commands to channels 1 and 3 leave channel 0 alone");
    Script cmd2(0, 0x80, 0x80, 0x80);
    cmd2.start(48, 1).cmd(I_CMD_FCUT, 0x0010).cmd(I_CMD_PTCH, 0x0105).cmd(I_CMD_ARPG, 0x0370).render(4800);
    expectSame(ref, cmd2, "FCUT, PTCH and ARPG do nothing");
  }

  printf("cmds: %d of %d checks passed\n", gChecks - gFails, gChecks);
  if (gFails) {
    fflush(stdout);
    _exit(1);
  }
}

// No commands: a hash per scenario, to compare with the build before them
static uint32_t fnv(const std::vector<int> &x) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < x.size(); i++) { h ^= uint32_t(x[i]); h *= 16777619u; }
  return h;
}

static void modeNoCmd() {
  gRate = 44100;
  SyncMaster::GetInstance()->SetTempo(120);
  for (int s = 0; s < kNumShapes; s++) {
    Script sc(s, 0x80, 0x80, 0x40, 1000 + s);
    sc.start(36 + s, 1).render(6000).field(PTIP_TIMBRE, 0x20).render(3001)
      .field(PTIP_COLOR, 0xE0).field(PTIP_VOLUME, 0xFF).render(4000).start(60, 0).render(5000)
      .field(PTIP_VOLUME, 0x10).render(777).start(43, 1).render(8000).stop().start(50, 0).render(3000);
    std::vector<int> out = run(sc);
    printf("%-9s %08x %d\n", kNames[s], fnv(out), int(out.size()));
  }
  // Several channels, interleaved
  Script multi(0, 0x60, 0xA0, 0x80, 77);
  for (int ch = 0; ch < 4; ch++) multi.add(SC_START, ch, 40 + 7 * ch, 1);
  for (int r = 0; r < 20; r++)
    for (int ch = 0; ch < 4; ch++) {
      multi.ch = ch;
      if (r == 7) multi.field(PTIP_SHAPE, 22);
      multi.render(311 + 50 * ch);
      if (r % 5 == 4) multi.start(45 + r, r & 1);
    }
  std::vector<int> out = run(multi);
  printf("4 channels %08x %d\n", fnv(out), int(out.size()));
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: pt_lab audit|tune [raw]|init|cpu|stress|cmds|nocmd\n");
    return 2;
  }
  System::Install(new LINUXSystem());
  FileSystem::Install(new UnixFileSystem());
  AudioSettings st;
  st.bufferSize_ = 1024;
  st.preBufferCount_ = 0;
  st.sampleRate_ = 44100;
  Audio::Install(new TestAudio(st));
  InstrumentBank *bank = new InstrumentBank();
  // Slots start empty: put a PIGTAIL in one
  gPig = (PigtailInstrument *)bank->Create(0x00, IT_PIGTAIL);
  std::string mode = argv[1];
  clock_t t0 = clock();
  if (mode == "audit") modeAudit();
  else if (mode == "tune") modeTune(argc > 2 && !strcmp(argv[2], "raw"));
  else if (mode == "init") modeInit();
  else if (mode == "cpu") modeCpu();
  else if (mode == "stress") modeStress();
  else if (mode == "cmds") modeCmds();
  else if (mode == "nocmd") modeNoCmd();
  else { fprintf(stderr, "unknown mode %s\n", argv[1]); return 2; }
  printf("(%s took %.1f s CPU)\n", mode.c_str(), double(clock() - t0) / CLOCKS_PER_SEC);
  fflush(stdout);
  _exit(0);  // skip static teardown of the app singletons
}
