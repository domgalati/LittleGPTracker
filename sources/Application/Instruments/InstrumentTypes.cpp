#include "InstrumentTypes.h"
#include "Application/Instruments/CommandList.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/MidiInstrument.h"
#include "Application/Instruments/PigtailInstrument.h"
#include <string.h>

static I_Instrument *createSample(int) {
	return new SampleInstrument() ;
}

// Upstream's MIDI slots 80-8F play on channels 0-15
static I_Instrument *createMidi(int slot) {
	MidiInstrument *m=new MidiInstrument() ;
	m->SetChannel(slot&0x0F) ;
	return m ;
}

static I_Instrument *createPigtail(int) {
	return new PigtailInstrument() ;
}

// Save names must stay stable: they're the TYPE attribute in project files
static const InstrumentTypeInfo types[]= {
	{IT_SAMPLE,"Sample","SAMPLE",createSample,0x00,0x80,
		SampleInstrument::Commands,0},
	{IT_MIDI,"Midi","MIDI",createMidi,0x80,0x10,
		MidiInstrument::Commands,MidiInstrument::Help},
	{IT_PIGTAIL,"Pigtail","PIGTAIL",createPigtail,-1,0,
		PigtailInstrument::Commands,0}
} ;

#define INSTRUMENT_TYPE_COUNT (int(sizeof(types)/sizeof(types[0])))

typedef char instrumentTypeCountCheck[
	(INSTRUMENT_TYPE_COUNT==int(IT_LAST))?1:-1] ;

int InstrumentTypes::Count() {
	return INSTRUMENT_TYPE_COUNT ;
} ;

const InstrumentTypeInfo *InstrumentTypes::At(int index) {
	if ((index<0)||(index>=INSTRUMENT_TYPE_COUNT)) {
		return 0 ;
	}
	return &types[index] ;
} ;

const InstrumentTypeInfo *InstrumentTypes::Get(InstrumentType type) {
	for (int i=0;i<INSTRUMENT_TYPE_COUNT;i++) {
		if (types[i].type==type) {
			return &types[i] ;
		}
	}
	return 0 ;
} ;

const InstrumentTypeInfo *InstrumentTypes::FindBySaveName(const char *name) {
	for (int i=0;i<INSTRUMENT_TYPE_COUNT;i++) {
		if (!strcmp(name,types[i].saveName)) {
			return &types[i] ;
		}
	}
	return 0 ;
} ;

const InstrumentTypeInfo *InstrumentTypes::FindByLegacySlot(int slot) {
	for (int i=0;i<INSTRUMENT_TYPE_COUNT;i++) {
		const InstrumentTypeInfo &t=types[i] ;
		if ((slot>=t.legacyFirst)&&(slot<t.legacyFirst+t.legacyCount)) {
			return &t ;
		}
	}
	return 0 ;
} ;

bool InstrumentTypes::Implements(const InstrumentTypeInfo *info,
                                 FourCC command) {
	if ((!info)||(command==I_CMD_NONE)) {
		return false ;
	}
	for (const FourCC *c=info->commands;*c!=I_CMD_NONE;c++) {
		if (*c==command) {
			return true ;
		}
	}
	return false ;
} ;

const CommandHelp *InstrumentTypes::FindHelp(const InstrumentTypeInfo *info,
                                             FourCC command) {
	if ((!info)||(!info->help)||(command==I_CMD_NONE)) {
		return 0 ;
	}
	for (const CommandHelp *h=info->help;h->command!=I_CMD_NONE;h++) {
		if (h->command==command) {
			return h ;
		}
	}
	return 0 ;
} ;
