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
	// Never null: slot numbers outside the bank get the empty stand-in
	I_Instrument *GetInstrument(int i) ;
	virtual void SaveContent(TiXmlNode *node);
	virtual void RestoreContent(TiXmlElement *element);
	void Init() ;
	void OnStart() ;
	unsigned short GetNext() ;
	unsigned short Clone(unsigned short i) ;
private:
	void replace(int i,I_Instrument *instrument) ;

	I_Instrument *instrument_[MAX_INSTRUMENT_COUNT] ;
	EmptyInstrument empty_ ;
} ;

#endif
