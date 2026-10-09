#ifndef _SONG_H_
#define _SONG_H_

#include "Chain.h"
#include "Phrase.h"
#include "Application/Persistency/Persistent.h"

#define SONG_CHANNEL_COUNT 8
#define SONG_ROW_COUNT 256

#define MAX_SAMPLEINSTRUMENT_COUNT 0x80
#define MAX_MIDIINSTRUMENT_COUNT 0x10
#define MAX_PIGTAILINSTRUMENT_COUNT 0x10

// Slot layout: sample 0x00-0x7F, MIDI 0x80-0x8F, pigtail 0x90-0x9F
#define PIGTAIL_INSTRUMENT_BASE (MAX_SAMPLEINSTRUMENT_COUNT+MAX_MIDIINSTRUMENT_COUNT)

// Instrument count before pigtail slots existed (legacy binary save format)
#define LEGACY_INSTRUMENT_COUNT (MAX_SAMPLEINSTRUMENT_COUNT+MAX_MIDIINSTRUMENT_COUNT)

#define MAX_INSTRUMENT_COUNT (PIGTAIL_INSTRUMENT_BASE+MAX_PIGTAILINSTRUMENT_COUNT)

class Song:Persistent {
public:
	Song() ;
	~Song() ;

	virtual void SaveContent(TiXmlNode *node) ;
	virtual void RestoreContent(TiXmlElement *element);

	unsigned char *data_ ;
	Chain *chain_ ;
	Phrase *phrase_ ;
} ;

#endif
