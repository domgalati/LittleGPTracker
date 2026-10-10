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
//   mem <projdir>            heap in use (glibc mallinfo2) after creating the
//                            project and after loading it, and filled slots
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
//                            (phrase bytes 90-FE), clone (upstream: Clone onto
//                            slots a channel and a table still use; fork: those
//                            slots emptied by Compact, then cloned into), view (the
//                            instrument screen's observers across slot
//                            changes, a Clone and a Compact; no drawing, no
//                            playback),
//                            dlay (DLAY in command column 2 delays by its
//                            own parameter), flows (fork: A,A and LT+(B,A) in
//                            the phrase screen and imports from the sample
//                            dialog; writes samples into <projdir>, so give
//                            it a copy), types (fork: the instrument
//                            screen's type field and clear confirmation on
//                            slots the song is playing, then a seeded
//                            random run of both; MIDI goes to a recording
//                            device, which must end with no note held),
//                            resolve (fork: the command popup's context,
//                            design doc 8.2: which slot a phrase row's
//                            commands go to, the table screen's slot as the
//                            phrase and mixer screens leave it, and which
//                            commands each type and location dims). Meant
//                            for the ASan build (make play)
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
#include "Services/Midi/MidiService.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"
#ifdef SL_FORK
#include "Application/Instruments/PigtailInstrument.h"
#include "Application/Views/InstrumentView.h"
#include "Application/Views/ModalDialogs/ImportSampleDialog.h"
#include "Application/Views/PhraseView.h"
#include "Application/Views/CommandContext.h"
#include "Application/Views/MixerView.h"
#include "Application/Utils/char.h"
#include "UIFramework/Interfaces/I_GUIWindowImp.h"
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
#include <malloc.h>
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
// Heap in use (bytes) before the project, after creating it (and the Mixer
// singleton), after loading
static size_t gHeap[3];

static size_t heapInUse() { return mallinfo2().uordblks; }

