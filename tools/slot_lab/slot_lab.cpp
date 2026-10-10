// slot_lab: load/save harness for instrument slots and project files.
//
// One source, built twice (see Makefile), each time linked against one
// tree's own objects, so a dump shows what that tree's loader makes of a file:
//   SL_FORK     this repository (projects/buildX64)
//   SL_UPSTREAM djdiskmachine/LittleGPTracker, pinned commit
// SL_TREE names the tree in the dump header.
//
// Modes:
//   dump <projdir>           load as AppWindow::LoadProject does and print
//                            pool, project variables, every slot's type and
//                            variables, phrase references and data checksums
//   save <projdir> <outdir>  load, then write <outdir>/lgptsav.dat with the
//                            tree's own save code
//   xml <file> <out>         parse a save file and write it back out with
//                            TinyXML (decompresses an LZ save first, as the
//                            loader does), to compare files written by hand
//   gen <name> <projdir>     build fixture <name> (basic, clones, tables) with
//                            the tree's model code, write its samples and save
//   lz <in> <out>            LZ-compress a save file in the layout the loader
//                            expects (int length, then LZ data)
//   play <projdir> <scenario>
//                            load, edit the song in memory and play it through
//                            the real Player and mixer, pulsed from this loop
//                            (no audio thread, no device). Scenarios: range
//                            (phrase bytes 90-FE), clone (Clone onto slots a
//                            channel and a table still use). Meant for the
//                            ASan build (make play)
//
// Trace output goes to stderr; results go to stdout.

#include "Adapters/Dummy/Midi/DummyMidi.h"
#include "Adapters/Unix/FileSystem/UnixFileSystem.h"
#include "Adapters/Unix/Process/UnixProcess.h"
#include "Application/Instruments/CommandList.h"
#include "Application/Instruments/InstrumentBank.h"
#include "Application/Instruments/MidiInstrument.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Mixer/MixerService.h"
#include "Application/Model/Groove.h"
#include "Application/Model/Mixer.h"
#include "Application/Model/Project.h"
#include "Application/Model/Table.h"
#include "Application/Persistency/PersistencyService.h"
#include "Application/Player/Player.h"
#include "Application/Player/SyncMaster.h"
#include "Application/Player/TablePlayback.h"
#include "Application/Views/ViewData.h"
#include "Externals/Compression/lz.h"
#include "Foundation/Variables/WatchedVariable.h"
#include "Services/Audio/Audio.h"
#include "Services/Audio/AudioDriver.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"
#ifdef SL_FORK
#include "Application/Instruments/PigtailInstrument.h"
#endif
// TinyXML's Tiny2NosStub.h maps stdio onto the app's FileSystem; the harness
// itself uses plain stdio
#undef FILE
#undef fopen
#undef fclose
#undef fseek
#undef ftell
#undef fputs
#undef fread
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <algorithm>
#include <string>
#include <vector>

#ifndef SL_TREE
#define SL_TREE "unknown"
#endif

// ------------------------------------------------------------- environment

class LabSystem : public System {
public:
  virtual unsigned long GetClock() {
    struct timeval tv;
    gettimeofday(&tv, 0);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
  }
  virtual int GetBatteryLevel() { return -1; }
  virtual void *Malloc(unsigned size) { return malloc(size); }
  virtual void Free(void *p) { free(p); }
  virtual void Memset(void *addr, char value, int size) { memset(addr, value, size); }
  virtual void *Memcpy(void *s1, const void *s2, int n) { return memcpy(s1, s2, n); }
  virtual void PostQuitMessage() {}
  virtual unsigned int GetMemoryUsage() { return 0; }
};

class LabAudio : public Audio {
public:
  LabAudio(AudioSettings &s) : Audio(s) {}
  virtual void Init() {}
  virtual void Close() {}
  virtual int GetSampleRate() { return 44100; }
};

class StderrLogger : public Trace::Logger {
  virtual void AddLine(const char *line) { fprintf(stderr, "%s\n", line); }
};

static void die(const char *msg, const char *arg = "") {
  fprintf(stderr, "slot_lab: %s%s\n", msg, arg);
  exit(1);
}

static std::string absPath(const char *p) {
  char buf[PATH_MAX];
  if (!realpath(p, buf)) die("no such directory: ", p);
  return buf;
}

