#include "InstrumentView.h"
#include "Application/Instruments/InstrumentTypes.h"
#include "Application/Instruments/MidiInstrument.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Model/Config.h"
#include "BaseClasses/UIBigHexVarField.h"
#include "BaseClasses/UIIntVarOffField.h"
#include "BaseClasses/UINoteVarField.h"
#include "BaseClasses/UIStaticField.h"
#include "Foundation/Variables/Variable.h"
#include "ModalDialogs/ImportSampleDialog.h"
#include "ModalDialogs/MessageBox.h"
#include "System/System/System.h"

void ImportSampleDialogCallback(View &v, ModalView &dialog) {
    ((InstrumentView &)v).OnFocus();
}

static void ClearSlotCallback(View &v,ModalView &dialog) {
	((InstrumentView &)v).OnClearSlot(dialog.GetReturnCode()==MBL_YES) ;
}

InstrumentView::InstrumentView(GUIWindow &w,ViewData *data):
	FieldView(w,data),
	typeVar_("type",IVF_TYPE,typeNames_,InstrumentTypes::Count()+2,0) {

	project_=data->project_ ;
	lastFocusID_=0 ;
	current_=0 ;
	picked_=0 ;
	pickSlot_=-1 ;
	clearSlot_=-1 ;
	typeNames_[0]="--" ;
	for (int k=0;k<InstrumentTypes::Count();k++) {
		typeNames_[k+1]=InstrumentTypes::At(k)->uiName ;
	}
	unknownType_[0]=0 ;
	typeNames_[InstrumentTypes::Count()+1]=unknownType_ ;
	onInstrumentChange() ;
}

InstrumentView::~InstrumentView() {
}

InstrumentType InstrumentView::getInstrumentType() {
	int i=viewData_->currentInstrument_ ;
	InstrumentBank *bank=viewData_->project_->GetInstrumentBank() ;
	I_Instrument *instrument=bank->GetInstrument(i) ;
    return instrument->GetType() ;
} ;

void InstrumentView::onInstrumentChange() {

	ClearFocus() ;

	I_Instrument *old=current_ ;

	int i=viewData_->currentInstrument_ ;
	InstrumentBank *bank=viewData_->project_->GetInstrumentBank() ;
	current_=bank->GetInstrument(i) ;

	if ((old)&&(current_!=old)) {
		old->RemoveObserver(*this) ;
	} ;
	T_SimpleList<UIField>::Empty() ;

	InstrumentType it=getInstrumentType() ;
	const InstrumentTypeInfo *info=InstrumentTypes::Get(it) ;

	// A type with a layout in the registry needs no code here
	if ((info)&&(info->fields)) {
		fillFields(info->fields) ;
	} else switch (it) {
		case IT_MIDI:
			fillMidiParameters() ;
			break ;
		case IT_SAMPLE:
			fillSampleParameters() ;
			break ;
		default:
			break ;
	} ;
	// After the parameters, so the first parameter keeps the default focus.
	// None for a number outside the bank (FF from the phrase screen)
	if (i<MAX_INSTRUMENT_COUNT) {
		fillTypeField() ;
	}

	SetFocus(T_SimpleList<UIField>::GetFirst()) ;
	IteratorPtr<UIField> it2(T_SimpleList<UIField>::GetIterator()) ;
	for (it2->Begin();!it2->IsDone();it2->Next()) {
		// Labels are UIStaticFields, not UIIntVarFields, and never focused
		if (it2->CurrentItem().IsStatic()) {
			continue ;
		}
        UIIntVarField &field=(UIIntVarField &)it2->CurrentItem() ;
        if (field.GetVariableID()==lastFocusID_) {
            SetFocus(&field) ;
            break ;
        }
    } ;
	if (current_!=old) {
		current_->AddObserver(*this) ;
	}
} ;

