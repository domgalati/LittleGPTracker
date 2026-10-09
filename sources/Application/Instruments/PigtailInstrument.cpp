#include "PigtailInstrument.h"
#include "Services/Audio/Audio.h"
#include "braids/macro_oscillator.h"
#include "braids/host_rate.h"
#include <string.h>
#include <math.h>

// MacroOscillator::Render handles at most 24 samples per call
#define BRAIDS_BLOCK_SIZE 24

// Braids pitch unit: 1/128 semitone, MIDI note n == n<<7
#define BRAIDS_SEMITONE 128

// Shape names are what gets saved: keep them stable. Order follows
// braids::MacroOscillatorShape (names after the Braids module display).
// A saved name that isn't in this list loads as the first entry.

static const char *shapeNames[]= {
	"csaw","morph","saw/sq","fold","buzz",
	"sub sq","sub saw","sync sq","sync saw",
	"saw x3","sq x3","tri x3","sine x3","ring",
	"swarm","saw comb","toy",
	"zlpf","zpkf","zbpf","zhpf",
	"vosim","vowel","vow fof","harmonic",
	"fm","fb fm","wt fm",
	"pluck","bowed","blown","fluted",
	"bell","drum","kick","cymbal","snare",
	"wtbl","wmap","wline","wt x4",
	"noise","twin q","clk noise","cloud","particle",
	"qpsk","qmark"
} ;

#define PIGTAIL_SHAPE_COUNT (int(sizeof(shapeNames)/sizeof(shapeNames[0])))

typedef char pigtailShapeCountCheck[
	(PIGTAIL_SHAPE_COUNT==int(braids::MACRO_OSC_SHAPE_LAST))?1:-1] ;

// One voice per song channel. A channel plays a single instrument at a time,
// so voices are shared by all pigtail instruments (as SampleInstrument does
// with its per channel static state).

struct PigtailVoice {
	braids::MacroOscillator osc ;
	int32_t fifo[BRAIDS_BLOCK_SIZE] ; // last rendered block, DC blocked
	int fifoPos ;                     // next unread sample in fifo
	int16_t pitch ;
	int32_t dcX1 ;                    // DC blocker: previous input
	int32_t dcY1 ;                    // DC blocker: previous output (Q8)
	bool initialized ;
} ;

static PigtailVoice voices_[SONG_CHANNEL_COUNT] ;
static const uint8_t noSync_[BRAIDS_BLOCK_SIZE]= { 0 } ;

// Per driver rate state, recomputed only when the rate changes:
// - Braids' host rate (braids/host_rate.h): pitch offset so phase increments
//   computed for 96kHz give the right frequency at the driver rate, plus the
//   rescaled constants of rate dependent models
// - pole of the per voice DC blocker (one pole high pass, Q15)

#define PIGTAIL_DC_CUTOFF 15.0 // Hz

static int dcPoleRate_=0 ;
static int32_t dcPole_=0 ;

static void updateRateState() {
	int rate=Audio::GetInstance()->GetSampleRate() ;
	if (rate!=braids::host_rate.rate) {
		braids::SetHostSampleRate(rate) ;
	}
	if (rate!=dcPoleRate_) {
		double pole=exp(-2.0*3.14159265358979323846*PIGTAIL_DC_CUTOFF/rate) ;
		dcPole_=int32_t(floor(pole*32768.0+0.5)) ;
		dcPoleRate_=rate ;
	}
}

static void initVoice(PigtailVoice &v) {
	updateRateState() ;
	v.osc.Init() ;
	v.osc.set_shape(braids::MACRO_OSC_SHAPE_CSAW) ;
	v.fifoPos=BRAIDS_BLOCK_SIZE ; // empty, forces a render on first use
	v.pitch=60*BRAIDS_SEMITONE ;
	v.dcX1=0 ;
	v.dcY1=0 ;
	v.initialized=true ;
}

// Render one Braids block into the voice FIFO, removing DC on the way:
// y[n] = x[n] - x[n-1] + pole * y[n-1]
static void renderBlock(PigtailVoice &v,int shape,int16_t timbre,int16_t color) {
	int16_t block[BRAIDS_BLOCK_SIZE] ;
	v.osc.set_shape(braids::MacroOscillatorShape(shape)) ;
	v.osc.set_pitch(v.pitch) ;
	v.osc.set_parameters(timbre,color) ;
	v.osc.Render(noSync_,block,BRAIDS_BLOCK_SIZE) ;

	int32_t x1=v.dcX1 ;
	int32_t y1=v.dcY1 ;
	for (int i=0;i<BRAIDS_BLOCK_SIZE;i++) {
		int32_t x=block[i] ;
		y1=((x-x1)<<8)+int32_t((long long)(y1)*dcPole_>>15) ;
		x1=x ;
		v.fifo[i]=(y1+128)>>8 ;
	}
	v.dcX1=x1 ;
	v.dcY1=y1 ;
	v.fifoPos=0 ;
}