static void install() {
  System::Install(new LabSystem());
  FileSystem::Install(new UnixFileSystem());
  Trace::GetInstance()->SetLogger(*(new StderrLogger()));
  AudioSettings st;
  st.bufferSize_ = 1024;
  st.preBufferCount_ = 0;
  st.sampleRate_ = 44100;
  Audio::Install(new LabAudio(st));
  // No MIDI devices, so the project's "midi" variable doesn't depend on the host
  MidiService::Install(new DummyMidi());
  SysProcessFactory::Install(new UnixProcessFactory());
}

// ------------------------------------------------------------- loading

static Project *gProject = 0;
static bool gLoaded = false;
static unsigned int gPoolResult = 0;

// The steps of AppWindow::LoadProject (8887975) that touch the model. One
// difference: the Mixer singleton is created before loading. The app creates
// it only once the mixer view is drawn or playback runs, so the app's first
// load in a session skips <MIXER>; later loads restore it, as here.
static Project *loadProject(const char *dir) {
  std::string root = absPath(dir);
  PersistencyService *persist = PersistencyService::GetInstance();
  Mixer::GetInstance();
  TablePlayback::Reset();
  Path::SetAlias("project", root.c_str());
  Path::SetAlias("samples", "project:samples");
  gPoolResult = SamplePool::GetInstance()->Load();
  Project *project = new Project();
  gLoaded = persist->Load();
  if (!gLoaded) project->GetInstrumentBank()->AssignDefaults();
  WatchedVariable::Disable();
  project->GetInstrumentBank()->Init();
  WatchedVariable::Enable();
  gProject = project;
  return project;
}

static void saveProject(const char *outdir) {
  mkdir(outdir, 0777);
  std::string root = absPath(outdir);
  Path::SetAlias("project", root.c_str());
  PersistencyService::GetInstance()->Save();
}

// ------------------------------------------------------------- dump

static const char *typeName(int t) {
  switch (t) {
  case IT_SAMPLE: return "Sample";
  case IT_MIDI: return "Midi";
#ifdef SL_FORK
  case IT_PIGTAIL: return "Pigtail";
#endif
  default: return "?";
  }
}

// A freshly constructed instrument per type, to spot slots still at defaults
static I_Instrument *freshInstrument(int t) {
  static I_Instrument *cache[IT_LAST + 1] = {0};
  if (t < 0 || t > IT_LAST) return 0;
  if (!cache[t]) {
    switch (t) {
    case IT_SAMPLE: cache[t] = new SampleInstrument(); break;
    case IT_MIDI: cache[t] = new MidiInstrument(); break;
#ifdef SL_FORK
    case IT_PIGTAIL: cache[t] = new PigtailInstrument(); break;
#endif
    default: break;
    }
  }
  return cache[t];
}

static std::string varValue(Variable &v) {
  std::string s = v.GetString();
  if (v.GetType() == Variable::CHAR_LIST) {
    char idx[16];
    sprintf(idx, " [%d]", v.GetInt());
    s += idx;
  }
  return s;
}

static std::vector<std::string> varLines(I_Instrument *in) {
  std::vector<std::string> out;
  IteratorPtr<Variable> it(in->GetIterator());
  for (it->Begin(); !it->IsDone(); it->Next()) {
    Variable &v = it->CurrentItem();
    out.push_back(std::string(v.GetName()) + "=" + varValue(v));
  }
  return out;
}

static unsigned fnv(const void *p, size_t n, unsigned h = 2166136261u) {
  const unsigned char *b = (const unsigned char *)p;
  for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
  return h;
}