// The steps of AppWindow::LoadProject that touch the model
static Project *loadProject(const char *dir) {
  std::string root = absPath(dir);
  PersistencyService *persist = PersistencyService::GetInstance();
  TablePlayback::Reset();
  Path::SetAlias("project", root.c_str());
  Path::SetAlias("samples", "project:samples");
  gPoolResult = SamplePool::GetInstance()->Load();
  gHeap[0] = heapInUse();
  Project *project = new Project();
  // Creating the Mixer earlier would change the save's node order
  Mixer::GetInstance()->Clear();
  gHeap[1] = heapInUse();
  gLoaded = persist->Load();
#ifdef SL_FORK
  if (gLoaded) project->OnLoaded();
#endif
  if (!gLoaded) project->GetInstrumentBank()->AssignDefaults();
  WatchedVariable::Disable();
  project->GetInstrumentBank()->Init();
  WatchedVariable::Enable();
  gHeap[2] = heapInUse();
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

// Empty slots exist only in the fork; upstream fills every slot
static bool slotEmpty(InstrumentBank *bank, int i) {
#ifdef SL_FORK
  return bank->IsSlotEmpty(i);
#else
  return false;
#endif
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

  // An empty slot (fork) prints as "--", and only if a phrase plays it. A
  // slot whose variables equal a new instrument's says "defaults"; its
  // variables are left out if it's also empty (a sampleless Sample) and
  // no phrase plays it
  printf("slots %02X\n", MAX_INSTRUMENT_COUNT);
  for (int i = 0; i < MAX_INSTRUMENT_COUNT; i++) {
    if (slotEmpty(bank, i)) {
      if (rows[i]) printf("I%02X --, %d rows in phrases%s\n", i, rows[i], phrases[i].c_str());
      continue;
    }
    I_Instrument *in = bank->GetInstrument(i);
    int t = in->GetType();
    std::vector<std::string> lines = varLines(in);
    bool atDefaults = false;
    I_Instrument *fresh = freshInstrument(t);
    if (fresh) atDefaults = (varLines(fresh) == lines);
    printf("I%02X %s%s%s", i, typeName(t), in->IsEmpty() ? " empty" : "", atDefaults ? ", defaults" : "");
    if (rows[i]) printf(", %d rows in phrases%s", rows[i], phrases[i].c_str());
    printf("\n");
    if (in->IsEmpty() && atDefaults && !rows[i]) continue;
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

// ------------------------------------------------------------- mem

static void modeMem(const char *dir) {
  Project *project = loadProject(dir);
  InstrumentBank *bank = project->GetInstrumentBank();
  int filled = 0;
  for (int i = 0; i < MAX_INSTRUMENT_COUNT; i++) {
    if (!slotEmpty(bank, i)) filled++;
  }
  printf("# slot_lab mem, tree %s\n", SL_TREE);
  printf("new Project and Mixer: %zu bytes\n", gHeap[1] - gHeap[0]);
  printf("load and Init: %zu bytes\n", gHeap[2] - gHeap[1]);
  printf("project after load: %zu bytes, %d of %d slots filled\n", gHeap[2] - gHeap[0], filled,
         MAX_INSTRUMENT_COUNT);
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

// Set by scenarios that check MIDI output: each pulse then also ticks and
// flushes the MIDI service, as the SDL2 driver does after every buffer
static bool gMidiTicks = false;

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
    if (gMidiTicks) {
      SetChanged();
      AudioDriver::Event tick(AudioDriver::Event::ADET_DRIVERTICK);
      NotifyObservers(&tick);
      MidiService::GetInstance()->Flush();
    }
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
// (LT+B,A). Upstream: the clones land in 03 and 04, which the player still
// points at. Fork: Clone only fills empty slots, so first the phrase rows
// that name 03 and 04 lose their instrument and Compact empties both slots
// while the player still points at them; then the clones land there
static void setupClone(Project *p) {
  InstrumentBank *b = p->GetInstrumentBank();
  Song *s = p->song_;
#ifdef SL_FORK
  // up-basic loads 03 and 04 empty
  if (b->FirstEmpty() != 0x03 || !b->IsSlotEmpty(0x04)) die("clone scenario needs 03 and 04 empty");
  b->Create(0x03, IT_SAMPLE);
  b->Create(0x04, IT_SAMPLE);
#endif
  for (int i = 0; i < 5; i++) {
    I_Instrument *in = b->GetInstrument(i);
    if (in->GetType() != IT_SAMPLE || (in->FindVariable(SIP_SAMPLE)->GetInt() >= 0) != (i < 3))
      die("clone scenario needs samples in 00-02 and none in 03-04");
  }
#ifndef SL_FORK
  if (b->GetNext() != 0x03) die("clone scenario needs GetNext() == 03");
#endif
  setVar(b->GetInstrument(0x03), SIP_TABLE, 0x00);
  Table &t0 = TableHolder::GetInstance()->GetTable(0x00);
  for (int r = 0; r < 16; r++) { t0.cmd1_[r] = I_CMD_VOLM; t0.param1_[r] = 0x0080; }
  TableHolder::GetInstance()->SetUsed(0x00);
  setVar(b->GetInstrument(0x04), SIP_SAMPLE, 2);
  setVar(b->GetInstrument(0x04), SIP_LOOPMODE, 1);
  clearSong(s);
  for (int i = 0xE0; i < 0xE2; i++) {
    clearChain(s, i);
    clearPhrase(s, i);
    setChain(s, i, 0, i);
  }
  setRow(s, 0xE0, 0, 0x30, 0x03);
  for (int r = 2; r < 16; r += 2) setRow(s, 0xE0, r, 0x30, 0xFF);
  setRow(s, 0xE1, 0, 0x30, 0x04);
  for (int r = 4; r < 16; r += 4) setRow(s, 0xE1, r, 0x37, 0xFF);
  // Channels 2-7 have no chain: song mode starts with them empty
  setSong(s, 0, 0, 0xE0);
  setSong(s, 1, 0, 0xE1);
}

#ifdef SL_FORK
// Stands in for the instrument screen, which observes the instrument it shows
class LabObserver : public I_Observer {
public:
  LabObserver() : deleted_(0), other_(0) {}
  virtual void Update(Observable &o, I_ObservableData *d) {
    if (d && ((InstrumentEvent *)d)->type_ == IET_DELETED) deleted_++;
    else other_++;
  }
  int deleted_, other_;
};
#endif

static void cloneDuringPlay(Project *p) {
  InstrumentBank *b = p->GetInstrumentBank();
#ifdef SL_FORK
  LabObserver obs[2];
  b->GetInstrument(0x03)->AddObserver(obs[0]);
  b->GetInstrument(0x04)->AddObserver(obs[1]);
#endif
  printf("before clone: %d of %d buffers with sound (only 04 has a sample), channel 0 table %s\n",
         gOut->audible_, gOut->buffers_,
         TablePlayback::GetTablePlayback(0).GetTable() ? "running" : "stopped");
  // Each step is one button event in the app, which holds the mixer lock
  MixerService *ms = MixerService::GetInstance();
  ms->Lock();
  b->GetInstrument(0x04)->Purge();
  ms->Unlock();
  pulse(20);
#ifdef SL_FORK
  ms->Lock();
  p->song_->phrase_->instr_[16 * 0xE0] = 0xFF;
  p->song_->phrase_->instr_[16 * 0xE1] = 0xFF;
  p->song_->phrase_->instr_[16 * 0x02] = 0xFF;  // up-basic's own row with 03
  p->PurgeInstruments(false);
  ms->Unlock();
  printf("after Compact: 03 %s, 04 %s\n", b->IsSlotEmpty(0x03) ? "empty" : "FILLED",
         b->IsSlotEmpty(0x04) ? "empty" : "FILLED");
  pulse(20);
#endif
  ms->Lock();
  int c1 = b->Clone(0x00);
  ms->Unlock();
  ms->Lock();
  int c2 = b->Clone(0x01);
  ms->Unlock();
  printf("clones of 00 and 01 went to %02X and %02X\n", c1, c2);
#ifdef SL_FORK
  for (int k = 0; k < 2; k++)
    printf("observer of the old %02X: %d deleted event(s), %d other notification(s)\n", 3 + k,
           obs[k].deleted_, obs[k].other_);
#endif
}

#ifdef SL_FORK
// A window for a view that is never drawn: View's drawing casts the window
// to AppWindow, so the view scenario only constructs, focuses and sends
// buttons. GUIWindow deletes its imp
class LabWindowImp : public I_GUIWindowImp {
public:
  virtual void Clear(GUIColor &, bool) {}
  virtual void SetColor(GUIColor &) {}
  virtual void ClearRect(GUIRect &) {}
  virtual void DrawString(const char *, GUIPoint &, GUITextProperties &, bool) {}
  virtual void DrawChar(const char, GUIPoint &, GUITextProperties &) {}
  virtual GUIRect GetRect() { return GUIRect(0, 0, 320, 240); }
  virtual void Invalidate() {}
  virtual void Lock() {}
  virtual void Unlock() {}
  virtual void Flush() {}
  virtual void PushEvent(GUIEvent &) {}
};

class LabWindow : public GUIWindow {
public:
  LabWindow(I_GUIWindowImp &imp) : GUIWindow(imp) {}
  virtual void onUpdate() {}
  virtual bool onEvent(GUIEvent &) { return false; }
};

// Declared in Foundation/Observable.h but defined nowhere in the app. If the
// app ever defines it, this one fails to link and can go
int Observable::CountObservers() { return (int)_list.size(); }

static int gFailed = 0;

static void expect(bool ok, const char *what) {
  printf("  %s: %s\n", ok ? "ok" : "FAIL", what);
  if (!ok) gFailed++;
}

// The instrument screen observes the instrument it shows (sample changes
// rebuild its fields). Needs up-basic's slots: samples in 00-02, 03 the first
// empty slot, played only by phrase 02 row 0
static void modeView(Project *p) {
  InstrumentBank *b = p->GetInstrumentBank();
  if (b->FirstEmpty() != 0x03) die("view scenario needs FirstEmpty() == 03");
  LabWindow *win = new LabWindow(*new LabWindowImp());
  ViewData *vd = new ViewData(p);
  InstrumentView *iv = new InstrumentView(*win, vd);
  iv->OnFocus();
  printf("view on slot 00\n");
  expect(b->GetInstrument(0x00)->CountObservers() == 1, "00 has the screen as its one observer");
  iv->ProcessButtonMask(EPBM_B | EPBM_RIGHT, true);  // next slot
  expect(b->GetInstrument(0x00)->CountObservers() == 0, "B+RIGHT to 01: 00 has no observer left");
  expect(b->GetInstrument(0x01)->CountObservers() == 1, "01 has one");
  iv->ProcessButtonMask(EPBM_B | EPBM_RIGHT, true);
  iv->ProcessButtonMask(EPBM_B | EPBM_RIGHT, true);
  expect(b->GetInstrument(0x03)->CountObservers() == 1 && b->GetInstrument(0x01)->CountObservers() == 0,
         "B+RIGHT twice to empty 03: only the stand-in observed");

  // The phrase screen clones 00 into 03 (FirstEmpty) while this screen shows 03
  I_Instrument *old03 = b->GetInstrument(0x03);
  int c = b->Clone(0x00);
  printf("clone of 00 went to %02X\n", c);
  expect(b->GetInstrument(0x03) != old03, "03 holds a new instrument");
  iv->OnFocus();  // back to the instrument screen; must not touch the old 03
  expect(b->GetInstrument(0x03)->CountObservers() == 1, "after OnFocus the new 03 is observed");

  // The phrase screen sets the current slot without switching screens (R+RIGHT
  // on a row with no instrument and none before it); then 03's sample changes
  vd->currentInstrument_ = 0xFF;
  b->GetInstrument(0x03)->FindVariable(SIP_SAMPLE)->SetInt(1);
  expect(b->GetInstrument(0x03)->CountObservers() == 1, "sample change on 03 while the slot is FF: still observed");
  iv->OnFocus();
  expect(b->GetInstrument(0x03)->CountObservers() == 0, "OnFocus on slot FF: 03 no longer observed");
  expect(b->GetInstrument(0xFF)->CountObservers() == 1, "the stand-in is");

  // Buttons on a screen without fields
  iv->ProcessButtonMask(EPBM_A, true);
  iv->ProcessButtonMask(EPBM_B | EPBM_A, true);
  iv->ProcessButtonMask(EPBM_B | EPBM_L, true);
  iv->ProcessButtonMask(EPBM_A | EPBM_L, true);
  iv->ProcessButtonMask(EPBM_B | EPBM_LEFT, true);
  printf("buttons on slot FF, then B+LEFT: slot %02X\n", vd->currentInstrument_);
  // FE is an empty slot, so the screen still shows the stand-in: once
  expect(b->GetInstrument(vd->currentInstrument_)->CountObservers() == 1, "the slot it moved to is observed once");
  iv->ProcessButtonMask(EPBM_B | EPBM_RIGHT, true);  // wraps past FE
  printf("B+RIGHT: slot %02X\n", vd->currentInstrument_);
  expect(b->GetInstrument(0xFF)->CountObservers() == 0 && b->GetInstrument(0x00)->CountObservers() == 1,
         "on 00 the stand-in isn't observed");

  // Compact empties 03 once no phrase plays it, while this screen shows it
  vd->currentInstrument_ = 0x03;
  iv->OnFocus();
  expect(b->GetInstrument(0x03)->CountObservers() == 1, "back on 03: observed");
  p->song_->phrase_->instr_[16 * 0x02] = 0xFF;
  p->PurgeInstruments(false);
  expect(b->IsSlotEmpty(0x03), "Compact emptied 03");
  iv->ProcessButtonMask(EPBM_A, true);  // no fields left: must not touch them
  iv->OnFocus();
  expect(b->GetInstrument(0x03)->CountObservers() == 1, "after OnFocus the stand-in is observed");
}
#endif

#ifdef SL_FORK
// The sample dialog's import, which the dialog runs on A
class LabImport : public ImportSampleDialog {
public:
  LabImport(View &v) : ImportSampleDialog(v) {}
  void Import(const std::string &file) {
    Path p(file.c_str());
    import(p);
  }
};

static int filledSlots(InstrumentBank *b) {
  int n = 0;
  for (int i = 0; i < MAX_INSTRUMENT_COUNT; i++) n += b->IsSlotEmpty(i) ? 0 : 1;
  return n;
}

static std::string sampleName(InstrumentBank *b, int i) {
  I_Instrument *in = b->GetInstrument(i);
  if (in->GetType() != IT_SAMPLE) return "(not a Sample)";
  return in->FindVariable(SIP_SAMPLE)->GetString();
}

// Phrase screen A,A and LT+(B,A), then sample imports, on up-basic's slots:
// 00-02, 40, 7F Samples, 80-8F MIDI (80 on channel 9), the rest empty
static void modeFlows(Project *p, ViewData *vd, const std::string &root) {
  InstrumentBank *b = p->GetInstrumentBank();
  Song *s = p->song_;
  if (b->FirstEmpty() != 0x03 || filledSlots(b) != 21) die("flows scenario needs up-basic's slots");
  LabWindow *win = new LabWindow(*new LabWindowImp());
  // ViewData leaves playMode_ uninitialised until something plays, and the
  // phrase screen reads it on any button but A (design doc, Appendix B)
  vd->playMode_ = PM_SONG;
  vd->currentPhrase_ = 0xE0;
  clearPhrase(s, 0xE0);
  unsigned char *row = s->phrase_->instr_ + 16 * 0xE0;
  PhraseView *pv = new PhraseView(*win, vd);
  pv->ProcessButtonMask(EPBM_RIGHT, true);  // instrument column

  printf("A,A on an empty instrument cell\n");
  pv->ProcessButtonMask(EPBM_A, true);
  pv->ProcessButtonMask(EPBM_A, true);
  printf("  row 0: %02X\n", row[0]);
  expect(row[0] == 0x03 && b->IsSlotEmpty(0x03), "the lowest empty slot, left empty");
  Player::GetInstance()->Stop();  // the first A auditions

  printf("LT+(B,A) on a row with MIDI 80\n");
  pv->ProcessButtonMask(EPBM_DOWN, true);
  row[1] = 0x80;
  pv->ProcessButtonMask(EPBM_B | EPBM_L, true);
  pv->ProcessButtonMask(EPBM_A | EPBM_L, true);
  printf("  row 1: %02X\n", row[1]);
  expect(row[1] == 0x03 && b->GetInstrument(0x03)->GetType() == IT_MIDI &&
             b->GetInstrument(0x03)->FindVariable(MIP_CHANNEL)->GetInt() == 9,
         "a MIDI clone of 80 (channel 9) in 03");

  printf("LT+(B,A) on a row with empty slot 05\n");
  pv->ProcessButtonMask(EPBM_DOWN, true);
  row[2] = 0x05;
  int before = filledSlots(b);
  pv->ProcessButtonMask(EPBM_B | EPBM_L, true);
  pv->ProcessButtonMask(EPBM_A | EPBM_L, true);
  expect(row[2] == 0x05 && filledSlots(b) == before, "nothing cloned");

  // A sample library under root:, as the dialog lists it
  Path::SetAlias("root", root.c_str());
  mkdir((root + "/samplelib").c_str(), 0777);
  writeWav(root + "/samplelib/x_one.wav", 80);
  writeWav(root + "/samplelib/y_two.wav", 120);
  std::string lib = root + "/samplelib/";

  printf("imports, starting from Sample 00\n");
  vd->currentInstrument_ = 0x00;
  LabImport *dlg = new LabImport(*pv);
  dlg->OnFocus();
  dlg->Import(lib + "x_one.wav");
  printf("  00: %s\n", sampleName(b, 0x00).c_str());
  expect(sampleName(b, 0x00) == "x_one.wav", "the first import goes to 00, as before");
  dlg->Import(lib + "y_two.wav");
  printf("  04: %s\n", sampleName(b, 0x04).c_str());
  expect(sampleName(b, 0x04) == "y_two.wav", "the next import creates a Sample in empty 04");

  printf("import while the next slot holds another type\n");
  b->Create(0x05, IT_PIGTAIL);
  int pool = SamplePool::GetInstance()->GetNameListSize();
  dlg->Import(lib + "x_one.wav");
  expect(b->GetInstrument(0x05)->GetType() == IT_PIGTAIL, "05 keeps its PIGTAIL");
  expect(SamplePool::GetInstance()->GetNameListSize() == pool + 1, "the sample still went to the pool");

  printf("imports with one empty slot left, then none\n");
  for (int i = 0; i < MAX_INSTRUMENT_COUNT; i++) {
    if (b->IsSlotEmpty(i)) b->Create(i, IT_MIDI);
  }
  b->Empty(0x05);
  dlg->Import(lib + "y_two.wav");
  expect(b->GetInstrument(0x05)->GetType() == IT_SAMPLE, "a Sample in 05, the last empty slot");
  pool = SamplePool::GetInstance()->GetNameListSize();
  dlg->Import(lib + "x_one.wav");
  expect(filledSlots(b) == MAX_INSTRUMENT_COUNT && SamplePool::GetInstance()->GetNameListSize() == pool + 1,
         "no slot left: the sample only goes to the pool");
}
#endif

#ifdef SL_FORK
// A MIDI output that keeps whether each note is held, from the messages the
// MIDI service flushes to it
class LabMidiOut : public MidiOutDevice {
public:
  LabMidiOut() : MidiOutDevice("slot_lab"), ons_(0), offs_(0) { memset(held_, 0, sizeof(held_)); }
  virtual bool Init() { return true; }
  virtual void Close() {}
  virtual bool Start() { return true; }
  virtual void Stop() {}
  virtual void SendMessage(MidiMessage &m) {
    int type = m.status_ & 0xF0, ch = m.status_ & 0x0F;
    if (type == MIDI_NOTE_ON && m.data2_ > 0) {
      held_[ch][m.data1_ & 0x7F] = true;
      ons_++;
    } else if (type == MIDI_NOTE_OFF || type == MIDI_NOTE_ON) {
      held_[ch][m.data1_ & 0x7F] = false;
      offs_++;
    }
  }
  int Held() {
    int n = 0;
    for (int c = 0; c < 16; c++) n += HeldOn(c);
    return n;
  }
  // ch counts from 0, as MIP_CHANNEL does
  int HeldOn(int ch) {
    int n = 0;
    for (int k = 0; k < 128; k++) n += held_[ch][k] ? 1 : 0;
    return n;
  }
  void PrintHeld() {
    for (int c = 0; c < 16; c++)
      for (int k = 0; k < 128; k++)
        if (held_[c][k]) printf("  held: MIDI channel %d note %d\n", c + 1, k);
  }
  bool held_[16][128];
  int ons_, offs_;
};

static LabMidiOut *gMidiOut = 0;

class LabMidi : public MidiService {
public:
  virtual void buildDriverList() {
    gMidiOut = new LabMidiOut();
    Insert(gMidiOut);
  }
};

// One button event, as AppWindow sends it: the mask of buttons down after the
// change, and whether it was a press
static void button(InstrumentView *iv, unsigned short mask, bool pressed) {
  iv->ProcessButtonMask(mask, pressed);
}

// Press and release, holding `held` throughout (B,A is button(B), B|A)
static void press(InstrumentView *iv, unsigned short held, unsigned short b) {
  if (held) button(iv, held, true);
  button(iv, held | b, true);
  button(iv, held, false);
  if (held) button(iv, 0, false);
}

static FourCC focusID(InstrumentView *iv) {
  UIIntVarField *f = (UIIntVarField *)iv->GetFocus();
  return f ? f->GetVariableID() : 0;
}

static const char *slotType(InstrumentBank *b, int i) {
  return b->IsSlotEmpty(i) ? "--" : typeName(b->GetInstrument(i)->GetType());
}

// The instrument screen shows the instrument in the current slot, and is its
// one observer
static bool showsSlot(InstrumentBank *b, InstrumentView *iv, ViewData *vd) {
  return b->GetInstrument(vd->currentInstrument_)->CountObservers() == 1 && focusID(iv) == IVF_TYPE;
}

// Small LCG, so the run is the same on every host
static unsigned gSeed = 2026;
static int rnd(int n) {
  gSeed = gSeed * 1103515245u + 12345u;
  return (int)((gSeed >> 16) % (unsigned)n);
}

// Channel 0 plays MIDI 80 (channel 9 in up-basic) on every other row, channel 1
// Sample 00 once per phrase with a table, then rows without an instrument;
// channel 2 the empty slot 03 on every other row; channel 3 MIDI 85 once, then
// rows without an instrument; channel 4 Sample 40. MIDI notes are held until
// the next note (length 00)
static void setupTypes(Project *p) {
  InstrumentBank *b = p->GetInstrumentBank();
  Song *s = p->song_;
  clearSong(s);
  const int slots[5] = {0x80, 0x00, 0x03, 0x85, 0x40};
  for (int c = 0; c < 5; c++) {
    int ph = 0xE0 + c;
    clearChain(s, ph);
    clearPhrase(s, ph);
    setChain(s, ph, 0, ph);
    setSong(s, c, 0, ph);
    for (int r = 0; r < 16; r += 2) {
      bool named = (r == 0) || (c == 0) || (c == 2) || (c == 4);
      setRow(s, ph, r, 0x30 + r, named ? slots[c] : 0xFF);
    }
  }
  setVar(b->GetInstrument(0x00), SIP_TABLE, 0x00);
  Table &t0 = TableHolder::GetInstance()->GetTable(0x00);
  for (int r = 0; r < 16; r++) { t0.cmd1_[r] = I_CMD_VOLM; t0.param1_[r] = 0x0080; }
  TableHolder::GetInstance()->SetUsed(0x00);
}

// The instrument screen's type field and clear confirmation (design doc 5.4)
// on slots the song is playing, under ASan. Needs up-basic's slots: 00-02, 40,
// 7F Samples, 80-8F MIDI, the rest empty
static void modeTypes(Project *p, ViewData *vd) {
  InstrumentBank *b = p->GetInstrumentBank();
  if (b->FirstEmpty() != 0x03 || filledSlots(b) != 21) die("types scenario needs up-basic's slots");
  setupTypes(p);
  unsigned char *phr = p->song_->phrase_->instr_;
  LabWindow *win = new LabWindow(*new LabWindowImp());
  InstrumentView *iv = new InstrumentView(*win, vd);
  MidiService::GetInstance()->SelectDevice("slot_lab");
  Player *player = Player::GetInstance();
  player->OnStartButton(PM_SONG, 0, false, 0);
  pulse(40);
  printf("before any change: %d MIDI note-ons, %d held\n", gMidiOut->ons_, gMidiOut->Held());
  expect(gMidiOut->Held() > 0, "MIDI 80 and 85 hold notes");

  printf("slot 80 (MIDI), played on channel 0\n");
  vd->currentInstrument_ = 0x80;
  iv->OnFocus();
  press(iv, 0, EPBM_UP);
  expect(focusID(iv) == IVF_TYPE, "UP from the first parameter reaches the type field");
  I_Instrument *m80 = b->GetInstrument(0x80);
  press(iv, EPBM_A, EPBM_RIGHT);
  expect(b->GetInstrument(0x80) == m80, "A+RIGHT on a filled slot changes nothing");
  // up-basic's own phrases use 80, 03 and 7F once each; setupTypes adds E0 and E2
  printf("  80 is in %d phrase(s), 03 in %d, 7F in %d, 50 in %d\n", b->ReferenceCount(0x80, phr),
         b->ReferenceCount(0x03, phr), b->ReferenceCount(0x7F, phr), b->ReferenceCount(0x50, phr));
  expect(b->ReferenceCount(0x80, phr) == 2 && b->ReferenceCount(0x03, phr) == 2 &&
             b->ReferenceCount(0x7F, phr) == 1 && b->ReferenceCount(0x50, phr) == 0,
         "reference counts");
  press(iv, EPBM_B, EPBM_A);
  expect(b->GetInstrument(0x80) == m80, "B,A only asks");
  iv->OnClearSlot(false);
  expect(b->GetInstrument(0x80) == m80, "No keeps it");
  int ch80 = m80->FindVariable(MIP_CHANNEL)->GetInt();
  int heldBefore = gMidiOut->HeldOn(ch80);
  press(iv, EPBM_B, EPBM_A);
  iv->OnClearSlot(true);
  expect(b->IsSlotEmpty(0x80), "Yes empties 80");
  expect(showsSlot(b, iv, vd), "the screen shows the empty slot, focus on the type field");
  pulse(40);
  printf("  notes held on 80's MIDI channel %d: %d before the clear, %d after\n", ch80 + 1, heldBefore,
         gMidiOut->HeldOn(ch80));
  expect(heldBefore > 0 && gMidiOut->HeldOn(ch80) == 0, "the clear released 80's note");

  printf("picking a type for 80 in one A hold, while channel 0 plays it\n");
  button(iv, EPBM_A, true);
  const unsigned short steps[4] = {EPBM_RIGHT, EPBM_RIGHT, EPBM_RIGHT, EPBM_LEFT};
  for (int k = 0; k < 4; k++) {
    button(iv, EPBM_A | steps[k], true);
    printf("  %s: 80 is %s\n", steps[k] == EPBM_RIGHT ? "A+RIGHT" : "A+LEFT", slotType(b, 0x80));
    pulse(30);
    button(iv, EPBM_A, false);
  }
  button(iv, 0, false);
  expect(b->GetInstrument(0x80)->GetType() == IT_MIDI &&
             b->GetInstrument(0x80)->FindVariable(MIP_CHANNEL)->GetInt() == 0,
         "SAMPLE, MIDI, PIGTAIL, back to MIDI (channel 0)");
  I_Instrument *picked = b->GetInstrument(0x80);
  press(iv, EPBM_A, EPBM_RIGHT);
  expect(b->GetInstrument(0x80) == picked, "after A is released, A+RIGHT changes nothing");
  expect(showsSlot(b, iv, vd), "the screen shows it");

  printf("slot 00 (Sample with a table running on channel 1)\n");
  press(iv, EPBM_B, EPBM_LEFT);  // 80 to 7F
  vd->currentInstrument_ = 0x00;
  iv->OnFocus();
  expect(focusID(iv) == IVF_TYPE, "the focus stays on the type field across slots");
  bool tableOn = TablePlayback::GetTablePlayback(1).GetTable() != 0;
  press(iv, EPBM_B, EPBM_A);
  iv->OnClearSlot(true);
  printf("  table on channel 1 %s before, %s after\n", tableOn ? "running" : "stopped",
         TablePlayback::GetTablePlayback(1).GetTable() ? "running" : "stopped");
  expect(tableOn && b->IsSlotEmpty(0x00), "00 emptied while its table ran");
  pulse(40);

  // Random clears (Yes or No) and picks on the slots the song plays, and two
  // it doesn't, with pulses in between and while the dialog is open
  printf("random run\n");
  const int pool[7] = {0x00, 0x03, 0x40, 0x80, 0x85, 0x01, 0x8F};
  int clears = 0, kept = 0, picks = 0, warps = 0, bad = 0;
  for (int n = 0; n < 400; n++) {
    int r = rnd(10);
    if (r == 0) {
      press(iv, EPBM_B, rnd(2) ? EPBM_RIGHT : EPBM_LEFT);
      warps++;
    } else {
      int slot = pool[rnd(7)];
      if (vd->currentInstrument_ != slot) {
        vd->currentInstrument_ = slot;
        iv->OnFocus();
      }
    }
    int slot = vd->currentInstrument_;
    if (b->IsSlotEmpty(slot)) {
      button(iv, EPBM_A, true);
      for (int k = 1 + rnd(5); k > 0; k--) {
        button(iv, EPBM_A | (rnd(3) ? EPBM_RIGHT : EPBM_LEFT), true);
        pulse(rnd(8));
        button(iv, EPBM_A, false);
      }
      button(iv, 0, false);
      picks++;
    } else {
      press(iv, EPBM_B, EPBM_A);
      pulse(rnd(4));
      bool yes = rnd(4) != 0;
      iv->OnClearSlot(yes);
      if (yes != b->IsSlotEmpty(slot)) bad++;
      if (yes) clears++;
      else kept++;
    }
    if (!showsSlot(b, iv, vd)) bad++;
    pulse(1 + rnd(15));
  }
  printf("  %d clears, %d kept after No, %d A holds on empty slots, %d B+arrow moves\n", clears, kept,
         picks, warps);
  printf("  slots now:");
  for (int k = 0; k < 7; k++) printf(" %02X=%s", pool[k], slotType(b, pool[k]));
  printf("\n");
  expect(bad == 0, "every step: Yes empties, No keeps, the screen observes the slot it shows");

  player->Stop();
  pulse(25);
  printf("after stop: %d note-ons, %d note-offs, %d notes held\n", gMidiOut->ons_, gMidiOut->offs_,
         gMidiOut->Held());
  gMidiOut->PrintHeld();
  expect(gMidiOut->Held() == 0, "no MIDI note left held");
}
#endif

#ifdef SL_FORK
// ------------------------------------------------------------- resolve

static const char *slotText(int slot) {
  static char buf[8];
  if (slot == CC_UNKNOWN_SLOT) return "unknown";
  sprintf(buf, "%02X", slot);
  return buf;
}

// One resolver case: the slot phrase row `row` plays, reached through song
// row srow of column col, chain row crow of `chain`
static void expectResolve(Song *s, const char *what, int col, int srow, int chain, int crow, int phrase,
                          int row, int want) {
  int got = CommandContext::ResolvePhraseRow(s, col, srow, chain, crow, phrase, row);
  char line[200];
  snprintf(line, sizeof(line), "%s: %s", what, slotText(got));
  if (got != want) snprintf(line + strlen(line), sizeof(line) - strlen(line), " (want %s)", slotText(want));
  expect(got == want, line);
}

// The popup's cells for a context: how many stay lit, and the dimmed ones
static void expectCells(InstrumentBank *b, const CommandContext &cc, const char *what, int wantLit) {
  std::string dimmed;
  int lit = 0;
  for (int i = 1; i < CommandList::GetCount(); i++) {
    FourCC c = CommandList::GetAt(i);
    if (cc.Check(c, b) == CA_AVAILABLE) {
      lit++;
    } else {
      char name[5];
      fourCC2char(c, name);
      name[4] = 0;
      dimmed += std::string(" ") + name;
    }
  }
  char line[300];
  snprintf(line, sizeof(line), "%s: %d of %d lit, dimmed:%s", what, lit, CommandList::GetCount() - 1,
           dimmed.empty() ? " none" : dimmed.c_str());
  expect(lit == wantLit, line);
}

static void expectStatus(InstrumentBank *b, const CommandContext &cc, FourCC c, const char *want) {
  std::string got = cc.GetStatus(c, b);
  char line[200];
  snprintf(line, sizeof(line), "status \"%s\"%s%s%s", got.c_str(), got == want ? "" : " (want \"",
           got == want ? "" : want, got == want ? "" : "\")");
  expect(got == want && got.size() <= CC_STATUS_LENGTH, line);
}

// The command popup's context (design doc 8.2) on up-basic's slots: 00-02, 40,
// 7F Samples, 80-8F MIDI, the rest empty. No playback
static void modeResolve(Project *p, ViewData *vd) {
  InstrumentBank *b = p->GetInstrumentBank();
  Song *s = p->song_;
  if (b->FirstEmpty() != 0x03 || filledSlots(b) != 21) die("resolve scenario needs up-basic's slots");
  clearSong(s);
  for (int i = 0xE0; i <= 0xEC; i++) {
    clearChain(s, i);
    clearPhrase(s, i);
  }
  // Column 0: song rows 0-2 chains E0-E2. E0 plays phrases E0, E1; E1 plays
  // E2, E3 and ends at its empty row 2, so its E4 never plays; E2 plays E5
  setSong(s, 0, 0, 0xE0);
  setSong(s, 0, 1, 0xE1);
  setSong(s, 0, 2, 0xE2);
  setChain(s, 0xE0, 0, 0xE0);
  setChain(s, 0xE0, 1, 0xE1);
  setChain(s, 0xE1, 0, 0xE2);
  setChain(s, 0xE1, 1, 0xE3);
  setChain(s, 0xE1, 3, 0xE4);
  setChain(s, 0xE2, 0, 0xE5);
  setRow(s, 0xE0, 3, 0x30, 0x80);
  setRow(s, 0xE0, 5, 0x30, 0xFF);  // a note without an instrument
  setRow(s, 0xE0, 7, 0xFF, 0x00);  // an instrument without a note
  setRow(s, 0xE0, 9, 0x30, 0x40);
  setRow(s, 0xE1, 2, 0x30, 0x03);  // 03 is empty
  setRow(s, 0xE3, 4, 0x30, 0xFF);
  setRow(s, 0xE4, 8, 0x30, 0x01);

  printf("rows of one phrase (E0, song row 0, chain row 0)\n");
  expectResolve(s, "row 3, note and 80", 0, 0, 0xE0, 0, 0xE0, 3, 0x80);
  expectResolve(s, "row 4, empty, below row 3", 0, 0, 0xE0, 0, 0xE0, 4, 0x80);
  expectResolve(s, "row 5, a note without an instrument", 0, 0, 0xE0, 0, 0xE0, 5, 0x80);
  expectResolve(s, "row 8, below row 7's 00 without a note", 0, 0, 0xE0, 0, 0xE0, 8, 0x80);
  expectResolve(s, "row 9, note and 40", 0, 0, 0xE0, 0, 0xE0, 9, 0x40);
  expectResolve(s, "row 2, nothing above in the song", 0, 0, 0xE0, 0, 0xE0, 2, CC_UNKNOWN_SLOT);

  printf("earlier phrases and chains\n");
  expectResolve(s, "E1 row 0: E0's last, row 9", 0, 0, 0xE0, 1, 0xE1, 0, 0x40);
  expectResolve(s, "E1 row 2: empty slot 03", 0, 0, 0xE0, 1, 0xE1, 2, 0x03);
  expectResolve(s, "E2 (song row 1): chain E0 above, its E1", 0, 1, 0xE1, 0, 0xE2, 15, 0x03);
  expectResolve(s, "E3 row 15: its note without an instrument, then E2, then E1", 0, 1, 0xE1, 1, 0xE3, 15, 0x03);
  expectResolve(s, "E4 row 3: chain E1 ends above it", 0, 1, 0xE1, 3, 0xE4, 3, CC_UNKNOWN_SLOT);
  expectResolve(s, "E4 row 8: its own 01", 0, 1, 0xE1, 3, 0xE4, 8, 0x01);
  expectResolve(s, "E5 (song row 2): E1 skips its unplayed E4 (01)", 0, 2, 0xE2, 0, 0xE5, 15, 0x03);

  printf("links that don't hold\n");
  expectResolve(s, "chain row 1 of E1 isn't E2", 0, 1, 0xE1, 1, 0xE2, 15, CC_UNKNOWN_SLOT);
  expectResolve(s, "song row 2 isn't chain E1", 0, 2, 0xE1, 0, 0xE2, 15, CC_UNKNOWN_SLOT);
  expectResolve(s, "phrase FF", 0, 0, 0xE0, 0, 0xFF, 3, CC_UNKNOWN_SLOT);
  expectResolve(s, "row 16", 0, 0, 0xE0, 0, 0xE0, 16, CC_UNKNOWN_SLOT);
  expectResolve(s, "chain FF: the phrase only", 0, 0, 0xFF, 0, 0xE0, 4, 0x80);

  printf("where the player loops back (column 1: E6, empty row, E7; column 2: E6, E8, E7)\n");
  setChain(s, 0xE6, 0, 0xE6);
  setRow(s, 0xE6, 0, 0x30, 0x85);
  setChain(s, 0xE7, 0, 0xE7);
  setChain(s, 0xE8, 1, 0xE6);  // first row empty
  setSong(s, 1, 0, 0xE6);
  setSong(s, 1, 2, 0xE7);
  setSong(s, 2, 0, 0xE6);
  setSong(s, 2, 1, 0xE8);
  setSong(s, 2, 2, 0xE7);
  expectResolve(s, "after an empty song row", 1, 2, 0xE7, 0, 0xE7, 15, CC_UNKNOWN_SLOT);
  expectResolve(s, "after a chain whose first row is empty", 2, 2, 0xE7, 0, 0xE7, 15, CC_UNKNOWN_SLOT);
  setSong(s, 2, 1, 0xE6);
  expectResolve(s, "the same with E6 in its place", 2, 2, 0xE7, 0, 0xE7, 15, 0x85);

  printf("the longest search (255 song rows of 16 phrases of 16 empty rows)\n");
  for (int r = 0; r < 16; r++) setChain(s, 0xE9, r, 0xEA);
  setChain(s, 0xEB, 0, 0xEC);
  setRow(s, 0xEC, 0, 0x30, 0x7F);
  for (int r = 0; r < SONG_ROW_COUNT; r++) {
    setSong(s, 3, r, 0xE9);
    setSong(s, 4, r, r ? 0xE9 : 0xEB);
  }
  struct timeval t0, t1;
  gettimeofday(&t0, 0);
  expectResolve(s, "column 3, nothing anywhere", 3, 255, 0xE9, 15, 0xEA, 15, CC_UNKNOWN_SLOT);
  expectResolve(s, "column 4, 7F on song row 0", 4, 255, 0xE9, 15, 0xEA, 15, 0x7F);
  gettimeofday(&t1, 0);
  printf("  both searches: %ld us (ASan build)\n", (long)((t1.tv_sec - t0.tv_sec) * 1000000 + t1.tv_usec - t0.tv_usec));

  printf("the phrase screen's position\n");
  vd->songX_ = 0;
  vd->songY_ = 1;
  vd->songOffset_ = 0;
  vd->currentChain_ = 0xE1;
  vd->chainRow_ = 1;
  vd->currentPhrase_ = 0xE3;
  CommandContext pc = CommandContext::ForPhraseRow(vd, 15);
  expect(pc.GetLocation() == CL_PHRASE && pc.GetSlot() == 0x03, "ForPhraseRow: E3 row 15 plays 03");
  vd->songY_ = 0;
  vd->songOffset_ = 1;
  expect(CommandContext::ForPhraseRow(vd, 15).GetSlot() == 0x03, "song row from songOffset_ + songY_");

  printf("the table screen\n");
  vd->currentInstrument_ = 0x40;
  vd->currentTable_ = 0x05;
  expect(CommandContext::ForTable(vd, true).GetSlot() == 0x40, "under the instrument screen: its slot");
  expect(CommandContext::ForTable(vd, false).GetSlot() == CC_UNKNOWN_SLOT, "under the phrase screen, no TABL row: unknown");
  vd->phraseTable_ = 0x05;
  vd->phraseTableSlot_ = 0x80;
  CommandContext tc = CommandContext::ForTable(vd, false);
  expect(tc.GetLocation() == CL_TABLE && tc.GetSlot() == 0x80, "the TABL row's slot while table 05 shows");
  vd->currentTable_ = 0x06;
  expect(CommandContext::ForTable(vd, false).GetSlot() == CC_UNKNOWN_SLOT, "another table: unknown");
  vd->currentInstrument_ = 0xFF;
  expect(CommandContext::ForTable(vd, true).GetSlot() == CC_UNKNOWN_SLOT, "the instrument screen on FF: unknown");

  printf("the screens that open the table screen (E0, song row 0, chain row 0)\n");
  setRow(s, 0xE0, 3, 0x30, 0x80, I_CMD_TABL, 0x0085);  // TABL takes the low 7 bits
  setRow(s, 0xE0, 9, 0x30, 0x40, I_CMD_VOLM, 0x0080, I_CMD_TABL, 0x0007);
  vd->songX_ = 0;
  vd->songY_ = 0;
  vd->songOffset_ = 0;
  vd->currentChain_ = 0xE0;
  vd->chainRow_ = 0;
  vd->currentPhrase_ = 0xE0;
  // ViewData leaves playMode_ uninitialised until something plays, and the
  // phrase screen reads it on any button but A (design doc, Appendix B)
  vd->playMode_ = PM_SONG;
  LabWindow *win = new LabWindow(*new LabWindowImp());
  PhraseView *pv = new PhraseView(*win, vd);
  for (int i = 0; i < 3; i++) pv->ProcessButtonMask(EPBM_DOWN, true);
  pv->ProcessButtonMask(EPBM_R | EPBM_DOWN, true);
  printf("  phrase R+DOWN on row 3: table %02X, slot %s\n", vd->currentTable_, slotText(vd->phraseTableSlot_));
  expect(vd->currentTable_ == 0x05 && CommandContext::ForTable(vd, false).GetSlot() == 0x80,
         "on row 3's TABL 0085 (80): table 05 for 80");
  pv->ProcessButtonMask(EPBM_DOWN, true);
  pv->ProcessButtonMask(EPBM_R | EPBM_DOWN, true);
  expect(vd->currentTable_ == 0x05 && CommandContext::ForTable(vd, false).GetSlot() == CC_UNKNOWN_SLOT,
         "on row 4, no TABL: still table 05, unknown");
  for (int i = 0; i < 5; i++) pv->ProcessButtonMask(EPBM_DOWN, true);
  pv->ProcessButtonMask(EPBM_R | EPBM_DOWN, true);
  expect(vd->currentTable_ == 0x07 && CommandContext::ForTable(vd, false).GetSlot() == 0x40,
         "on row 9's TABL in command column 2 (40): table 07 for 40");
  MixerView *mv = new MixerView(*win, vd);
  mv->ProcessButtonMask(EPBM_R | EPBM_RIGHT, true);
  expect(vd->currentTable_ == 0x07 && CommandContext::ForTable(vd, false).GetSlot() == CC_UNKNOWN_SLOT,
         "mixer R+RIGHT: table 07, unknown");
  setRow(s, 0xE0, 3, 0x30, 0x80);
  setRow(s, 0xE0, 9, 0x30, 0x40);

  printf("dimmed commands\n");
  b->Create(0x05, IT_PIGTAIL);
  expectCells(b, CommandContext(CL_PHRASE, 0x00), "phrase, Sample 00", 22);
  expectCells(b, CommandContext(CL_PHRASE, 0x80), "phrase, MIDI 80", 13);
  expectCells(b, CommandContext(CL_PHRASE, 0x05), "phrase, PIGTAIL 05", 7);
  expectCells(b, CommandContext(CL_PHRASE, 0x03), "phrase, empty 03", 7);
  expectCells(b, CommandContext(CL_PHRASE), "phrase, unknown", 26);
  expectCells(b, CommandContext(CL_TABLE, 0x00), "table, Sample 00", 19);
  expectCells(b, CommandContext(CL_TABLE, 0x80), "table, MIDI 80", 10);
  expectCells(b, CommandContext(CL_TABLE, 0x05), "table, PIGTAIL 05", 4);
  expectCells(b, CommandContext(CL_TABLE, 0x03), "table, empty 03", 4);
  expectCells(b, CommandContext(CL_TABLE), "table, unknown", 23);
  expectCells(b, CommandContext(CL_PHRASE, 0xFF), "phrase, slot FF: as unknown", 26);

  printf("status lines and help\n");
  expectStatus(b, CommandContext(CL_PHRASE), I_CMD_ARPG, "instrument unknown");
  expectStatus(b, CommandContext(CL_PHRASE), I_CMD_IRTG, "tables only");
  expectStatus(b, CommandContext(CL_TABLE, 0x00), I_CMD_TMPO, "phrases only");
  expectStatus(b, CommandContext(CL_PHRASE, 0x80), I_CMD_MDCC, "I80 MIDI");
  expectStatus(b, CommandContext(CL_PHRASE, 0x80), I_CMD_FCUT, "not for I80 MIDI");
  expectStatus(b, CommandContext(CL_PHRASE, 0x05), I_CMD_VOLM, "not for I05 PIGTAIL");
  expectStatus(b, CommandContext(CL_TABLE, 0x7F), I_CMD_CRSH, "I7F SAMPLE");
  expectStatus(b, CommandContext(CL_PHRASE, 0x03), I_CMD_KILL, "I03 empty");
  expectStatus(b, CommandContext(CL_PHRASE, 0x03), I_CMD_VOLM, "I03 is empty");
  const CommandHelp *h = CommandContext(CL_PHRASE, 0x80).GetHelp(I_CMD_VOLM, b);
  expect(h && !strcmp(h->line[0], "VOLuMe:--bb"), "MIDI's own help for VOLM");
  expect(CommandContext(CL_TABLE, 0x81).GetHelp(I_CMD_RTRG, b) != 0, "and for RTRG, in a table too");
  expect(!CommandContext(CL_PHRASE, 0x80).GetHelp(I_CMD_MDCC, b), "none of its own for MDCC");
  expect(!CommandContext(CL_PHRASE, 0x00).GetHelp(I_CMD_VOLM, b), "none for a Sample's VOLM");
  expect(!CommandContext(CL_PHRASE).GetHelp(I_CMD_VOLM, b), "none when the slot is unknown");
}
#endif

// Channel 0 plays one note with a column 1 command whose parameter has low
// nibble 0, and DLAY dd in command column 2. Returns the pulse at which the
// note starts
static int dlayStart(Song *s, int dd) {
  if (dd < 0) setRow(s, 0xE0, 0, 0x30, 0x00, I_CMD_VOLM, 0x0080);
  else setRow(s, 0xE0, 0, 0x30, 0x00, I_CMD_VOLM, 0x0080, I_CMD_DLAY, dd);
  Player *player = Player::GetInstance();
  player->OnStartButton(PM_SONG, 0, false, 0);
  int start = -1;
  for (int i = 0; i < 400 && start < 0; i++) {
    pulse(1);
    if (strcmp(player->GetPlayedNote(0), "  ")) start = i;
  }
  player->Stop();
  return start;
}

// DLAY in command column 2 must delay by its own parameter: DLAY 05 by five
// times what DLAY 01 does
static void modeDlay() {
  Song *s = gProject->song_;
  clearSong(s);
  clearChain(s, 0xE0);
  clearPhrase(s, 0xE0);
  setChain(s, 0xE0, 0, 0xE0);
  for (int c = 0; c < SONG_CHANNEL_COUNT; c++) setSong(s, c, 0, 0xE0);
  int none = dlayStart(s, -1), d1 = dlayStart(s, 1), d5 = dlayStart(s, 5);
  printf("note starts at pulse %d without DLAY, %d with DLAY 01 in column 2, %d with DLAY 05\n", none, d1, d5);
  if (d1 <= none || d5 - none != 5 * (d1 - none)) die("dlay: column 2 DLAY doesn't delay by its own parameter");
}

static void modePlay(const char *dir, const char *scenario) {
  std::string sc = scenario;
  if (sc != "range" && sc != "clone" && sc != "view" && sc != "dlay" && sc != "flows" && sc != "types" &&
      sc != "resolve")
    die("unknown scenario ", scenario);
#ifndef SL_FORK
  if (sc == "flows" || sc == "types" || sc == "resolve") die("flows, types and resolve scenarios: fork only");
#else
  if (sc == "types") {
    // Before the project exists: MidiInstrument keeps the first service it sees
    MidiService::Install(new LabMidi());
    MidiService::GetInstance()->Init();
    gMidiTicks = true;
  }
#endif
  setvbuf(stdout, 0, _IONBF, 0);  // keep the output if a sanitizer aborts
  gOut = new LabAudioOut();
  Audio::GetInstance()->Insert(gOut);
  Project *project = loadProject(dir);
  if (sc == "view") {
#ifdef SL_FORK
    printf("# slot_lab play %s, tree %s\n", scenario, SL_TREE);
    modeView(project);
    if (gFailed) die("view scenario: checks failed");
    return;
#else
    die("view scenario: fork only");
#endif
  }
  if (sc == "range") setupRange(project);
  else if (sc == "clone") setupClone(project);
  ViewData *viewData = new ViewData(project);
  Player *player = Player::GetInstance();
  if (!player->Init(project, viewData)) die("player init failed");
  printf("# slot_lab play %s, tree %s\n", scenario, SL_TREE);
  if (sc == "dlay") {
    modeDlay();
    return;
  }
#ifdef SL_FORK
  if (sc == "flows") {
    modeFlows(project, viewData, absPath(dir));
    if (gFailed) die("flows scenario: checks failed");
    return;
  }
  if (sc == "types") {
    modeTypes(project, viewData);
    if (gFailed) die("types scenario: checks failed");
    return;
  }
  if (sc == "resolve") {
    modeResolve(project, viewData);
    if (gFailed) die("resolve scenario: checks failed");
    return;
  }
#endif
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
    fprintf(stderr, "usage: slot_lab dump|mem <projdir> | save <projdir> <outdir> | xml <file> <out>\n"
                    "       | gen basic|clones|tables <projdir> | lz <in> <out>\n"
                    "       | play <projdir> range|clone|view|dlay|flows|types|resolve\n");
    return 2;
  }
  std::string mode = argv[1];
  if (mode == "lz" && argc == 4) { modeLz(argv[2], argv[3]); _exit(0); }
  install();
  if (mode == "xml" && argc == 4) { modeXml(argv[2], argv[3]); _exit(0); }
  if (mode == "dump") modeDump(argv[2]);
  else if (mode == "save" && argc == 4) { loadProject(argv[2]); saveProject(argv[3]); }
  else if (mode == "mem") modeMem(argv[2]);
  else if (mode == "gen" && argc == 4) modeGen(argv[2], argv[3]);
  else if (mode == "play" && argc == 4) modePlay(argv[2], argv[3]);
  else die("bad arguments for mode ", argv[1]);
  fflush(stdout);
  _exit(0);  // skip static teardown of the app singletons
}