void InstrumentView::fillSampleParameters() {

	int i=viewData_->currentInstrument_ ;
	InstrumentBank *bank=viewData_->project_->GetInstrumentBank() ;
	I_Instrument *instr=bank->GetInstrument(i) ;
	SampleInstrument *instrument=(SampleInstrument *)instr  ;
	GUIPoint position=GetAnchor() ;

//	position._y+=View::fieldSpaceHeight_;
	Variable *v=instrument->FindVariable(SIP_SAMPLE) ;
	SamplePool *sp=SamplePool::GetInstance() ;
	UIIntVarField *f1=new UIIntVarField(position,*v,"sample: %s",0,sp->GetNameListSize()-1,1,0x10) ;
	T_SimpleList<UIField>::Insert(f1) ;
	f1->SetFocus() ;

    position._y += 1;
#ifdef FFMPEG_ENABLED
    v = instrument->FindVariable(SIP_PRINTFX);
    f1 = new UIIntVarField(position, *v, "%s", 0, 3, 1, 2);
    T_SimpleList<UIField>::Insert(f1) ;

    position._x += 7;
    v = instrument->FindVariable(SIP_IR_WET);
    f1 = new UIIntVarField(position, *v, "wet:%d%%", 0, 100, 1, 10);
    T_SimpleList<UIField>::Insert(f1);
    position._x += 9;

    v = instrument->FindVariable(SIP_IR_PAD);
    f1 = new UIIntVarField(position, *v, "pad:%dms", 0, 5000, 5, 100);
    T_SimpleList<UIField>::Insert(f1);
    position._x -= 16;
#endif
    position._y += 2;
    v=instrument->FindVariable(SIP_VOLUME) ;
	f1=new UIIntVarField(position,*v,"volume: %d [%2.2X]",0,255,1,10) ;
	T_SimpleList<UIField>::Insert(f1) ;

    position._y+=1 ;
	v=instrument->FindVariable(SIP_PAN) ;
	f1=new UIIntVarField(position,*v,"pan: %2.2X",0,0xFE,1,0x10) ;
	T_SimpleList<UIField>::Insert(f1) ;

	position._y+=1 ;
	v=instrument->FindVariable(SIP_ROOTNOTE) ;
	f1=new UINoteVarField(position,*v,"root note: %s",0,0x7F,1,0x0C) ;
	T_SimpleList<UIField>::Insert(f1) ;

	position._y+=1 ;
	v=instrument->FindVariable(SIP_FINETUNE) ;
	f1=new UIIntVarField(position,*v,"detune: %2.2X",0,255,1,0x10) ;
	T_SimpleList<UIField>::Insert(f1) ;

    position._y += 2;
    v=instrument->FindVariable(SIP_CRUSH);
	f1=new UIIntVarField(position,*v,"crush: %d",1,0x10,1,4) ;
	T_SimpleList<UIField>::Insert(f1) ;

    position._x += 10;
    v = instrument->FindVariable(SIP_CRUSHVOL);
    f1=new UIIntVarField(position,*v,"drive: %2.2X",0,0xFF,1,0x10) ;
	T_SimpleList<UIField>::Insert(f1) ;
    position._x -= 10;

    position._y += 1;
	v=instrument->FindVariable(SIP_DOWNSMPL) ;
	f1=new UIIntVarField(position,*v,"downsample: %d",0,8,1,4) ;
	T_SimpleList<UIField>::Insert(f1) ;


	position._y+=2 ;
	UIStaticField *sf=new UIStaticField(position,"flt cut/res:") ;
	T_SimpleList<UIField>::Insert(sf) ;

	position._x+=13 ;
	v=instrument->FindVariable(SIP_FILTCUTOFF) ;
	f1=new UIIntVarField(position,*v,"%2.2X",0,0xFF,1,0x10) ;
	T_SimpleList<UIField>::Insert(f1) ;

	position._x+=3 ;
	v=instrument->FindVariable(SIP_FILTRESO) ;
	f1=new UIIntVarField(position,*v,"%2.2X",0,0xFF,1,0x10) ;
	T_SimpleList<UIField>::Insert(f1) ;
	position._x-=16 ;

	position._y+=1 ;
	v=instrument->FindVariable(SIP_FILTMIX) ;
	f1=new UIIntVarField(position,*v,"type: %2.2X",0,0xFF,1,0x10) ;
	T_SimpleList<UIField>::Insert(f1) ;

	position._y+=1 ;
	v=instrument->FindVariable(SIP_FILTMODE) ;
	f1=new UIIntVarField(position,*v,"Mode: %s",0,2,1,1) ;
	T_SimpleList<UIField>::Insert(f1) ;

	position._y+=1 ;
	v=instrument->FindVariable(SIP_ATTENUATE) ;
	f1=new UIIntVarField(position,*v,"attenuate: %d [%2.2X]",1,0xFF,1,0x10) ;
	T_SimpleList<UIField>::Insert(f1) ;

	position._y+=1 ;
	sf=new UIStaticField(position,"fb tune/mix: ") ;
	T_SimpleList<UIField>::Insert(sf) ;

	v=instrument->FindVariable(SIP_FBTUNE) ;
	position._x+=13 ;
	f1=new UIIntVarField(position,*v,"%2.2X",0,0xFF,1,0x10) ;
	T_SimpleList<UIField>::Insert(f1) ;

	position._x+=3 ;
	v=instrument->FindVariable(SIP_FBMIX) ;
	f1=new UIIntVarField(position,*v,"%2.2X",0,0xFF,1,0X10) ;
	T_SimpleList<UIField>::Insert(f1) ;

	position._x-=16 ;

	position._y+=2;
	v=instrument->FindVariable(SIP_INTERPOLATION) ;
	f1=new UIIntVarField(position,*v,"interpolation: %s",0,1,1,1) ;
	T_SimpleList<UIField>::Insert(f1) ;

    position._y+=1 ;
	v=instrument->FindVariable(SIP_LOOPMODE) ;
	f1=new UIIntVarField(position,*v,"loop mode: %s",0,SILM_LAST-1,1,1) ;
	T_SimpleList<UIField>::Insert(f1) ;
	position._y+=1 ;

	v=instrument->FindVariable(SIP_SLICES) ;
	f1=new UIIntVarField(position,*v,"slices: %2.2X",1,0xFF,1,0x10) ;
	T_SimpleList<UIField>::Insert(f1) ;

	position._y+=1 ;
	v=instrument->FindVariable(SIP_START) ;
	f1=new UIBigHexVarField(position,*v,7,"start: %7.7X",0,instrument->GetSampleSize()-1,16) ;
	T_SimpleList<UIField>::Insert(f1) ;

	position._y+=1 ;
	v=instrument->FindVariable(SIP_LOOPSTART) ;
	f1=new UIBigHexVarField(position,*v,7,"loop start: %7.7X",0,instrument->GetSampleSize()-1,16) ;
	T_SimpleList<UIField>::Insert(f1) ;

	position._y+=1 ;
	v=instrument->FindVariable(SIP_END) ;
	f1=new UIBigHexVarField(position,*v,7,"loop end: %7.7X",0,instrument->GetSampleSize()-1,16) ;
	T_SimpleList<UIField>::Insert(f1) ;

	v=instrument->FindVariable(SIP_TABLEAUTO) ;
	position._y+=2 ;
	UIIntVarField *f2=new UIIntVarField(position,*v,"automation: %s",0,1,1,1) ;
	T_SimpleList<UIField>::Insert(f2) ;

	position._y+=1 ;
	v=instrument->FindVariable(SIP_TABLE) ;
	f1=new UIIntVarOffField(position,*v,"table: %2.2X",0x00,0x7F,1,0x10) ;
	T_SimpleList<UIField>::Insert(f1) ;

} ;