static void modeDump(const char *dir) {
  Project *project = loadProject(dir);
  InstrumentBank *bank = project->GetInstrumentBank();
  Song *song = project->song_;

  printf("# slot_lab dump, tree %s\n", SL_TREE);
  printf("load %s, table ratio %d\n", gLoaded ? "ok" : "no save file (defaults)",
         SyncMaster::GetInstance()->GetTableRatio());

  SamplePool *pool = SamplePool::GetInstance();
  printf("pool %d (result %u):", pool->GetNameListSize(), gPoolResult);
  for (int i = 0; i < pool->GetNameListSize(); i++) printf(" %s", pool->GetNameList()[i]);
  printf("\n");

  printf("project");
  IteratorPtr<Variable> pit(project->GetIterator());
  for (pit->Begin(); !pit->IsDone(); pit->Next()) {
    Variable &v = pit->CurrentItem();
    printf(" %s=%s", v.GetName(), varValue(v).c_str());
  }
  printf("\n");

  // Phrase references: every row whose instrument byte isn't FF
  std::vector<int> rows(256, 0);
  std::vector<std::string> phrases(256);
  for (int p = 0; p < PHRASE_COUNT; p++) {
    bool seen[256] = {false};
    for (int r = 0; r < 16; r++) {
      unsigned char b = song->phrase_->instr_[16 * p + r];
      if (b == 0xFF) continue;
      rows[b]++;
      if (!seen[b]) {
        char h[4];
        sprintf(h, " %02X", p);
        phrases[b] += h;
        seen[b] = true;
      }
    }
  }

  printf("slots %02X\n", MAX_INSTRUMENT_COUNT);
  for (int i = 0; i < MAX_INSTRUMENT_COUNT; i++) {
    I_Instrument *in = bank->GetInstrument(i);
    int t = in->GetType();
    std::vector<std::string> lines = varLines(in);
    bool atDefaults = false;
    I_Instrument *fresh = freshInstrument(t);
    if (fresh) atDefaults = (varLines(fresh) == lines);
    printf("I%02X %s%s", i, typeName(t), in->IsEmpty() ? " empty" : "");
    if (rows[i]) printf(", %d rows in phrases%s", rows[i], phrases[i].c_str());
    if (in->IsEmpty() && atDefaults && !rows[i]) {
      printf(", defaults\n");
      continue;
    }
    printf("\n");
    for (size_t k = 0; k < lines.size(); k++) printf("    %s\n", lines[k].c_str());
  }
  for (int b = MAX_INSTRUMENT_COUNT; b < 0xFF; b++) {
    if (rows[b]) printf("out of range I%02X, %d rows in phrases%s\n", b, rows[b], phrases[b].c_str());
  }

  // Everything else as checksums; `save` + `xml` shows the details
  Phrase *ph = song->phrase_;
  printf("data song %08x chains %08x transposes %08x\n",
         fnv(song->data_, SONG_CHANNEL_COUNT * SONG_ROW_COUNT),
         fnv(song->chain_->data_, CHAIN_COUNT * 16), fnv(song->chain_->transpose_, CHAIN_COUNT * 16));
  printf("data notes %08x instruments %08x cmd1 %08x param1 %08x cmd2 %08x param2 %08x\n",
         fnv(ph->note_, PHRASE_COUNT * 16), fnv(ph->instr_, PHRASE_COUNT * 16),
         fnv(ph->cmd1_, PHRASE_COUNT * 16 * sizeof(FourCC)), fnv(ph->param1_, PHRASE_COUNT * 16 * sizeof(ushort)),
         fnv(ph->cmd2_, PHRASE_COUNT * 16 * sizeof(FourCC)), fnv(ph->param2_, PHRASE_COUNT * 16 * sizeof(ushort)));
  unsigned th = 2166136261u;
  for (int i = 0; i < TABLE_COUNT; i++) {
    Table &tb = TableHolder::GetInstance()->GetTable(i);
    th = fnv(tb.cmd1_, sizeof(tb.cmd1_), th);
    th = fnv(tb.param1_, sizeof(tb.param1_), th);
    th = fnv(tb.cmd2_, sizeof(tb.cmd2_), th);
    th = fnv(tb.param2_, sizeof(tb.param2_), th);
    th = fnv(tb.cmd3_, sizeof(tb.cmd3_), th);
    th = fnv(tb.param3_, sizeof(tb.param3_), th);
  }
  unsigned gh = 2166136261u;
  for (int i = 0; i < MAX_GROOVES; i++) gh = fnv(Groove::GetInstance()->GetGrooveData(i), 16, gh);
  printf("data tables %08x grooves %08x\n", th, gh);
  Mixer *mx = Mixer::GetInstance();
  printf("mixer bus");
  for (int i = 0; i < SONG_CHANNEL_COUNT; i++) printf(" %d", mx->GetBus(i));
  printf(" vol");
  for (int i = 0; i < SONG_CHANNEL_COUNT; i++) printf(" %02X", mx->GetChannelVolume(i));
  printf(" hpf");
  for (int i = 0; i < SONG_CHANNEL_COUNT; i++) printf(" %d", mx->GetChannelHPF(i));
  printf(" lpf");
  for (int i = 0; i < SONG_CHANNEL_COUNT; i++) printf(" %d", mx->GetChannelLPF(i));
  printf("\n");
}

// ------------------------------------------------------------- xml, lz

static unsigned char *readFile(const char *name, int *len) {
  FILE *f = fopen(name, "rb");
  if (!f) die("can't open ", name);
  fseek(f, 0, SEEK_END);
  *len = ftell(f);
  fseek(f, 0, SEEK_SET);
  unsigned char *buf = (unsigned char *)malloc(*len + 1);
  if (fread(buf, 1, *len, f) != (size_t)*len) die("short read on ", name);
  buf[*len] = 0;
  fclose(f);
  return buf;
}

