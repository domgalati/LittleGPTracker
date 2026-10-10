#include "CommandContext.h"
#include "Application/Instruments/CommandList.h"
#include "Application/Player/Player.h"
#include "Application/Player/TablePlayback.h"
#include <stdio.h>

CommandContext::CommandContext(CommandLocation location,int slot):
	location_(location),
	slot_(((slot>=0)&&(slot<MAX_INSTRUMENT_COUNT))?slot:CC_UNKNOWN_SLOT) {
} ;

CommandLocation CommandContext::GetLocation() const {
	return location_ ;
} ;

int CommandContext::GetSlot() const {
	return slot_ ;
} ;

I_Instrument *CommandContext::GetInstrument(InstrumentBank *bank) const {
	if (slot_==CC_UNKNOWN_SLOT) {
		return 0 ;
	}
	return bank->GetInstrument(slot_) ;
} ;

static bool listed(const FourCC *list,FourCC command) {
	for (;*list!=I_CMD_NONE;list++) {
		if (*list==command) {
			return true ;
		}
	}
	return false ;
}

// A phrase cell's commands go first to the player, then to the instrument
// sounding on the channel; a table cell's to the table player, then to the
// instrument the table runs for. Player commands of the other location reach
// only the instrument, and none implements them
CommandAvailability CommandContext::Check(FourCC command,
                                          InstrumentBank *bank) const {
	if (command==I_CMD_NONE) {
		return CA_AVAILABLE ;
	}
	bool inPhrase=(location_==CL_PHRASE) ;
	const FourCC *here=inPhrase?Player::PhraseCommands:TablePlayback::TableCommands ;
	const FourCC *there=inPhrase?TablePlayback::TableCommands:Player::PhraseCommands ;
	if (listed(here,command)) {
		return CA_AVAILABLE ;
	}
	I_Instrument *instrument=GetInstrument(bank) ;
	if ((instrument)&&(InstrumentTypes::Implements(
	                       InstrumentTypes::Get(instrument->GetType()),command))) {
		return CA_AVAILABLE ;
	}
	if (listed(there,command)) {
		return CA_WRONG_LOCATION ;
	}
	return instrument?CA_NOT_FOR_TYPE:CA_AVAILABLE ;
} ;

const CommandHelp *CommandContext::GetHelp(FourCC command,
                                           InstrumentBank *bank) const {
	I_Instrument *instrument=GetInstrument(bank) ;
	if (!instrument) {
		return 0 ;
	}
	return InstrumentTypes::FindHelp(InstrumentTypes::Get(instrument->GetType()),
	                                 command) ;
} ;

std::string CommandContext::GetStatus(FourCC command,
                                      InstrumentBank *bank) const {
	I_Instrument *instrument=GetInstrument(bank) ;
	// The empty stand-in has no registry row
	const InstrumentTypeInfo *info=
		instrument?InstrumentTypes::Get(instrument->GetType()):0 ;
	char line[CC_STATUS_LENGTH+1] ;
	switch (Check(command,bank)) {
		case CA_WRONG_LOCATION:
			return (location_==CL_PHRASE)?"tables only":"phrases only" ;
		case CA_NOT_FOR_TYPE:
			if (info) {
				snprintf(line,sizeof(line),"not for I%2.2X %s",slot_,info->uiName) ;
			} else {
				snprintf(line,sizeof(line),"I%2.2X is empty",slot_) ;
			}
			return line ;
		case CA_AVAILABLE:
			break ;
	}
	if (!instrument) {
		return "instrument unknown" ;
	}
	if (info) {
		snprintf(line,sizeof(line),"I%2.2X %s",slot_,info->uiName) ;
	} else {
		snprintf(line,sizeof(line),"I%2.2X empty",slot_) ;
	}
	return line ;
} ;

// The instrument number of the last row from 'row' up that has a note and
// an instrument number: a row starts an instrument only with a note
static int scanPhrase(Phrase *phrase,int p,int row) {
	for (int r=row;r>=0;r--) {
		int i=16*p+r ;
		if ((phrase->note_[i]!=0xFF)&&(phrase->instr_[i]!=0xFF)) {
			return phrase->instr_[i] ;
		}
	}
	return CC_UNKNOWN_SLOT ;
}

int CommandContext::ResolvePhraseRow(Song *song,int songColumn,int songRow,
                                     int chain,int chainRow,int phrase,
                                     int row) {
	if ((phrase<0)||(phrase>=PHRASE_COUNT)||(row<0)||(row>15)) {
		return CC_UNKNOWN_SLOT ;
	}
	int slot=scanPhrase(song->phrase_,phrase,row) ;
	if (slot!=CC_UNKNOWN_SLOT) {
		return slot ;
	}

	// Earlier phrases of the chain. A chain plays from its first row to its
	// first empty one, so after an empty row nothing above comes first
	if ((chain<0)||(chain>=CHAIN_COUNT)||(chainRow<0)||(chainRow>15)) {
		return CC_UNKNOWN_SLOT ;
	}
	unsigned char *chainData=song->chain_->data_+16*chain ;
	if (chainData[chainRow]!=phrase) {
		return CC_UNKNOWN_SLOT ;
	}
	for (int r=chainRow-1;r>=0;r--) {
		if (chainData[r]==0xFF) {
			return CC_UNKNOWN_SLOT ;
		}
		slot=scanPhrase(song->phrase_,chainData[r],15) ;
		if (slot!=CC_UNKNOWN_SLOT) {
			return slot ;
		}
	}

	// Earlier chains of the song column, back to where the player loops to:
	// the row after an empty one or after a chain whose first row is empty
	// (Player::moveToNextChain)
	if ((songColumn<0)||(songColumn>=SONG_CHANNEL_COUNT)||(songRow<0)||
	    (songRow>=SONG_ROW_COUNT)) {
		return CC_UNKNOWN_SLOT ;
	}
	if (song->data_[SONG_CHANNEL_COUNT*songRow+songColumn]!=chain) {
		return CC_UNKNOWN_SLOT ;
	}
	for (int s=songRow-1;s>=0;s--) {
		unsigned char c=song->data_[SONG_CHANNEL_COUNT*s+songColumn] ;
		if (c==0xFF) {
			return CC_UNKNOWN_SLOT ;
		}
		unsigned char *data=song->chain_->data_+16*c ;
		int played=0 ;
		while ((played<16)&&(data[played]!=0xFF)) {
			played++ ;
		}
		if (played==0) {
			return CC_UNKNOWN_SLOT ;
		}
		for (int r=played-1;r>=0;r--) {
			slot=scanPhrase(song->phrase_,data[r],15) ;
			if (slot!=CC_UNKNOWN_SLOT) {
				return slot ;
			}
		}
	}
	return CC_UNKNOWN_SLOT ;
} ;

CommandContext CommandContext::ForPhraseRow(ViewData *viewData,int row) {
	int slot=ResolvePhraseRow(viewData->song_,viewData->songX_,
	                          viewData->songOffset_+viewData->songY_,
	                          viewData->currentChain_,viewData->chainRow_,
	                          viewData->currentPhrase_,row) ;
	return CommandContext(CL_PHRASE,slot) ;
} ;

CommandContext CommandContext::ForTable(ViewData *viewData,
                                        bool underInstrument) {
	if (underInstrument) {
		return CommandContext(CL_TABLE,viewData->currentInstrument_) ;
	}
	if ((viewData->phraseTable_!=-1)&&
	    (viewData->phraseTable_==viewData->currentTable_)) {
		return CommandContext(CL_TABLE,viewData->phraseTableSlot_) ;
	}
	return CommandContext(CL_TABLE) ;
} ;