void InstrumentView::fillMidiParameters() {

	int i=viewData_->currentInstrument_ ;
	InstrumentBank *bank=viewData_->project_->GetInstrumentBank() ;
	I_Instrument *instr=bank->GetInstrument(i) ;
	MidiInstrument *instrument=(MidiInstrument *)instr  ;
	GUIPoint position=GetAnchor() ;

	Variable *v=instrument->FindVariable(MIP_CHANNEL) ;
	UIIntVarField* f1=new UIIntVarField(position,*v,"channel: %2.2d",0,0x0F,1,0x04,1) ;
	T_SimpleList<UIField>::Insert(f1) ;
	f1->SetFocus() ;

	position._y+=1;
	v=instrument->FindVariable(MIP_VOLUME) ;
	f1=new UIIntVarField(position,*v,"volume: %2.2X",0,0xFF,1,0x10) ;
	T_SimpleList<UIField>::Insert(f1) ;

	position._y+=1;
	v=instrument->FindVariable(MIP_NOTELENGTH) ;
	f1=new UIIntVarField(position,*v,"length: %2.2X",0,0xFF,1,0x10) ;
	T_SimpleList<UIField>::Insert(f1) ;

	v=instrument->FindVariable(MIP_TABLEAUTO) ;
	position._y+=2 ;
	UIIntVarField *f2=new UIIntVarField(position,*v,"automation: %s",0,1,1,1) ;
	T_SimpleList<UIField>::Insert(f2) ;

	position._y+=1;
	v=instrument->FindVariable(MIP_TABLE) ;
	f1=new UIIntVarOffField(position,*v,"table: %2.2X",0,0x7F,1,0x10) ;
	T_SimpleList<UIField>::Insert(f1) ;

} ;

