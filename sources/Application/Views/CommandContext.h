#ifndef _COMMAND_CONTEXT_H_
#define _COMMAND_CONTEXT_H_

#include "Application/Instruments/InstrumentBank.h"
#include "Application/Instruments/InstrumentTypes.h"
#include "Application/Views/ViewData.h"
#include <string>

// What the command popup knows about the cell it edits
// (docs/design/instrument-slots.md, 8.2): whether the cell is in a phrase or
// a table, and which instrument slot its commands go to. A hint, not a
// simulation: it ignores HOP, live mode, and that a DLAY row's commands
// reach the channel before the row's note does.

#define CC_UNKNOWN_SLOT -1

// Longest status line: the popup's inner width (CommandSelectorCommon.h)
#define CC_STATUS_LENGTH 25

enum CommandLocation {
	CL_PHRASE,
	CL_TABLE
} ;

enum CommandAvailability {
	CA_AVAILABLE,      // may do something there
	CA_WRONG_LOCATION, // a player command for phrases only or tables only
	CA_NOT_FOR_TYPE    // the slot's type ignores it, or the slot is empty
} ;

class CommandContext {
public:
	// A slot outside the bank (FF) counts as unknown
	CommandContext(CommandLocation location=CL_PHRASE,int slot=CC_UNKNOWN_SLOT) ;

	CommandLocation GetLocation() const ;
	// CC_UNKNOWN_SLOT if unknown
	int GetSlot() const ;
	// The slot's instrument, the bank's empty stand-in for an empty slot;
	// 0 if the slot is unknown
	I_Instrument *GetInstrument(InstrumentBank *bank) const ;

	CommandAvailability Check(FourCC command,InstrumentBank *bank) const ;
	// The slot's type's own help for the command; 0 if HelpLegend.h's applies
	const CommandHelp *GetHelp(FourCC command,InstrumentBank *bank) const ;
	// One line for the popup: why the command is dimmed, otherwise the slot
	// and its type. At most CC_STATUS_LENGTH characters
	std::string GetStatus(FourCC command,InstrumentBank *bank) const ;

	// The slot that plays phrase row 'row' when the phrase is reached through
	// row chainRow of the chain in songRow of song column songColumn: the
	// first row from 'row' up with a note and an instrument number (a row
	// without a note starts nothing), then earlier phrases of the chain, then
	// earlier chains of the song column, back to where the player would loop
	// to. A link that doesn't hold (the chain row isn't that phrase, the song
	// row isn't that chain) ends the search. CC_UNKNOWN_SLOT if nothing found
	static int ResolvePhraseRow(Song *song,int songColumn,int songRow,
	                            int chain,int chainRow,int phrase,int row) ;
	// Phrase row 'row' of the phrase screen's position in viewData
	static CommandContext ForPhraseRow(ViewData *viewData,int row) ;
	// The table screen: under the instrument screen, the instrument it shows;
	// under the phrase screen, the slot of the phrase row whose TABL opened
	// the table (ViewData::phraseTable_), while the screen still shows that
	// table; otherwise unknown
	static CommandContext ForTable(ViewData *viewData,bool underInstrument) ;

private:
	CommandLocation location_ ;
	int slot_ ;
} ;

#endif
