#ifndef _INSTRUMENT_TYPES_H_
#define _INSTRUMENT_TYPES_H_

#include "I_Instrument.h"

// The instrument type registry: one row per type in InstrumentTypes.cpp.
// Adding a type: an IT_* value before IT_LAST (I_Instrument.h), the class,
// a row in the table and the object in COMMONFILES (projects/Makefile).
// With fields set, the instrument screen needs no code for it; without,
// InstrumentView needs a fill function (Sample and MIDI have theirs).

// Help for a command that a type reads differently from the text in
// HelpLegend.h: the same three lines the command popup shows
struct CommandHelp {
	FourCC command ;
	const char *line[3] ;
} ;

// One field of a type's instrument screen: a UIIntVarField over the
// instrument's variable. Data only, so this directory doesn't depend on the
// views. Rows end with variable 0
struct InstrumentField {
	FourCC variable ;
	int x,y ;                // from the position of the screen's first field
	const char *format ;     // as UIIntVarField draws it: "%s" for a list,
	                         // "%d" or "%2.2X" (or both) for a number
	int min,max ;            // max -1: the variable's last list entry
	int fine,coarse ;        // steps for A+LEFT/RIGHT and A+UP/DOWN
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
	const InstrumentField *fields ;   // its instrument screen; 0 if
	                                  // InstrumentView has a fill function
	                                  // for it. Kept in the type's .cpp
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