// Same decision as PersistencyService::Load: plain XML if it parses, else LZ
static void modeXml(const char *name, const char *out) {
  int len;
  unsigned char *buf = readFile(name, &len);
  TiXmlDocument doc;
  if (!doc.Parse((char *)buf)) {
    int full;
    memcpy(&full, buf, sizeof(int));
    unsigned char *xml = (unsigned char *)malloc(full + 1);
    LZ_Uncompress(buf + sizeof(int), xml, len - sizeof(int));
    xml[full] = 0;
    doc.Clear();
    doc.Parse((char *)xml);
    free(xml);
    fprintf(stderr, "slot_lab: %s is LZ, %d bytes uncompressed\n", name, full);
  }
  free(buf);
  if (doc.Error()) die("XML error in ", name);
  if (!doc.SaveFile(out)) die("can't write ", out);
}

// Length includes the terminating NUL, so the loader's buffer is a C string.
// A guess: no build in this repository's history writes compressed saves.
static void modeLz(const char *in, const char *out) {
  int len;
  unsigned char *buf = readFile(in, &len);
  int full = len + 1;
  unsigned char *z = (unsigned char *)malloc(full + full / 64 + 400);
  int zlen = LZ_Compress(buf, z, full);
  FILE *f = fopen(out, "wb");
  if (!f) die("can't write ", out);
  fwrite(&full, sizeof(int), 1, f);
  fwrite(z, 1, zlen, f);
  fclose(f);
}

// ------------------------------------------------------------- gen

// 16-bit mono 44.1 kHz WAV, 512 frames of a square wave, integer-only
static void writeWav(const std::string &path, int period) {
  const int n = 512;
  unsigned char h[44];
  int data = n * 2, riff = 36 + data, fmt = 16, rate = 44100, bps = rate * 2;
  short one = 1, block = 2, bits = 16;
  memcpy(h, "RIFF", 4); memcpy(h + 4, &riff, 4); memcpy(h + 8, "WAVEfmt ", 8);
  memcpy(h + 16, &fmt, 4); memcpy(h + 20, &one, 2); memcpy(h + 22, &one, 2);
  memcpy(h + 24, &rate, 4); memcpy(h + 28, &bps, 4); memcpy(h + 32, &block, 2);
  memcpy(h + 34, &bits, 2); memcpy(h + 36, "data", 4); memcpy(h + 40, &data, 4);
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) die("can't write ", path.c_str());
  fwrite(h, 1, 44, f);
  for (int i = 0; i < n; i++) {
    short s = ((i / (period / 2)) & 1) ? -8000 : 8000;
    fwrite(&s, 2, 1, f);
  }
  fclose(f);
}

static void setVar(I_Instrument *in, FourCC id, int value) {
  Variable *v = in->FindVariable(id);
  if (!v) die("missing instrument variable");
  v->SetInt(value);
}

static void setRow(Song *s, int phrase, int row, int note, int instr,
                   FourCC c1 = I_CMD_NONE, ushort p1 = 0, FourCC c2 = I_CMD_NONE, ushort p2 = 0) {
  int k = 16 * phrase + row;
  s->phrase_->note_[k] = note;
  s->phrase_->instr_[k] = instr;
  s->phrase_->cmd1_[k] = c1;
  s->phrase_->param1_[k] = p1;
  s->phrase_->cmd2_[k] = c2;
  s->phrase_->param2_[k] = p2;
}

static void setChain(Song *s, int chain, int row, int phrase, int transpose = 0) {
  s->chain_->data_[16 * chain + row] = phrase;
  s->chain_->transpose_[16 * chain + row] = transpose;
}

static void setSong(Song *s, int channel, int row, int chain) {
  s->data_[SONG_CHANNEL_COUNT * row + channel] = chain;
}

static void genSamples(const std::string &dir, int count) {
  static const char *names[] = { "a_kick.wav", "b_snare.wav", "c_tone.wav" };
  static const int periods[] = { 200, 50, 100 };
  std::string sdir = dir + "/samples";
  mkdir(dir.c_str(), 0777);
  mkdir(sdir.c_str(), 0777);
  for (int i = 0; i < count; i++) writeWav(sdir + "/" + names[i], periods[i]);
}

