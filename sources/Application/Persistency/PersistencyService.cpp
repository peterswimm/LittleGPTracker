#include "PersistencyService.h"
#include "Persistent.h"
#include "Externals/Compression/lz.h"
#include "System/Console/Trace.h"
#include "Foundation/Types/Types.h"

// Far above any real project (a full one is well under 1 MB of XML), and
// low enough that a damaged size cannot ask for an absurd allocation.
#define MAX_UNCOMPRESSED_PROJECT (16 * 1024 * 1024)

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

    // One more byte for the terminator TinyXML parses up to
    unsigned char *compBuffer =
        (length > 0) ? (unsigned char *)SYS_MALLOC(length + 1) : 0;
    if (!compBuffer) {
        Trace::Error("could not read project file (%d bytes)", length);
        file->Close();
        delete file;
        return false;
    }

  file->Seek(0,SEEK_SET) ;
	file->Read(compBuffer,1,length) ;
    compBuffer[length] = 0;
    file->Close();
    delete file;

    if (!doc.Parse((char *)compBuffer)) {

        // Not XML: an LZ-compressed file, starting with the uncompressed
        // size. Check that size and decode within it, since both come
        // from the file.

        int offset = sizeof(int);
        int fullLength = 0;
        if (length > offset)
            memcpy(&fullLength, compBuffer, offset);
        if (fullLength <= 0 || fullLength > MAX_UNCOMPRESSED_PROJECT) {
            Trace::Error("project file is damaged (size %d)", fullLength);
            SYS_FREE(compBuffer);
            return false;
        }

        // Allocate a buffer to decompress data, plus the terminator

        unsigned char *xmlSource =
            (unsigned char *)SYS_MALLOC(fullLength + 1);
        if (!xmlSource) {
            Trace::Error("could not allocate space for %d bytes", fullLength);
            SYS_FREE(compBuffer);
            return false;
        }

        int got = LZ_UncompressBounded(compBuffer + offset, xmlSource,
                                       length - offset, fullLength);
        if (got != fullLength) {
            Trace::Error("project file is damaged (decoded %d of %d bytes)",
                         got, fullLength);
            SYS_FREE(xmlSource);
            SYS_FREE(compBuffer);
            return false;
        }
        xmlSource[fullLength] = 0;

        // Initialize XML document on decompressed buffer
		doc.Parse((char *)xmlSource) ;

		SYS_FREE(xmlSource) ;
    };
    SYS_FREE(compBuffer) ;

    TiXmlNode *node = 0;
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
