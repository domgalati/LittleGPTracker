#ifndef _PIGTAIL_INSTRUMENT_H_
#define _PIGTAIL_INSTRUMENT_H_

#include "I_Instrument.h"
#include "Application/Model/Song.h"

#define PTIP_VOLUME MAKE_FOURCC('V','O','L','M')

// Sine lookup: 2^PIGTAIL_SINE_BITS entries indexed by the top bits of a
// 32 bit phase accumulator
#define PIGTAIL_SINE_BITS 10
#define PIGTAIL_SINE_SIZE (1<<PIGTAIL_SINE_BITS)

class PigtailInstrument:public I_Instrument {

public:
	PigtailInstrument() ;
	virtual ~PigtailInstrument() ;

	virtual bool Init() ;

	// Start & stop the instument
	virtual bool Start(int channel, unsigned char note, int flags = 1);
	virtual void Stop(int channel) ;

	// size refers to the number of samples
	// fills interleaved stereo Q15 fixed point
	virtual bool Render(int channel, fixed *buffer, int size, int flags);
	virtual void ProcessCommand(int channel,FourCC cc,ushort value) ;

	virtual bool IsInitialized() { return true ; } ;

	virtual bool IsEmpty() { return false ; } ;

	virtual InstrumentType GetType() { return IT_PIGTAIL ; } ;

	virtual const char *GetName() ;

	virtual void OnStart() ;

	virtual void Purge() {} ;

	virtual int GetTable() ;
	virtual bool GetTableAutomation() ;
	virtual void GetTableState(TableSaveState &state) ;
	virtual void SetTableState(TableSaveState &state) ;

private:
	static void initSineTable() ;

	Variable *volume_ ;
	TableSaveState tableState_ ;

	// Per channel voice state (one instrument can play on several channels)
	unsigned int phase_[SONG_CHANNEL_COUNT] ;
	unsigned int phaseInc_[SONG_CHANNEL_COUNT] ;

	// +1 guard entry so interpolation never wraps the index
	static short sineTable_[PIGTAIL_SINE_SIZE+1] ;
	static bool sineTableReady_ ;
} ;

#endif