// Slots, phrases, chains and the song for the "basic" fixture: samples in
// 00-02 (from AssignDefaults), 40 and 7F; MIDI 80, 85 (default) and 8F;
// sampleless 03 and 7E referenced; one row with an instrument but no note
static void genBasic(Project *p) {
  InstrumentBank *b = p->GetInstrumentBank();
  Song *s = p->song_;
  p->FindVariable(VAR_TEMPO)->SetInt(144);
  setVar(b->GetInstrument(0x00), SIP_VOLUME, 0x60);
  setVar(b->GetInstrument(0x00), SIP_PAN, 0x40);
  setVar(b->GetInstrument(0x00), SIP_ROOTNOTE, 48);
  setVar(b->GetInstrument(0x01), SIP_FILTCUTOFF, 0x80);
  setVar(b->GetInstrument(0x01), SIP_FILTRESO, 0x40);
  setVar(b->GetInstrument(0x01), SIP_LOOPMODE, 1);
  setVar(b->GetInstrument(0x01), SIP_INTERPOLATION, 1);
  setVar(b->GetInstrument(0x02), SIP_CRUSH, 8);
  setVar(b->GetInstrument(0x02), SIP_CRUSHVOL, 0xC0);
  setVar(b->GetInstrument(0x02), SIP_VOLUME, 0x70);
  setVar(b->GetInstrument(0x40), SIP_SAMPLE, 0);
  setVar(b->GetInstrument(0x40), SIP_FINETUNE, 0x70);
  setVar(b->GetInstrument(0x7F), SIP_SAMPLE, 2);
  setVar(b->GetInstrument(0x7F), SIP_VOLUME, 0xA0);
  setVar(b->GetInstrument(0x80), MIP_CHANNEL, 9);
  setVar(b->GetInstrument(0x80), MIP_VOLUME, 0x50);
  setVar(b->GetInstrument(0x80), MIP_NOTELENGTH, 0x10);
  setVar(b->GetInstrument(0x8F), MIP_VOLUME, 0x7F);
  setRow(s, 0x00, 0, 0x30, 0x00);
  setRow(s, 0x00, 4, 0x32, 0x01);
  setRow(s, 0x00, 8, 0x34, 0x80);
  setRow(s, 0x00, 12, 0x35, 0x02);
  setRow(s, 0x01, 0, 0x30, 0x40);
  setRow(s, 0x01, 2, 0x30, 0x7F);
  setRow(s, 0x01, 4, 0x3C, 0x8F);
  setRow(s, 0x01, 6, 0xFF, 0x01);  // instrument, no note
  setRow(s, 0x01, 8, 0x3C, 0x85);
  setRow(s, 0x01, 10, 0x3E, 0xFF); // note, no instrument
  setRow(s, 0x02, 0, 0x30, 0x03);
  setRow(s, 0x02, 8, 0x30, 0x7E);
  setChain(s, 0x00, 0, 0x00);
  setChain(s, 0x00, 1, 0x01);
  setChain(s, 0x01, 0, 0x02, 0x0C);
  setSong(s, 0, 0, 0x00);
  setSong(s, 1, 0, 0x01);
  setSong(s, 2, 1, 0x00);
}

// MIDI clones in the sample range (Clone() targets GetNext(), the first
// sampleless slot), plus a Sample clone changed after cloning
static void genClones(Project *p) {
  InstrumentBank *b = p->GetInstrumentBank();
  Song *s = p->song_;
  setVar(b->GetInstrument(0x80), MIP_CHANNEL, 3);
  setVar(b->GetInstrument(0x80), MIP_VOLUME, 0x40);
  setVar(b->GetInstrument(0x81), MIP_CHANNEL, 0x0E);
  setVar(b->GetInstrument(0x81), MIP_NOTELENGTH, 0x20);
  setVar(b->GetInstrument(0x00), SIP_VOLUME, 0x90);
  int c1 = b->Clone(0x80);
  int c2 = b->Clone(0x81);
  int c3 = b->Clone(0x00);
  fprintf(stderr, "slot_lab: clones of 80, 81, 00 went to %02X, %02X, %02X\n", c1, c2, c3);
  if (c3 < 0x80) setVar(b->GetInstrument(c3), SIP_VOLUME, 0x30);
  setRow(s, 0x00, 0, 0x30, 0x00);
  setRow(s, 0x00, 2, 0x30, c1);
  setRow(s, 0x00, 4, 0x30, c2);
  setRow(s, 0x00, 6, 0x30, c3);
  setRow(s, 0x00, 8, 0x30, 0x80);
  setChain(s, 0x00, 0, 0x00);
  setSong(s, 0, 0, 0x00);
}

