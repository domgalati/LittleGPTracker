#ifndef _INSTRUMENT_BANK_H_
#define _INSTRUMENT_BANK_H_

#include "Application/Persistency/Persistent.h"
#include "Application/Model/Song.h"
#include "Application/Instruments/I_Instrument.h"
#include "Application/Instruments/EmptyInstrument.h"

#define NO_MORE_INSTRUMENT 0x100

class InstrumentBank: public Persistent {
public:
	InstrumentBank() ;
	~InstrumentBank() ;
	void AssignDefaults() ;
	// Never null: empty slots and numbers outside the bank get the empty
	// stand-in
	I_Instrument *GetInstrument(int i) ;
	bool IsSlotEmpty(int i) ;
	virtual void SaveContent(TiXmlNode *node);
	virtual void RestoreContent(TiXmlElement *element);
	// Load rule that needs the whole file: run after the project has loaded
	void RestoreLegacyMidi(const unsigned char *phraseInstruments) ;
	void Init() ;
	void OnStart() ;
	// Lowest empty slot, or NO_MORE_INSTRUMENT
	unsigned short FirstEmpty() ;
	// A new instrument of that type in slot i, replacing what was there.
	// 0 for a number outside the bank or a type without a registry row
	I_Instrument *Create(int i,InstrumentType type) ;
	void Empty(int i) ;
	// Number of phrases with a row that names slot i
	int ReferenceCount(int i,const unsigned char *phraseInstruments) ;
	// Into the lowest empty slot
	unsigned short Clone(unsigned short i) ;
private:
	void replace(int i,I_Instrument *instrument) ;

	I_Instrument *instrument_[MAX_INSTRUMENT_COUNT] ;
	EmptyInstrument empty_ ;
	// LAYOUT attribute of the loaded file: 1 for files without one
	int layout_ ;
} ;

#endif
