
#include "InstrumentBank.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Instruments/InstrumentTypes.h"
#include "System/io/Status.h"
#include "System/Console/Trace.h"
#include "Application/Utils/char.h"
#include "Application/Model/Config.h"
#include "Application/Persistency/PersistencyService.h"
#include "Application/Mixer/MixerService.h"
#include "Application/Player/Player.h"
#include "Filters.h"

// Which type fills which slots. Every slot is filled.
// TODO(instrument-slots stage 3): slots start empty and this goes.

struct SlotRange {
	InstrumentType type ;
	int first,count ;
} ;

static const SlotRange defaultLayout[]= {
	{IT_SAMPLE,0,MAX_SAMPLEINSTRUMENT_COUNT},
	{IT_MIDI,MAX_SAMPLEINSTRUMENT_COUNT,MAX_MIDIINSTRUMENT_COUNT},
	{IT_PIGTAIL,PIGTAIL_INSTRUMENT_BASE,MAX_PIGTAILINSTRUMENT_COUNT}
} ;

// Contain all instrument definition

InstrumentBank::InstrumentBank():Persistent("INSTRUMENTBANK") {

	for (unsigned int r=0;r<sizeof(defaultLayout)/sizeof(defaultLayout[0]);r++) {
		const SlotRange &range=defaultLayout[r] ;
		const InstrumentTypeInfo *info=InstrumentTypes::Get(range.type) ;
		for (int i=range.first;i<range.first+range.count;i++) {
			instrument_[i]=info->create(i) ;
		}
	}
    Status::Set("All instrument loaded") ;
} ;

//
// Assigns default instruments value for new project
//

void InstrumentBank::AssignDefaults() {

	SamplePool *pool=SamplePool::GetInstance() ;
   	for (int i=0;i<MAX_SAMPLEINSTRUMENT_COUNT;i++) {
		SampleInstrument *s=(SampleInstrument*)instrument_[i] ;
		if (i<pool->GetNameListSize()) {
	        s->AssignSample(i) ;
		} else {
			s->AssignSample(-1) ;
		} 
    } ;
} ;

InstrumentBank::~InstrumentBank() {
	for (int i=0;i<MAX_INSTRUMENT_COUNT;i++) {
		delete instrument_[i] ;
	}	
} ;

I_Instrument *InstrumentBank::GetInstrument(int i) {
	if ((i<0)||(i>=MAX_INSTRUMENT_COUNT)||(!instrument_[i])) {
		return &empty_ ;
	}
	return instrument_[i] ;
} ;

void InstrumentBank::SaveContent(TiXmlNode *node) {
	char hex[3] ;
	for (int i=0;i<MAX_INSTRUMENT_COUNT;i++) {

		I_Instrument *instr=instrument_[i] ;
		if (!instr->IsEmpty()) {
			TiXmlElement data("INSTRUMENT") ;
			hex2char(i,hex) ;
			data.SetAttribute("ID",hex) ;
			data.SetAttribute("TYPE",InstrumentTypes::Get(instr->GetType())->saveName) ;

			IteratorPtr<Variable> it(instr->GetIterator()) ;
			int count=0 ;
			for (it->Begin();!it->IsDone();it->Next()) {
				Variable &v=it->CurrentItem() ;
				TiXmlElement param("PARAM") ;
				param.SetAttribute("NAME",v.GetName()) ;
				param.SetAttribute("VALUE",v.GetString()) ;
				data.InsertEndChild(param) ;
				count++ ;
			}
			if (count) node->InsertEndChild(data) ;
		}
	}
} ;