PigtailInstrument::PigtailInstrument() {

	shape_=new Variable("shape",PTIP_SHAPE,shapeNames,PIGTAIL_SHAPE_COUNT,0) ;
	Insert(shape_) ;

	timbre_=new Variable("timbre",PTIP_TIMBRE,0x80) ;
	Insert(timbre_) ;

	color_=new Variable("color",PTIP_COLOR,0x80) ;
	Insert(color_) ;

	// Braids models peak close to full scale: default to about -12dB
	volume_=new Variable("volume",PTIP_VOLUME,0x40) ;
	Insert(volume_) ;
}

PigtailInstrument::~PigtailInstrument() {
} ;

bool PigtailInstrument::Init() {
	tableState_.Reset() ;
	return true ;
} ;

void PigtailInstrument::OnStart() {
	tableState_.Reset() ;
} ;

bool PigtailInstrument::Start(int channel, unsigned char note, int flags) {

	PigtailVoice &v=voices_[channel] ;
	if (!v.initialized) {
		initVoice(v) ;
	}

	// The oscillator runs at the driver rate: shift the requested pitch by
	// the 96kHz/driver rate ratio instead of resampling Braids' output

	updateRateState() ;
	int pitch=int(note)*BRAIDS_SEMITONE+braids::host_rate.pitch_offset ;
	if (pitch<0) pitch=0 ;
	if (pitch>32767) pitch=32767 ;
	v.pitch=int16_t(pitch) ;

	// flags & 1: retrigger. The oscillator keeps running (and the FIFO keeps
	// its samples) so the waveform stays continuous across notes
	if (flags&1) {
		v.osc.Strike() ;
	}
	return true ;
} ;

void PigtailInstrument::Stop(int channel) {
	// No release stage yet: the channel stops calling Render
} ;

bool PigtailInstrument::Render(int channel, fixed *buffer, int size, int flags) {

	PigtailVoice &v=voices_[channel] ;
	if (!v.initialized) {
		initVoice(v) ;
	}

	int shape=shape_->GetInt() ;
	if ((shape<0)||(shape>=PIGTAIL_SHAPE_COUNT)) shape=0 ;
	int16_t timbre=int16_t(timbre_->GetInt()<<7) ; // 0x00-0xFF -> 0-32640
	int16_t color=int16_t(color_->GetInt()<<7) ;
	int volume=volume_->GetInt() ; // 0x00-0xFF

	fixed *dst=buffer ;
	int remaining=size ;

	while (remaining>0) {
		if (v.fifoPos>=BRAIDS_BLOCK_SIZE) {
			renderBlock(v,shape,timbre,color) ;
		}
		int count=BRAIDS_BLOCK_SIZE-v.fifoPos ;
		if (count>remaining) count=remaining ;

		const int32_t *src=v.fifo+v.fifoPos ;
		for (int i=0;i<count;i++) {
			// mono 16 bit -> Q15 fixed, same value on both channels
			fixed out=i2fp((src[i]*volume)>>8) ;
			*dst++=out ;
			*dst++=out ;
		}
		v.fifoPos+=count ;
		remaining-=count ;
	}
	return true ;
} ;

void PigtailInstrument::ProcessCommand(int channel, FourCC cc, ushort value) {
	// No commands supported yet
} ;

const char *PigtailInstrument::GetName() {
	return "PIGTAIL BRAIDS" ;
} ;

int PigtailInstrument::GetTable() {
	return VAR_OFF ;
} ;

bool PigtailInstrument::GetTableAutomation() {
	return false ;
} ;

void PigtailInstrument::GetTableState(TableSaveState &state) {
	memcpy(state.hopCount_,tableState_.hopCount_,sizeof(uchar)*TABLE_STEPS*3) ;
	memcpy(state.position_,tableState_.position_,sizeof(int)*3) ;
} ;

void PigtailInstrument::SetTableState(TableSaveState &state) {
	memcpy(tableState_.hopCount_,state.hopCount_,sizeof(uchar)*TABLE_STEPS*3) ;
	memcpy(tableState_.position_,state.position_,sizeof(int)*3) ;
} ;
