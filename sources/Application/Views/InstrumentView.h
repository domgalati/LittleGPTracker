#ifndef _INSTRUMENT_VIEW_H_
#define _INSTRUMENT_VIEW_H_

#include "Application/FX/FxPrinter.h"
#include "Application/Instruments/InstrumentTypes.h"
#include "BaseClasses/FieldView.h"
#include "Foundation/Observable.h"
#include "Foundation/Variables/Variable.h"
#include "ViewData.h"

// ID of the type field's variable: no instrument variable uses this FourCC,
// so lastFocusID_ can tell the field apart
#define IVF_TYPE MAKE_FOURCC('T','Y','P','E')

class InstrumentView: public FieldView, public I_Observer {
public:
	InstrumentView(GUIWindow &w,ViewData *data) ;
	virtual ~InstrumentView() ;

	virtual void ProcessButtonMask(unsigned short mask,bool pressed) ;
	virtual void DrawView() ;
	virtual void OnPlayerUpdate(PlayerEventType,unsigned int) {} ;
	virtual void OnFocus() ;
	// The answer to the clear confirmation (B,A on the type field): Yes
	// empties the slot it was asked for
	void OnClearSlot(bool confirmed) ;

protected:
	void warpToNext(int offset) ;
	void onInstrumentChange() ;
	void fillSampleParameters() ;
	void fillMidiParameters() ;
	void fillFields(const InstrumentField *fields) ;
	void fillTypeField() ;
	void stepType(int offset) ;
	void askClearSlot() ;
	InstrumentType getInstrumentType() ;
	FourCC getFocusID() ;
	void Update(Observable &o,I_ObservableData *d) ;

private:
	Project *project_ ;
	FourCC lastFocusID_ ;
	I_Instrument *current_ ;
	// The type field's list: "--" (empty), then the registry's types in
	// table order
	const char *typeNames_[IT_LAST+1] ;
	Variable typeVar_ ;
	// What the type field created in pickSlot_ during the current A hold.
	// While A stays down, further A+arrows may replace it: nothing has been
	// set on it yet
	I_Instrument *picked_ ;
	int pickSlot_ ;
	// The slot the clear confirmation is asking about
	int clearSlot_ ;
} ;
#endif
