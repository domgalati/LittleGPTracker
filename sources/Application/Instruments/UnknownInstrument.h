#ifndef _UNKNOWN_INSTRUMENT_H_
#define _UNKNOWN_INSTRUMENT_H_

#include "EmptyInstrument.h"
#include "Externals/TinyXML/tinyxml.h"
#include <stdio.h>

// A slot whose saved entry has a TYPE this build doesn't know (one a newer
// build added, say), or no TYPE outside the legacy slot ranges. It plays
// like an empty slot, but the slot stays filled: A,A, clones and imports
// don't take it, and saving writes the entry back as it was loaded (with
// the slot's ID). Clearing it on the instrument screen deletes the entry.

#define UNKNOWN_TYPE_NAME_LENGTH 8

class UnknownInstrument:public EmptyInstrument {

public:
	UnknownInstrument(const TiXmlElement &entry) {
		entry_=new TiXmlElement(entry) ;
		// What the screens show: the saved TYPE, cut to fit the clear
		// confirmation, printable characters only
		const char *type=entry.Attribute("TYPE") ;
		if (!type) {
			type="?" ;
		}
		int i=0 ;
		for (;(type[i])&&(i<UNKNOWN_TYPE_NAME_LENGTH);i++) {
			char c=type[i] ;
			typeName_[i]=((c>=' ')&&(c<='~'))?c:'?' ;
		}
		typeName_[i]=0 ;
		sprintf(name_,"%s (unknown)",typeName_) ;
	} ;

	virtual ~UnknownInstrument() {
		delete entry_ ;
	} ;

	// Not empty: the bank saves it
	virtual bool IsEmpty() { return false ; } ;

	virtual const char *GetName() { return name_ ; } ;

	// The saved TYPE, at most UNKNOWN_TYPE_NAME_LENGTH characters; "?" for
	// an entry without one
	const char *GetTypeName() { return typeName_ ; } ;

	// The entry as loaded
	const TiXmlElement &GetEntry() { return *entry_ ; } ;

private:
	TiXmlElement *entry_ ;
	char typeName_[UNKNOWN_TYPE_NAME_LENGTH+1] ;
	char name_[UNKNOWN_TYPE_NAME_LENGTH+11] ;
} ;

#endif
