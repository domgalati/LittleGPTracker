#ifndef _EMPTY_INSTRUMENT_H_
#define _EMPTY_INSTRUMENT_H_

#include "I_Instrument.h"
#include "Foundation/Variables/Variable.h"

// What InstrumentBank::GetInstrument returns for a slot number outside the
// bank, so callers always get an instrument: it plays nothing, has no
// parameters and no table, and ignores commands. Its type is IT_LAST, which
// is not a real type. Starting it on a channel stops the previous note and
// table, like a sample instrument without a sample.

class EmptyInstrument:public I_Instrument {

public:
	EmptyInstrument() {} ;
	virtual ~EmptyInstrument() {} ;

	virtual bool Init() { return true ; } ;

	virtual bool Start(int channel,unsigned char note,int flags=1) { return false ; } ;
	virtual void Stop(int channel) {} ;

	virtual void OnStart() {} ;

	virtual bool Render(int channel,fixed *buffer,int size,int flags) { return false ; } ;

	virtual bool IsInitialized() { return true ; } ;

	virtual bool IsEmpty() { return true ; } ;

	virtual InstrumentType GetType() { return IT_LAST ; } ;

	virtual const char *GetName() { return "--" ; } ;

	virtual void ProcessCommand(int channel,FourCC cc,ushort value) {} ;

	virtual void Purge() {} ;

	virtual int GetTable() { return VAR_OFF ; } ;
	virtual bool GetTableAutomation() { return false ; } ;

	virtual void GetTableState(TableSaveState &state) { state.Reset() ; } ;
	virtual void SetTableState(TableSaveState &state) {} ;
} ;

#endif