// Instrument tables with automation, phrase commands (TABL, VOLM, MIDI CC,
// groove), a groove and mixer settings
static void genTables(Project *p) {
  InstrumentBank *b = p->GetInstrumentBank();
  Song *s = p->song_;
  TableHolder *th = TableHolder::GetInstance();
  setVar(b->GetInstrument(0x00), SIP_TABLE, 0x00);
  setVar(b->GetInstrument(0x00), SIP_TABLEAUTO, 1);
  setVar(b->GetInstrument(0x01), SIP_TABLE, 0x05);
  setVar(b->GetInstrument(0x80), MIP_TABLE, 0x10);
  setVar(b->GetInstrument(0x80), MIP_TABLEAUTO, 1);
  Table &t0 = th->GetTable(0x00);
  for (int r = 0; r < 4; r++) { t0.cmd1_[r] = I_CMD_VOLM; t0.param1_[r] = 0x0080 - 0x20 * r; }
  t0.cmd2_[4] = I_CMD_PTCH; t0.param2_[4] = 0x0001;
  t0.cmd3_[0] = I_CMD_ARPG; t0.param3_[0] = 0x0370;
  th->SetUsed(0x00);
  Table &t5 = th->GetTable(0x05);
  t5.cmd1_[0] = I_CMD_VOLM; t5.param1_[0] = 0x0040;
  t5.cmd1_[3] = I_CMD_KILL; t5.param1_[3] = 0x0000;
  t5.cmd2_[2] = I_CMD_HOP; t5.param2_[2] = 0x0000;
  th->SetUsed(0x05);
  Table &t10 = th->GetTable(0x10);
  t10.cmd1_[0] = I_CMD_MDCC; t10.param1_[0] = 0x0740;
  t10.cmd1_[1] = I_CMD_MVEL; t10.param1_[1] = 0x0060;
  th->SetUsed(0x10);
  unsigned char *g = Groove::GetInstance()->GetGrooveData(1);
  g[0] = 8; g[1] = 4;
  setRow(s, 0x00, 0, 0x30, 0x00, I_CMD_TABL, 0x0005, I_CMD_VOLM, 0x0080);
  setRow(s, 0x00, 4, 0x30, 0x01, I_CMD_PTCH, 0x0002);
  setRow(s, 0x00, 8, 0x30, 0x80, I_CMD_MDCC, 0x0A40, I_CMD_GROV, 0x0001);
  setRow(s, 0x00, 12, 0x30, 0x02, I_CMD_TABL, 0x0010);
  setChain(s, 0x00, 0, 0x00);
  setSong(s, 0, 0, 0x00);
  Mixer *mx = Mixer::GetInstance();
  mx->SetBus(1, 1);
  mx->SetChannelVolume(0, 0xC0);
  mx->SetChannelHPF(1, 1);
  mx->SetChannelLPF(2, 4000);
}

static void modeGen(const char *name, const char *dir) {
  std::string n = name;
  struct stat st;
  if (stat((std::string(dir) + "/lgptsav.dat").c_str(), &st) == 0) die("refusing to overwrite a save in ", dir);
  genSamples(dir, n == "clones" ? 2 : 3);
  Project *p = loadProject(dir);
  if (n == "basic") genBasic(p);
  else if (n == "clones") genClones(p);
  else if (n == "tables") genTables(p);
  else die("unknown fixture ", name);
  saveProject(dir);
}

// ------------------------------------------------------------- play

// Audio output for the play mode. MixerService picks it up as the first output
// of the installed Audio; Pulse() does what a driver's buffer request does
// (MixerService::Update: player tick, then Trigger renders), from our loop
class LabAudioOut : public AudioOut {
public:
  LabAudioOut() : buffer_(0), buffers_(0), audible_(0) {}
  virtual bool Init() {
    buffer_ = (fixed *)malloc(MIX_BUFFER_SIZE * sizeof(fixed));
    return buffer_ != 0;
  }
  virtual void Close() { free(buffer_); buffer_ = 0; }
  virtual bool Start() { return true; }
  virtual void Stop() {}
  virtual void SetMasterVolume(int) {}
  virtual void Trigger() {
    int n = getPlaySampleCount();
    if (2 * n > MIX_BUFFER_SIZE) die("buffer request too large");
    buffers_++;
    if (!AudioMixer::Render(buffer_, n)) return;
    for (int i = 0; i < 2 * n; i++) {
      if (buffer_[i]) { audible_++; break; }
    }
  }
  virtual int GetPlayedBufferPercentage() { return 0; }
  virtual std::string GetAudioAPI() { return "slot_lab"; }
  virtual std::string GetAudioDevice() { return "pulse"; }
  virtual int GetAudioBufferSize() { return 0; }
  virtual int GetAudioRequestedBufferSize() { return 0; }
  virtual int GetAudioPreBufferCount() { return 0; }
  virtual double GetStreamTime() { return 0; }
  void Pulse() {
    SetChanged();
    AudioDriver::Event e(AudioDriver::Event::ADET_BUFFERNEEDED);
    NotifyObservers(&e);
  }
  fixed *buffer_;
  int buffers_, audible_;
};

