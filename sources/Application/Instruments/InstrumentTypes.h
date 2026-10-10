#ifndef _INSTRUMENT_TYPES_H_
#define _INSTRUMENT_TYPES_H_

#include "I_Instrument.h"

// The instrument type registry: one row per type in InstrumentTypes.cpp.
// Adding a type: an IT_* value before IT_LAST (I_Instrument.h), the class,
// a row in the table and the object in COMMONFILES (projects/Makefile).
// The instrument screen still needs a fill function for it (InstrumentView).

// Help for a command that a type reads differently from the text in
// HelpLegend.h: the same three lines the command popup shows
struct CommandHelp {
	FourCC command ;
	const char *line[3] ;
} ;

struct InstrumentTypeInfo {
	InstrumentType type ;
	const char *saveName ;            // TYPE attribute in the save file
	const char *uiName ;              // shown in the instrument screen's type
	                                  // field and its messages
	I_Instrument *(*create)(int slot) ;
	int legacyFirst,legacyCount ;     // upstream slots for an entry with no
	                                  // TYPE; -1,0 if none
	const FourCC *commands ;          // the commands its ProcessCommand acts
	                                  // on, I_CMD_NONE-terminated. Kept in the
	                                  // type's .cpp, next to ProcessCommand
	const CommandHelp *help ;         // its own help texts, I_CMD_NONE-
	                                  // terminated; 0 if none
} ;

class InstrumentTypes {
public:
	// Rows in table order, which is the order the type field steps through
	static int Count() ;
	static const InstrumentTypeInfo *At(int index) ;
	// 0 for IT_LAST (the empty stand-in) or a type without a row
	static const InstrumentTypeInfo *Get(InstrumentType type) ;
	// 0 if no type saves under that name
	static const InstrumentTypeInfo *FindBySaveName(const char *name) ;
	// The type an entry with no TYPE gets in that slot; 0 if none
	static const InstrumentTypeInfo *FindByLegacySlot(int slot) ;
	// Whether the type's ProcessCommand acts on the command
	static bool Implements(const InstrumentTypeInfo *info,FourCC command) ;
	// The type's own help for the command; 0 if HelpLegend.h's applies
	static const CommandHelp *FindHelp(const InstrumentTypeInfo *info,
	                                   FourCC command) ;
} ;

#endif
