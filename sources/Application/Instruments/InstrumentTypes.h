#ifndef _INSTRUMENT_TYPES_H_
#define _INSTRUMENT_TYPES_H_

#include "I_Instrument.h"

// The instrument type registry: one row per type in InstrumentTypes.cpp.
// Adding a type: an IT_* value before IT_LAST (I_Instrument.h), the class,
// a row in the table and the object in COMMONFILES (projects/Makefile).
// The instrument screen still needs a fill function for it (InstrumentView).

struct InstrumentTypeInfo {
	InstrumentType type ;
	const char *saveName ;            // TYPE attribute in the save file
	I_Instrument *(*create)(int slot) ;
	int legacyFirst,legacyCount ;     // upstream slots for an entry with no
	                                  // TYPE; -1,0 if none
} ;

class InstrumentTypes {
public:
	// 0 for IT_LAST (the empty stand-in) or a type without a row
	static const InstrumentTypeInfo *Get(InstrumentType type) ;
	// 0 if no type saves under that name
	static const InstrumentTypeInfo *FindBySaveName(const char *name) ;
	// The type an entry with no TYPE gets in that slot; 0 if none
	static const InstrumentTypeInfo *FindByLegacySlot(int slot) ;
} ;

#endif