// The layout a type gives in the registry (InstrumentTypeInfo::fields)
void InstrumentView::fillFields(const InstrumentField *fields) {

	int i=viewData_->currentInstrument_ ;
	InstrumentBank *bank=viewData_->project_->GetInstrumentBank() ;
	I_Instrument *instr=bank->GetInstrument(i) ;
	GUIPoint anchor=GetAnchor() ;

	for (const InstrumentField *f=fields;f->variable;f++) {
		// A row naming a variable the type doesn't have is skipped (slot_lab's
		// fields scenario checks every layout)
		Variable *v=instr->FindVariable(f->variable) ;
		if (!v) {
			continue ;
		}
		GUIPoint position=anchor ;
		position._x+=f->x ;
		position._y+=f->y ;
		int max=(f->max<0)?v->GetListSize()-1:f->max ;
		UIIntVarField *field=new UIIntVarField(position,*v,f->format,f->min,max,f->fine,f->coarse) ;
		T_SimpleList<UIField>::Insert(field) ;
	}
} ;

// On the row above the parameters. The slot's type is shown, not edited:
// ProcessButtonMask turns A+arrows on it into stepType
void InstrumentView::fillTypeField() {

	int i=viewData_->currentInstrument_ ;
	InstrumentBank *bank=viewData_->project_->GetInstrumentBank() ;
	I_Instrument *instr=bank->GetInstrument(i) ;
	InstrumentType type=instr->GetType() ;
	int index=0 ;
	for (int k=0;k<InstrumentTypes::Count();k++) {
		if (InstrumentTypes::At(k)->type==type) {
			index=k+1 ;
		}
	}
	// A filled slot without a registry type keeps an entry this build
	// doesn't know: its name ("Granular (unknown)") is the list's last entry
	if ((index==0)&&(!bank->IsSlotEmpty(i))) {
		snprintf(unknownType_,sizeof(unknownType_),"%s",instr->GetName()) ;
		index=InstrumentTypes::Count()+1 ;
	}
	typeVar_.SetInt(index,false) ;

	GUIPoint position=GetAnchor() ;
	position._y-=1 ;
	UIIntVarField *f=new UIIntVarField(position,typeVar_,"type: %s",0,InstrumentTypes::Count(),1,1) ;
	T_SimpleList<UIField>::Insert(f) ;
} ;