void InstrumentBank::RestoreContent(TiXmlElement *element) {

	TiXmlElement *current=element->FirstChildElement() ;

	PersistencyDocument *doc=(PersistencyDocument *)element->GetDocument() ;
  if (doc->version_ < 130)
  {
    if (Config::GetInstance()->GetValue("LEGACYDOWNSAMPLING") != NULL)
    {
      SampleInstrument::EnableDownsamplingLegacy();
    }
  }
	while (current) {

		// Check it is an instrument
		
		if (!strcmp(current->Value(),"INSTRUMENT")) {

			// Get the instrument ID
			
			const char* hexid=current->Attribute("ID") ;
			unsigned char b1=(c2h__(hexid[0]))<<4 ;
			unsigned char b2=c2h__(hexid[1]) ;
			unsigned char id=b1+b2 ;			

			// Entries older than 2014 have no TYPE: the slot decides
			const char* instype=current->Attribute("TYPE") ;
			const InstrumentTypeInfo *info=(instype)
				?InstrumentTypes::FindBySaveName(instype)
				:InstrumentTypes::FindByLegacySlot(id) ;
			if (id<MAX_INSTRUMENT_COUNT) {
        I_Instrument *instr=instrument_[id] ;
				if (!info) {
					// Unknown type (e.g. saved by a newer build): leave
					// the slot's default instrument in place, skip params
					Trace::Error("Unknown instrument type '%s' for instrument %s, leaving slot empty",instype?instype:"(none)",hexid) ;
					instr=0 ;
				} else if (instr->GetType()!=info->type) {
					instr=info->create(id) ;
					replace(id,instr) ;
				} ;

        TiXmlElement *param=(instr)?current->FirstChildElement():0 ;
				while (param) {
					const char *name=param->Attribute("NAME") ;
					const char *value=param->Attribute("VALUE") ;

          // Convert old filter dist to newer filter mode

          if (!strcmp(name,"filter dist"))
          {
            name = "filter mode";
            if (!strcmp(value,"none"))
            {
              value = "original";
            }
            else
            {
              value = "scream";
            }
          }

					IteratorPtr<Variable> it(instr->GetIterator()) ;
					for (it->Begin();!it->IsDone();it->Next()) {
						Variable &v=it->CurrentItem() ;
						if (!strcmp(v.GetName(),name)) {
							v.SetString(value) ;
						} ;
					}
					param=param->NextSiblingElement() ;
				}
				if ((instr)&&(doc->version_<38)) {
					Variable *cvl=instr->FindVariable(SIP_CRUSHVOL) ;
					Variable *vol=instr->FindVariable(SIP_VOLUME);
					Variable *crs=instr->FindVariable(SIP_CRUSH) ;
					if ((vol)&&(cvl)&&(crs)) {
						if (crs->GetInt()!=16) {
							int temp=vol->GetInt() ;
							vol->SetInt(cvl->GetInt()) ;
							cvl->SetInt(temp) ;
						}
					} ;
				}
			}
		}
		current=current->NextSiblingElement() ;
	} ;
};

void InstrumentBank::Init() {
	for (int i=0;i<MAX_INSTRUMENT_COUNT;i++) {
		instrument_[i]->Init() ;
	}
}

unsigned short InstrumentBank::GetNext() {
	for (int i=0;i<MAX_SAMPLEINSTRUMENT_COUNT;i++) {
		SampleInstrument *si=(SampleInstrument *)instrument_[i] ;
		Variable *sample=si->FindVariable(SIP_SAMPLE) ;
		if (sample) {
			if (sample->GetInt()==-1) {
				return i ;
			}
		}
	}
	return NO_MORE_INSTRUMENT ;
} ;

unsigned short InstrumentBank::Clone(unsigned short i) {
	// can't clone midi instruments

	unsigned short next=GetNext() ;
	if (next==NO_MORE_INSTRUMENT) 
  {
		return NO_MORE_INSTRUMENT ;
	}

	// Nothing to clone outside the bank (the empty stand-in)
	if ((i>=MAX_INSTRUMENT_COUNT)||(i==next)) {
		return NO_MORE_INSTRUMENT ;
	}

	I_Instrument *src=instrument_[i] ;
	I_Instrument *dst=InstrumentTypes::Get(src->GetType())->create(next) ;
	IteratorPtr<Variable> it(src->GetIterator()) ;
	for (it->Begin();!it->IsDone();it->Next()) {
		Variable &srcV=it->CurrentItem() ;
		Variable *dstV=dst->FindVariable(srcV.GetID()) ;
		if (dstV) {
			dstV->CopyFrom(srcV) ;
		}
	}
	replace(next,dst) ;
	return next ;

}

// The one place a slot's instrument is deleted, apart from the destructor.
// Under the mixer lock (the audio thread renders under it; SDL mutexes are
// recursive, and button handlers already hold it): the player lets go of
// the old instrument, then its observers, then it's deleted and the new one
// (or none) goes in. Channels that last played it fall back to the empty
// stand-in, not to slot 00.
void InstrumentBank::replace(int i,I_Instrument *instrument) {
	MixerService *ms=MixerService::GetInstance() ;
	ms->Lock() ;
	I_Instrument *old=instrument_[i] ;
	if (old) {
		Player::GetInstance()->ForgetInstrument(old,&empty_) ;
		InstrumentEvent event ;
		event.type_=IET_DELETED ;
		old->SetChanged() ;
		old->NotifyObservers(&event) ;
		delete old ;
	}
	instrument_[i]=instrument ;
	ms->Unlock() ;
} ;

void InstrumentBank::OnStart() {
	for (int i=0;i<MAX_INSTRUMENT_COUNT;i++) {
		instrument_[i]->OnStart() ;
	}
	init_filters() ;
} ;
