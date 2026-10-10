#ifndef _PIGTAIL_INSTRUMENT_H_
#define _PIGTAIL_INSTRUMENT_H_

#include "I_Instrument.h"
#include "Application/Model/Song.h"

#define PTIP_VOLUME MAKE_FOURCC('V','O','L','M')
#define PTIP_SHAPE  MAKE_FOURCC('S','H','A','P')
#define PTIP_TIMBRE MAKE_FOURCC('T','M','B','R')
#define PTIP_COLOR  MAKE_FOURCC('C','O','L','R')

// Synth voice driven by the Braids macro oscillator by Émilie Gillet
// (github.com/pichenettes/eurorack, MIT license; ported in
// sources/Externals/Braids). Braids renders fixed blocks of mono int16 at
// its own rate; each song channel keeps a small FIFO of rendered samples so
// LGPT's variable sized Render calls are served from a continuous stream.

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
	Variable *volume_ ;
	Variable *shape_ ;
	Variable *timbre_ ;
	Variable *color_ ;
	TableSaveState tableState_ ;
} ;

#endif
