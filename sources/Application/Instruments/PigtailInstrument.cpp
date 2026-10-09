#include "PigtailInstrument.h"
#include "Services/Audio/Audio.h"
#include <string.h>
#include <math.h>

short PigtailInstrument::sineTable_[PIGTAIL_SINE_SIZE+1] ;
bool PigtailInstrument::sineTableReady_=false ;

PigtailInstrument::PigtailInstrument() {

	initSineTable() ;

	volume_=new Variable("volume",PTIP_VOLUME,0x80) ;
	Insert(volume_) ;

	for (int i=0;i<SONG_CHANNEL_COUNT;i++) {
		phase_[i]=0 ;
		phaseInc_[i]=0 ;
	}
}

PigtailInstrument::~PigtailInstrument() {
} ;

void PigtailInstrument::initSineTable() {
	if (sineTableReady_) return ;
	const double twoPi=6.28318530717958647692 ;
	for (int i=0;i<=PIGTAIL_SINE_SIZE;i++) {
		sineTable_[i]=short(floor(sin(twoPi*i/PIGTAIL_SINE_SIZE)*32767.0+0.5)) ;
	}
	sineTableReady_=true ;
} ;

bool PigtailInstrument::Init() {
	tableState_.Reset() ;
	return true ;
} ;

void PigtailInstrument::OnStart() {
	tableState_.Reset() ;
} ;

bool PigtailInstrument::Start(int channel, unsigned char note, int flags) {

	// Pitch is derived from the driver rate at note start; the render loop
	// itself only does integer math on the phase accumulator

	double driverRate=double(Audio::GetInstance()->GetSampleRate()) ;
	double freq=440.0*pow(2.0,(int(note)-69)/12.0) ;
	phaseInc_[channel]=(unsigned int)(freq/driverRate*4294967296.0) ;

	// flags & 1: retrigger, restart the waveform at zero crossing
	if (flags&1) {
		phase_[channel]=0 ;
	}
	return true ;
} ;

void PigtailInstrument::Stop(int channel) {
	// No release stage yet: the channel stops calling Render
} ;

bool PigtailInstrument::Render(int channel, fixed *buffer, int size, int flags) {

	unsigned int phase=phase_[channel] ;
	unsigned int inc=phaseInc_[channel] ;
	int volume=volume_->GetInt() ; // 0x00-0xFF

	const int fracShift=32-PIGTAIL_SINE_BITS-15 ;
	fixed *dst=buffer ;

	for (int i=0;i<size;i++) {
		int index=phase>>(32-PIGTAIL_SINE_BITS) ;
		int frac=(phase>>fracShift)&0x7FFF ;
		int a=sineTable_[index] ;
		int b=sineTable_[index+1] ;
		int s=a+(((b-a)*frac)>>15) ;
		s=(s*volume)>>8 ;

		// 16 bit sample -> Q15 fixed, same value on both channels
		fixed out=i2fp(s) ;
		*dst++=out ;
		*dst++=out ;
		phase+=inc ;
	}

	phase_[channel]=phase ;
	return true ;
} ;

void PigtailInstrument::ProcessCommand(int channel, FourCC cc, ushort value) {
	// No commands supported yet
} ;

const char *PigtailInstrument::GetName() {
	return "PIGTAIL SINE" ;
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