// A+arrows on the type field. An empty slot gets the next or previous type
// in the list, created at once. So does the slot the type field filled
// during this A hold, which lets A+RIGHT step through the types; "--"
// empties it again. Any other instrument has to be cleared first
void InstrumentView::stepType(int offset) {

	int i=viewData_->currentInstrument_ ;
	InstrumentBank *bank=viewData_->project_->GetInstrumentBank() ;
	if (i>=MAX_INSTRUMENT_COUNT) {
		return ;
	}
	if ((!bank->IsSlotEmpty(i))&&((i!=pickSlot_)||(bank->GetInstrument(i)!=picked_))) {
		View::SetNotification("B,A to clear first") ;
		return ;
	}
	int index=typeVar_.GetInt()+offset ;
	if (index<0) {
		index=0 ;
	}
	if (index>InstrumentTypes::Count()) {
		index=InstrumentTypes::Count() ;
	}
	if (index==typeVar_.GetInt()) {
		return ;
	}
	// The bank tells this screen (IET_DELETED) before it deletes the
	// instrument shown; onInstrumentChange builds the screen again
	if (index==0) {
		bank->Empty(i) ;
		picked_=0 ;
	} else {
		picked_=bank->Create(i,InstrumentTypes::At(index-1)->type) ;
		pickSlot_=i ;
	}
	onInstrumentChange() ;
	isDirty_=true ;
} ;

// B,A on the type field: Yes/No before the slot is emptied, since its
// settings go with it
void InstrumentView::askClearSlot() {

	int i=viewData_->currentInstrument_ ;
	InstrumentBank *bank=viewData_->project_->GetInstrumentBank() ;
	if (bank->IsSlotEmpty(i)) {
		return ;
	}
	int phrases=bank->ReferenceCount(i,viewData_->song_->phrase_->instr_) ;

	// MessageBox shows one line of up to 36 characters
	char used[20] ;
	if (phrases==0) {
		strcpy(used,"Unused") ;
	} else {
		sprintf(used,"In %d phrase%s",phrases,(phrases==1)?"":"s") ;
	}
	char message[40] ;
	sprintf(message,"Clear I%2.2X (%s)? %s",i,bank->GetTypeName(i),used) ;

	clearSlot_=i ;
	MessageBox *mb=new MessageBox(*this,message,MBBF_YES|MBBF_NO) ;
	DoModal(mb,ClearSlotCallback) ;
} ;

void InstrumentView::OnClearSlot(bool confirmed) {
	if ((confirmed)&&(clearSlot_>=0)) {
		// Through InstrumentBank::replace, which lets the player and this
		// screen forget the instrument first
		viewData_->project_->GetInstrumentBank()->Empty(clearSlot_) ;
	}
	clearSlot_=-1 ;
	onInstrumentChange() ;
	isDirty_=true ;
} ;

FourCC InstrumentView::getFocusID() {
	// Never a static field: navigation skips them, and every layout starts
	// with a variable field
	UIIntVarField *field=(UIIntVarField *)GetFocus() ;
	return field?field->GetVariableID():0 ;
} ;

void InstrumentView::warpToNext(int offset) {
	int instrument=viewData_->currentInstrument_+offset ;
	if (instrument>=MAX_INSTRUMENT_COUNT) {
		instrument=instrument-MAX_INSTRUMENT_COUNT ;
	} ;
	if (instrument<0) {
		instrument=MAX_INSTRUMENT_COUNT+instrument ;
	} ;
	viewData_->currentInstrument_=instrument ;
	onInstrumentChange() ;
	isDirty_=true ;
} ;

