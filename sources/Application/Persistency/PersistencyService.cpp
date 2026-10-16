#include "PersistencyService.h"
#include "Persistent.h"
#include "Externals/Compression/lz.h"
#include "System/Console/Trace.h"
#include "Foundation/Types/Types.h"

// Largest uncompressed length an LZ save may claim. Real projects are under
// 1 MB, and the first 4 bytes of a text file read as a negative int or at
// least 0x09000000
static const int MAX_LZ_LENGTH=16*1024*1024 ;

PersistencyService::PersistencyService():Service(MAKE_FOURCC('S','V','P','S')) {
} ;

void PersistencyService::Save(const char *name) {

    Path filename(name);

    TiXmlDocument doc(filename.GetPath());
    TiXmlElement first("LITTLEGPTRACKER") ;
	TiXmlNode *node=doc.InsertEndChild(first) ;

	// Loop on all registered service
	// accumulating XML flow
	
	IteratorPtr<SubService> it(GetIterator()) ;
	for (it->Begin();!it->IsDone();it->Next()) {
		Persistent *currentItem=(Persistent *)&it->CurrentItem() ;
		currentItem->Save(node) ;
	} ;

	doc.SaveFile() ;
};

bool PersistencyService::Load() {

	Path filename("project:lgptsav.dat") ;
	PersistencyDocument doc( filename.GetPath() );

	// Try opening the file
	
	FileSystem *fs=FileSystem::GetInstance() ;
	I_File *file=fs->Open(filename.GetPath().c_str(),"r") ;
	if (!file) return false ;
	
	// get file size and read all buffer
	
	file->Seek(0,SEEK_END) ;
	int length=file->Tell() ;

	// One more byte for a terminating NUL: TinyXML parses a C string
	unsigned char *compBuffer=(unsigned char *)SYS_MALLOC(length+1) ;

  file->Seek(0,SEEK_SET) ;
	file->Read(compBuffer,1,length) ;
	compBuffer[length]=0 ;
	file->Close();
	delete file ;
	
	// Decide by the error, not the return value: Parse() also returns 0
	// for a document that ends right after its last '>'
	doc.Parse((char *)compBuffer) ;
	if (doc.Error()) {
        
		// Get uncompressed buffer size from first bytes. A plain save that
		// doesn't parse gets an implausible one and is refused here
		
		int offset=sizeof(int) ;
		int fullLength=0 ;
		if (length>offset) memcpy(&fullLength,compBuffer,offset) ;
		if (fullLength<=0 || fullLength>MAX_LZ_LENGTH) {
			Trace::Error("%s: XML error '%s' (row %d, col %d), and not an LZ save",
				filename.GetPath().c_str(),doc.ErrorDesc(),doc.ErrorRow(),doc.ErrorCol()) ;
			SYS_FREE(compBuffer) ;
			return false ;
		}
		
		// Allocate a buffer to decompress data
		
		unsigned char *xmlSource=(unsigned char *)SYS_MALLOC(fullLength+1) ;
		if (!xmlSource) {
			Trace::Error("could not allocate space for %d bytes",fullLength) ;
			SYS_FREE(compBuffer) ;
			return false ;
		}

    LZ_Uncompress(compBuffer+offset,xmlSource,length-offset);
		xmlSource[fullLength]=0 ;

		// Initialize XML document on decompressed buffer
		doc.Parse((char *)xmlSource) ;

		SYS_FREE(xmlSource) ;

		
	} ; 
	SYS_FREE(compBuffer) ;

	TiXmlNode* node = 0;
	node = doc.FirstChild( "LITTLEGPTRACKER" );
	if (!node) {
		Trace::Error("could not find master node") ;
		return false ;
	};

	TiXmlElement* element =node->ToElement();
	node = element->FirstChildElement() ;
	if (node) {
		element = node->ToElement();
		while (element) {
			IteratorPtr<SubService> it(GetIterator()) ;
			for (it->Begin();!it->IsDone();it->Next()) {
				Persistent *currentItem=(Persistent *)&it->CurrentItem() ;
				if (currentItem->Restore(element)) {
					break ;
				} ;
			}
			element = element->NextSiblingElement();
		} ;
	}
	return true ;
} ;