static LabAudioOut *gOut = 0;
// Instrument bytes the player showed per channel while a note was on
static std::vector<std::string> gPlayed[SONG_CHANNEL_COUNT];

static void pulse(int count) {
  Player *player = Player::GetInstance();
  for (int i = 0; i < count; i++) {
    gOut->Pulse();
    for (int c = 0; c < SONG_CHANNEL_COUNT; c++) {
      std::string s = player->GetPlayedInstrument(c);
      if (s == "  ") continue;
      std::vector<std::string> &v = gPlayed[c];
      if (std::find(v.begin(), v.end(), s) == v.end()) v.push_back(s);
    }
  }
}

static void printPlayed() {
  for (int c = 0; c < SONG_CHANNEL_COUNT; c++) {
    if (gPlayed[c].empty()) continue;
    printf("  channel %d played", c);
    for (size_t k = 0; k < gPlayed[c].size(); k++) printf(" %s", gPlayed[c][k].c_str());
    printf("\n");
  }
  printf("  %d buffers rendered, %d with sound\n", gOut->buffers_, gOut->audible_);
}

static void clearPhrase(Song *s, int phrase) {
  for (int r = 0; r < 16; r++) setRow(s, phrase, r, 0xFF, 0xFF);
}

static void clearChain(Song *s, int chain) {
  for (int r = 0; r < 16; r++) setChain(s, chain, r, 0xFF);
}

// Song rows 1 and up empty, so each channel loops its row-0 chain
static void clearSong(Song *s) {
  memset(s->data_, 0xFF, SONG_CHANNEL_COUNT * SONG_ROW_COUNT);
}

// Phrase bytes 90-FE, 8 per phrase on the even rows; the odd rows have a note
// and no instrument, so they replay the channel's last instrument. Phrases
// E0-ED, channel c's chain E0+c holds the phrases k with k%8 == c
static void setupRange(Project *p) {
  Song *s = p->song_;
  clearSong(s);
  for (int c = 0; c < SONG_CHANNEL_COUNT; c++) {
    clearChain(s, 0xE0 + c);
    setSong(s, c, 0, 0xE0 + c);
  }
  int b = 0x90;
  for (int k = 0; b < 0xFF; k++) {
    int ph = 0xE0 + k;
    clearPhrase(s, ph);
    for (int r = 0; r < 16 && b < 0xFF; r += 2, b++) {
      setRow(s, ph, r, 0x30, b);
      setRow(s, ph, r + 1, 0x37, 0xFF);
    }
    setChain(s, 0xE0 + k % 8, k / 8, ph);
  }
}

// What the phrase header and the instrument screen ask the bank for
static void printLookups(Project *p) {
  InstrumentBank *bank = p->GetInstrumentBank();
  printf("bank lookups 90-FE:");
  for (int i = 0x90; i < 0xFF; i++) {
    I_Instrument *in = bank->GetInstrument(i);
    if (i == 0x90 || i == 0xA0 || i == 0xFE)
      printf(" %02X=%s/%s/table %d", i, typeName(in->GetType()), in->GetName(), in->GetTable());
  }
  printf("\n");
}