void InstrumentView::ProcessButtonMask(unsigned short mask,bool pressed) {

	// A pick on the type field lasts while A is held (releases come here too)
	if (!(mask&EPBM_A)) {
		picked_=0 ;
	}

	if (!pressed) return ;

	isDirty_=false ;

	if (viewMode_==VM_NEW) {
		if (mask==EPBM_A) {
			UIIntVarField *field=(UIIntVarField *)GetFocus() ;
			Variable &v=field->GetVariable() ;
			switch(v.GetID()) {
				case SIP_SAMPLE:
				 {
                    // First check if the samplelib exists

					 Path sampleLib(SamplePool::GetInstance()->GetSampleLib()) ;
					 if (FileSystem::GetInstance()->GetFileType(sampleLib.GetPath().c_str())!=FT_DIR) {
						 MessageBox *mb=new MessageBox(*this,"Can't access the samplelib",MBBF_OK) ;
						 DoModal(mb) ;
					 } else { ;
						// Go to import sample

						 ImportSampleDialog *isd=new ImportSampleDialog(*this) ;
                         DoModal(isd, ImportSampleDialogCallback);
                     }
                    break ;
				 }
				case SIP_TABLE:
				 {
					int next=TableHolder::GetInstance()->GetNext() ;
					if (next!=NO_MORE_TABLE) {
						v.SetInt(next) ;
						isDirty_=true ;
					}
					break ;
                }
                case SIP_PRINTFX: {
                    FxPrinter printer(viewData_);
                    isDirty_ = printer.Run();
                    View::SetNotification(printer.GetNotification());
                    break;
                }
                default:
                    break ;
			}
			mask&=(0xFFFF-EPBM_A) ;
		}
	}

	if (viewMode_==VM_CLONE) {
        if ((mask&EPBM_A)&&(mask&EPBM_L)) {
			UIIntVarField *field=(UIIntVarField *)GetFocus() ;
			mask&=(0xFFFF-EPBM_A) ;
			// Only the table field holds a table number to clone
			if (getFocusID()!=SIP_TABLE) return ;
			Variable &v=field->GetVariable() ;
			int current=v.GetInt() ;
			if (current==-1) return ;

			int next=TableHolder::GetInstance()->Clone(current) ;
			if (next!=NO_MORE_TABLE) {
				v.SetInt(next) ;
				isDirty_=true ;
			}
		}
		mask&=(0xFFFF-(EPBM_A|EPBM_L)) ;
	} ;

	if (viewMode_==VM_SELECTION) {
	} else {
		viewMode_=VM_NORMAL ;
	}

	// A+arrows on the type field change the slot's type, not the field
	if ((mask&EPBM_A)&&(getFocusID()==IVF_TYPE)) {
		if (mask&(EPBM_RIGHT|EPBM_UP)) {
			stepType(+1) ;
		} else if (mask&(EPBM_LEFT|EPBM_DOWN)) {
			stepType(-1) ;
		}
		mask&=(0xFFFF-(EPBM_LEFT|EPBM_RIGHT|EPBM_UP|EPBM_DOWN)) ;
	}

	FieldView::ProcessButtonMask(mask) ;

    Player *player=Player::GetInstance() ;
	// B Modifier

    if (mask & EPBM_B) {
        if (mask&EPBM_LEFT) warpToNext(-1) ;
		if (mask&EPBM_RIGHT) warpToNext(+1);
		if (mask&EPBM_DOWN) warpToNext(-16) ;
		if (mask&EPBM_UP) warpToNext(+16);
		if (mask&EPBM_A) { // Allow cut instrument
			int i=viewData_->currentInstrument_ ;
			InstrumentBank *bank=viewData_->project_->GetInstrumentBank() ;
			I_Instrument *instr=bank->GetInstrument(i) ;
			switch (getFocusID()) {
				case IVF_TYPE: // Clear the slot, after a Yes
					askClearSlot() ;
					break ;
				case SIP_SAMPLE: // Only Sample has this field
					instr->Purge() ;
//					Variable *v=instr->FindVariable(SIP_SAMPLE) ;
//					v->SetInt(-1) ;
					isDirty_=true ;
					break ;
				case SIP_TABLE: // MIP_TABLE is the same FourCC
				{
					Variable *v=instr->FindVariable(SIP_TABLE) ;
					if (v) {
						v->SetInt(-1) ;
						isDirty_=true ;
					}
					break ;
				}
				default:
					break ;
			}
        }
        if (mask&EPBM_L) {
            viewMode_=VM_CLONE ;
        } ;
    } else {

        // A modifier

        if (mask == EPBM_A) {
            UIIntVarField *field = (UIIntVarField *)GetFocus();
            FourCC varID = field ? field->GetVariableID() : 0;
            if ((varID == SIP_TABLE) || (varID == MIP_TABLE) ||
                (varID == SIP_SAMPLE) || (varID == SIP_PRINTFX)) {
                viewMode_ = VM_NEW;
			}
        } else {

            // R Modifier

            if (mask & EPBM_R) {
                if (mask & EPBM_LEFT) {
                    ViewType vt = VT_PHRASE;
                    ViewEvent ve(VET_SWITCH_VIEW, &vt);
                    SetChanged();
                    NotifyObservers(&ve);
                }

                if (mask & EPBM_DOWN) {

                    // Go to table view

                    ViewType vt = VT_TABLE2;

                    int i = viewData_->currentInstrument_;
                    InstrumentBank *bank =
                        viewData_->project_->GetInstrumentBank();
                    I_Instrument *instr = bank->GetInstrument(i);
                    int table = instr->GetTable();
                    if (table != VAR_OFF) {
                        viewData_->currentTable_ = table;
                    }
                    ViewEvent ve(VET_SWITCH_VIEW, &vt);
                    SetChanged();
                    NotifyObservers(&ve);
                }

                // if (mask&EPBM_RIGHT) {

                //	// Go to import sample

                //		ViewType vt=VT_IMPORT ;
                //		ViewEvent ve(VET_SWITCH_VIEW,&vt) ;
                //		SetChanged();
                //		NotifyObservers(&ve) ;
                //}

                if (mask & EPBM_START) {
                    player->OnStartButton(PM_PHRASE, viewData_->songX_, true,
                                          viewData_->chainRow_);
                }
            } else {
                // No modifier
                if (mask & EPBM_START) {
                    player->OnStartButton(PM_PHRASE, viewData_->songX_, false,
                                          viewData_->chainRow_);
                }
            }
        }
    }

    UIIntVarField *field = (UIIntVarField *)GetFocus();
    if (field) {
	   lastFocusID_=field->GetVariableID() ;
    }

} ;

