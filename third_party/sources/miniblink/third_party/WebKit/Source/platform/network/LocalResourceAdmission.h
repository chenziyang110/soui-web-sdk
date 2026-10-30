#ifndef SOUI_LOCAL_RESOURCE_ADMISSION_H
#define SOUI_LOCAL_RESOURCE_ADMISSION_H
#include <memory>
namespace blink {
class LocalResourceAdmission;
class ExecutionContext;
class ResourceRequest;
class KURL;
// Private identity/reservation bridge. These functions never grant an origin
// inferred from URL text; only the actual Page binding can mint a cell.
bool isLocalResourceNamespace(const KURL&);
bool hasLocalResourceOwner(ExecutionContext*);
bool prepareLocalResourceRequest(ExecutionContext*, ResourceRequest&);
void identifyLocalResourceRequest(const void* page, const KURL& documentUrl,
    bool mainFrame, ResourceRequest&);
bool admitLocalResourceRequest(ResourceRequest&);
void abandonLocalResourceRequest(ResourceRequest&);
void claimLocalResourceLoaderReservation(ResourceRequest&);
void cancelLocalResourceLoaderReservation(ResourceRequest&);
}
#endif