// Needs a project whose slots 00-02 have samples, 03 and 04 none and 03 is
// GetNext(), like up-basic. Channel 0 starts sampleless 03 with a table, then
// replays it from rows without an instrument (last instrument, table); channel
// 1 plays 04 with a looped sample. Then, as the screens would: purge 04's
// sample (B,A on the sample field), clone 00 and 01 from the phrase screen
// (LT+B,A). The clones land in 03 and 04, which the player still points at
static void setupClone(Project *p) {
  InstrumentBank *b = p->GetInstrumentBank();
  Song *s = p->song_;
  for (int i = 0; i < 5; i++) {
    I_Instrument *in = b->GetInstrument(i);
    if (in->GetType() != IT_SAMPLE || (in->FindVariable(SIP_SAMPLE)->GetInt() >= 0) != (i < 3))
      die("clone scenario needs samples in 00-02 and none in 03-04");
  }
  if (b->GetNext() != 0x03) die("clone scenario needs GetNext() == 03");
  setVar(b->GetInstrument(0x03), SIP_TABLE, 0x00);
  Table &t0 = TableHolder::GetInstance()->GetTable(0x00);
  for (int r = 0; r < 16; r++) { t0.cmd1_[r] = I_CMD_VOLM; t0.param1_[r] = 0x0080; }
  TableHolder::GetInstance()->SetUsed(0x00);
  setVar(b->GetInstrument(0x04), SIP_SAMPLE, 2);
  setVar(b->GetInstrument(0x04), SIP_LOOPMODE, 1);
  clearSong(s);
  for (int i = 0xE0; i < 0xE3; i++) {
    clearChain(s, i);
    clearPhrase(s, i);
    setChain(s, i, 0, i);
  }
  setRow(s, 0xE0, 0, 0x30, 0x03);
  for (int r = 2; r < 16; r += 2) setRow(s, 0xE0, r, 0x30, 0xFF);
  setRow(s, 0xE1, 0, 0x30, 0x04);
  for (int r = 4; r < 16; r += 4) setRow(s, 0xE1, r, 0x37, 0xFF);
  // Channels 2-7 play the empty phrase E2: starting song mode on a channel
  // without a phrase reads past the phrase arrays (Player::updatePhrasePos,
  // a separate bug)
  for (int c = 0; c < SONG_CHANNEL_COUNT; c++) setSong(s, c, 0, 0xE0 + (c < 2 ? c : 2));
}

static void cloneDuringPlay(Project *p) {
  InstrumentBank *b = p->GetInstrumentBank();
  printf("before clone: %d of %d buffers with sound (only 04 has a sample), channel 0 table %s\n",
         gOut->audible_, gOut->buffers_,
         TablePlayback::GetTablePlayback(0).GetTable() ? "running" : "stopped");
  // Each step is one button event in the app, which holds the mixer lock
  MixerService *ms = MixerService::GetInstance();
  ms->Lock();
  b->GetInstrument(0x04)->Purge();
  ms->Unlock();
  pulse(20);
  ms->Lock();
  int c1 = b->Clone(0x00);
  ms->Unlock();
  ms->Lock();
  int c2 = b->Clone(0x01);
  ms->Unlock();
  printf("clones of 00 and 01 went to %02X and %02X\n", c1, c2);
}

static void modePlay(const char *dir, const char *scenario) {
  std::string sc = scenario;
  if (sc != "range" && sc != "clone") die("unknown scenario ", scenario);
  setvbuf(stdout, 0, _IONBF, 0);  // keep the output if a sanitizer aborts
  gOut = new LabAudioOut();
  Audio::GetInstance()->Insert(gOut);
  Project *project = loadProject(dir);
  if (sc == "range") setupRange(project);
  else setupClone(project);
  ViewData *viewData = new ViewData(project);
  Player *player = Player::GetInstance();
  if (!player->Init(project, viewData)) die("player init failed");
  printf("# slot_lab play %s, tree %s\n", scenario, SL_TREE);
  player->OnStartButton(PM_SONG, 0, false, 0);
  if (sc == "range") {
    pulse(3000);
  } else {
    pulse(50);
    cloneDuringPlay(project);
    pulse(1000);
  }
  player->Stop();
  printf("played:\n");
  printPlayed();
  if (sc == "range") printLookups(project);
}

// ------------------------------------------------------------- main

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: slot_lab dump <projdir> | save <projdir> <outdir> | xml <file> <out>\n"
                    "       | gen basic|clones|tables <projdir> | lz <in> <out>\n"
                    "       | play <projdir> range|clone\n");
    return 2;
  }
  std::string mode = argv[1];
  if (mode == "lz" && argc == 4) { modeLz(argv[2], argv[3]); _exit(0); }
  install();
  if (mode == "xml" && argc == 4) { modeXml(argv[2], argv[3]); _exit(0); }
  if (mode == "dump") modeDump(argv[2]);
  else if (mode == "save" && argc == 4) { loadProject(argv[2]); saveProject(argv[3]); }
  else if (mode == "gen" && argc == 4) modeGen(argv[2], argv[3]);
  else if (mode == "play" && argc == 4) modePlay(argv[2], argv[3]);
  else die("bad arguments for mode ", argv[1]);
  fflush(stdout);
  _exit(0);  // skip static teardown of the app singletons
}