void InstrumentView::DrawView() {

	Clear() ;
    View::EnableNotification();

    GUITextProperties props;
    GUIPoint pos = GetTitlePosition();

    // Draw title

    char title[20];
    SetColor(CD_NORMAL);
    sprintf(title, "Instrument %2.2X", viewData_->currentInstrument_);
    DrawString(pos._x, pos._y, title, props);

    // Draw fields

    FieldView::Redraw();
    drawMap() ;
} ;

void InstrumentView::OnFocus() { onInstrumentChange(); }

// Called by the instrument shown, while it walks its observer list, so this
// must not add or remove observers on it
void InstrumentView::Update(Observable &o,I_ObservableData *d) {
	if ((d)&&(((InstrumentEvent *)d)->type_==IET_DELETED)) {
		// The bank is about to delete it: forget it, and the fields that
		// point at its variables. OnFocus builds the screen again
		if (&o==current_) {
			current_=0 ;
			ClearFocus() ;
			T_SimpleList<UIField>::Empty() ;
		}
		return ;
	}
	// If the current slot has changed meanwhile, the screen isn't showing
	// and OnFocus will switch instruments
	InstrumentBank *bank=viewData_->project_->GetInstrumentBank() ;
	if (bank->GetInstrument(viewData_->currentInstrument_)==current_) {
		onInstrumentChange() ;
	}
}
