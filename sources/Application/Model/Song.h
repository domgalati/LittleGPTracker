#ifndef _SONG_H_
#define _SONG_H_

#include "Chain.h"
#include "Phrase.h"
#include "Application/Persistency/Persistent.h"

#define SONG_CHANNEL_COUNT 8
#define SONG_ROW_COUNT 256

// Instrument slots 00-FE, each empty or holding any type. FF in a phrase
// means no instrument
#define MAX_INSTRUMENT_COUNT 0xFF

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
