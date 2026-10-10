
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

// LAYOUT attribute written on INSTRUMENTBANK. Files without it (upstream's,
// and every older one) get the legacy MIDI rule after loading
#define INSTRUMENT_BANK_LAYOUT 2

// Contain all instrument definition. Every slot starts empty

InstrumentBank::InstrumentBank():Persistent("INSTRUMENTBANK") {
	for (int i=0;i<MAX_INSTRUMENT_COUNT;i++) {
		instrument_[i]=0 ;
	}
	layout_=1 ;
    Status::Set("All instrument loaded") ;
} ;

//
// Assigns default instruments value for new project: one sample slot per
// sample already in the pool
//

void InstrumentBank::AssignDefaults() {

	SamplePool *pool=SamplePool::GetInstance() ;
	for (int i=0;i<pool->GetNameListSize();i++) {
		SampleInstrument *s=(SampleInstrument *)Create(i,IT_SAMPLE) ;
		if (s) {
			s->AssignSample(i) ;
		}
	} ;
} ;

InstrumentBank::~InstrumentBank() {
	for (int i=0;i<MAX_INSTRUMENT_COUNT;i++) {
		delete instrument_[i] ;
	}	
} ;

I_Instrument *InstrumentBank::GetInstrument(int i) {
	if (IsSlotEmpty(i)) {
		return &empty_ ;
	}
	return instrument_[i] ;
} ;

bool InstrumentBank::IsSlotEmpty(int i) {
	return (i<0)||(i>=MAX_INSTRUMENT_COUNT)||(!instrument_[i]) ;
} ;

void InstrumentBank::SaveContent(TiXmlNode *node) {
	TiXmlElement *element=(TiXmlElement *)node ;
	element->SetAttribute("LAYOUT",INSTRUMENT_BANK_LAYOUT) ;
	char hex[3] ;
	for (int i=0;i<MAX_INSTRUMENT_COUNT;i++) {

		I_Instrument *instr=instrument_[i] ;
		// A sample slot without a sample isn't written: it loads as empty
		if ((instr)&&(!instr->IsEmpty())) {
			TiXmlElement data("INSTRUMENT") ;
			hex2char(i,hex) ;
			data.SetAttribute("ID",hex) ;
			data.SetAttribute("TYPE",InstrumentTypes::Get(instr->GetType())->saveName) ;

			IteratorPtr<Variable> it(instr->GetIterator()) ;
			for (it->Begin();!it->IsDone();it->Next()) {
				Variable &v=it->CurrentItem() ;
				TiXmlElement param("PARAM") ;
				param.SetAttribute("NAME",v.GetName()) ;
				param.SetAttribute("VALUE",v.GetString()) ;
				data.InsertEndChild(param) ;
			}
			// Written even without parameters: the entry is the slot's type
			node->InsertEndChild(data) ;
		}
	}
} ;

void InstrumentBank::RestoreContent(TiXmlElement *element) {

	TiXmlElement *current=element->FirstChildElement() ;

	layout_=1 ;
	element->Attribute("LAYOUT",&layout_) ;

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
					// Unknown type (e.g. saved by a newer build): the slot
					// stays as it is (empty), params skipped
					Trace::Error("Unknown instrument type '%s' for instrument %s, leaving slot empty",instype?instype:"(none)",hexid) ;
					instr=0 ;
				} else if ((!instr)||(instr->GetType()!=info->type)) {
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

// Upstream's constructor filled 80-8F with MIDI instruments, so a file
// without LAYOUT 2 that plays one of those slots without an entry for it
// (only possible before 2014: since then every MIDI entry is written) gets
// that default. Not done in RestoreContent: the phrases may load after the
// bank. Our own files skip it, so an empty slot stays empty
void InstrumentBank::RestoreLegacyMidi(const unsigned char *phraseInstruments) {
	if (layout_>=INSTRUMENT_BANK_LAYOUT) {
		return ;
	}
	for (int k=0;k<PHRASE_COUNT*16;k++) {
		int i=phraseInstruments[k] ;
		if (!IsSlotEmpty(i)) {
			continue ;
		}
		const InstrumentTypeInfo *info=InstrumentTypes::FindByLegacySlot(i) ;
		if ((info)&&(info->type==IT_MIDI)) {
			replace(i,info->create(i)) ;
		}
	}
} ;

void InstrumentBank::Init() {
	for (int i=0;i<MAX_INSTRUMENT_COUNT;i++) {
		if (instrument_[i]) {
			instrument_[i]->Init() ;
		}
	}
}

unsigned short InstrumentBank::FirstEmpty() {
	for (int i=0;i<MAX_INSTRUMENT_COUNT;i++) {
		if (!instrument_[i]) {
			return i ;
		}
	}
	return NO_MORE_INSTRUMENT ;
} ;

I_Instrument *InstrumentBank::Create(int i,InstrumentType type) {
	const InstrumentTypeInfo *info=InstrumentTypes::Get(type) ;
	if ((i<0)||(i>=MAX_INSTRUMENT_COUNT)||(!info)) {
		return 0 ;
	}
	I_Instrument *instr=info->create(i) ;
	replace(i,instr) ;
	return instr ;
} ;

void InstrumentBank::Empty(int i) {
	if (!IsSlotEmpty(i)) {
		replace(i,0) ;
	}
} ;

int InstrumentBank::ReferenceCount(int i,const unsigned char *phraseInstruments) {
	int count=0 ;
	for (int p=0;p<PHRASE_COUNT;p++) {
		for (int r=0;r<16;r++) {
			if (phraseInstruments[16*p+r]==i) {
				count++ ;
				break ;
			}
		}
	}
	return count ;
} ;

unsigned short InstrumentBank::Clone(unsigned short i) {

	// Nothing to clone in an empty slot or outside the bank
	if (IsSlotEmpty(i)) {
		return NO_MORE_INSTRUMENT ;
	}

	unsigned short next=FirstEmpty() ;
	if (next==NO_MORE_INSTRUMENT) 
  {
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
// Every other change to a slot goes through here too.
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
		if (instrument_[i]) {
			instrument_[i]->OnStart() ;
		}
	}
	init_filters() ;
} ;
