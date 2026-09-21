#include "config.h"

#include "content/web_impl_win/WebURLLoaderImplCurl.h"
#include "content/web_impl_win/WebBlobRegistryImpl.h"

#include "net/WebURLLoaderManager.h"
#include "net/WebURLLoaderInternal.h"
#include "net/BlinkSynchronousLoader.h"
#include "net/BlobResourceLoader.h"
#include "content/web_impl_win/BlinkPlatformImpl.h"
#include "content/web_impl_win/CurrentTimeImpl.h"
#include "third_party/WebKit/public/platform/WebURLLoaderClient.h"
#include "third_party/WebKit/public/platform/WebURLRequest.h"
#include "third_party/WebKit/public/platform/WebURLResponse.h"
#include "third_party/WebKit/public/platform/WebURLError.h"
#include "third_party/WebKit/Source/platform/MIMETypeRegistry.h"
#include "third_party/WebKit/Source/wtf/text/WTFStringUtil.h"
#include "third_party/WebKit/Source/wtf/RefCountedLeakCounter.h"

#include <windows.h>

#ifndef NDEBUG
DEFINE_DEBUG_ONLY_GLOBAL(WTF::RefCountedLeakCounter, webURLLoaderImplCurlCount, ("WebURLLoaderImplCurl"));
#endif

namespace content {

namespace {

static bool resolveLocalFilePath(const blink::KURL& url, String& fileName)
{
    Vector<UChar> fileNameVec = WTF::ensureUTF16UChar(url.fileSystemPath(), false);
    if (fileNameVec.isEmpty())
        return false;

    fileName = String(fileNameVec.data(), fileNameVec.size());
    if (fileName.isEmpty())
        return false;
    if (L'/' == fileName[0])
        fileName.remove(0);

    // KURL normally returns a drive-qualified path. Preserve the historical
    // relative-file behavior for callers that provide a bare path.
    if (WTF::kNotFound == fileName.find(":/") &&
        WTF::kNotFound == fileName.find(":\\") &&
        !fileName.startsWith("\\\\")) {
        WTF::Vector<WCHAR> moduleName;
        moduleName.resize(MAX_PATH + 3);
        moduleName[MAX_PATH] = 0;
        const DWORD length = ::GetModuleFileNameW(NULL, moduleName.data(), MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
            return false;

        size_t moduleNameLen = length;
        while (moduleNameLen > 0 && moduleName[moduleNameLen - 1] != L'\\' && moduleName[moduleNameLen - 1] != L'/')
            --moduleNameLen;
        if (moduleNameLen == 0)
            return false;
        fileName.insert(moduleName.data(), static_cast<unsigned>(moduleNameLen), 0);
    }
    fileName.replace("/", "\\");
    return true;
}

static void initializeLocalResponse(const blink::KURL& url, const String& fileName,
                                    blink::WebURLResponse& response)
{
    response.initialize();
    response.setURL(blink::WebURL(url));
    response.setHTTPStatusCode(200);
    response.setHTTPStatusText(blink::WebString::fromUTF8("ok"));

    const int dotPos = fileName.reverseFind('.');
    const int slashPos = fileName.reverseFind('\\');
    if (slashPos < dotPos && dotPos != -1) {
        const String ext = fileName.substring(dotPos + 1);
        String mime = blink::MIMETypeRegistry::getMIMETypeForExtension(ext);
        // The embedded registry intentionally only contains a small legacy
        // map and has no CSS entry.  ResourceLoader uses the HTTP header for
        // script/CSS admission, so publish a deterministic local-file type.
        if (mime.isEmpty()) {
            if (equalIgnoringCase(ext, "css"))
                mime = "text/css";
            else if (equalIgnoringCase(ext, "js") || equalIgnoringCase(ext, "mjs"))
                mime = "text/javascript";
            else if (equalIgnoringCase(ext, "json"))
                mime = "application/json";
        }
        if (!mime.isEmpty()) {
            response.setMIMEType(mime);
            response.setHTTPHeaderField(blink::WebString::fromUTF8("Content-Type"), blink::WebString(mime));
        }
    }
}

static bool readLocalFile(const blink::KURL& url, String& fileName, Vector<char>& data,
                          DWORD& errorCode)
{
    errorCode = ERROR_SUCCESS;
    if (!resolveLocalFilePath(url, fileName)) {
        errorCode = ERROR_INVALID_NAME;
        return false;
    }

    HANDLE fileHandle = ::CreateFileW(
        static_cast<LPCWSTR>(fileName.charactersWithNullTermination().data()),
        GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, 0);
    if (fileHandle == INVALID_HANDLE_VALUE) {
        errorCode = GetLastError();
        if (errorCode == ERROR_SUCCESS)
            errorCode = ERROR_FILE_NOT_FOUND;
        return false;
    }

    const DWORD chunkSize = 32 * 1024;
    char buffer[chunkSize];
    DWORD bytesRead = 0;
    bool result = true;
    do {
        bytesRead = 0;
        result = ::ReadFile(fileHandle, buffer, chunkSize, &bytesRead, 0) != FALSE;
        if (result && bytesRead)
            data.append(buffer, bytesRead);
    } while (result && bytesRead != 0);

    if (!result)
        errorCode = GetLastError();
    ::CloseHandle(fileHandle);
    return result;
}

static void reportLocalFileFailure(blink::WebURLLoaderClient* client,
                                   blink::WebURLLoader* loader, const String& fileName,
                                   DWORD errorCode)
{
    if (!client)
        return;

    blink::WebURLError error;
    error.reason = errorCode == ERROR_SUCCESS ? -1 : static_cast<int>(errorCode);
    error.domain = blink::WebString(fileName);
    error.localizedDescription = blink::WebString::fromUTF8("local file load failed\n");
    client->didFail(loader, error);
}

} // namespace

WebURLLoaderImplCurl::WebURLLoaderImplCurl()
{
    m_hadDestroied = nullptr;
    m_jobIds = 0;
    m_blobLoader = nullptr;
    m_client = nullptr;
    m_requestCanceled = false;
#ifndef NDEBUG
    webURLLoaderImplCurlCount.increment();
#endif
}

WebURLLoaderImplCurl::~WebURLLoaderImplCurl()
{
    m_client = nullptr;
    m_requestCanceled = true;
#ifndef NDEBUG
    webURLLoaderImplCurlCount.decrement();
#endif
}

void WebURLLoaderImplCurl::init()
{
    m_hadDestroied = nullptr;
    m_jobIds = 0;
    m_blobLoader = nullptr;
    m_client = nullptr;
    m_requestCanceled = false;
}

static bool shouldContentSniffURL(const KURL& url)
{
    // We shouldn't content sniff file URLs as their MIME type should be established via their extension.
    return !url.protocolIs("file");
}

void WebURLLoaderImplCurl::loadSynchronously(
    const blink::WebURLRequest& request,
    blink::WebURLResponse& response,
    blink::WebURLError& error,
    blink::WebData& data)
{
    if (KURL(request.url()).isLocalFile() && request.frameType() != blink::WebURLRequest::FrameTypeTopLevel) {
        init();
        String fileName;
        Vector<char> buffer;
        DWORD errorCode = ERROR_SUCCESS;
        if (!readLocalFile(request.url(), fileName, buffer, errorCode)) {
            error.reason = errorCode == ERROR_SUCCESS ? -1 : static_cast<int>(errorCode);
            error.domain = blink::WebString(fileName);
            error.localizedDescription = blink::WebString::fromUTF8("local file load failed\n");
            return;
        }
        initializeLocalResponse(request.url(), fileName, response);
        data.assign(buffer.data(), buffer.size());
        return;
    }

    if (!net::WebURLLoaderManager::sharedInstance())
        return;

    init();

    WebURLRequest requestNew = request;

    Vector<char> buffer;
    net::BlinkSynchronousLoader syncLoader(error, response, buffer);
    net::WebURLLoaderInternal* job = new net::WebURLLoaderInternal(net::WebURLLoaderManager::sharedInstance()->getIoThread(net::WebURLLoaderManager::kIoThreadTypeSync), this, requestNew, &syncLoader, false, shouldContentSniffURL(request.url()));
    net::WebURLLoaderManager::sharedInstance()->dispatchSynchronousJob(job);

    data.assign(buffer.data(), buffer.size());
}

void WebURLLoaderImplCurl::loadAsynchronously(const blink::WebURLRequest& request, blink::WebURLLoaderClient* client)
{
    init();

    if (KURL(request.url()).isLocalFile() && request.frameType() != blink::WebURLRequest::FrameTypeTopLevel) {
        m_client = client;
        fileLoadImpl(request.url());
        return;
    }

    if (!net::WebURLLoaderManager::sharedInstance())
        return;

    KURL url = request.url();
    if (url.protocol() == "blob") {
        WebBlobRegistryImpl* blolRegistry = (WebBlobRegistryImpl*)blink::Platform::current()->blobRegistry();
        net::BlobDataWrap* blogData = blolRegistry->getBlobDataFromUUID(url.string());
        if (!blogData)
            return;

        blolRegistry->abortStream(WebURL());

        m_blobLoader = net::BlobResourceLoader::createAsync(blogData, request, client, this);
        m_blobLoader->start();
        return;
    }

    WebURLRequest requestNew = request;
    net::WebURLLoaderInternal* job = new net::WebURLLoaderInternal(net::WebURLLoaderManager::sharedInstance()->getIoThread(net::WebURLLoaderManager::kIoThreadTypeOther), this, requestNew, client, false, shouldContentSniffURL(request.url()));
    int jobIds = net::WebURLLoaderManager::sharedInstance()->addAsynchronousJob(job);
    if (0 == jobIds)
        return;
    m_jobIds = jobIds;

    // 执行完add后，this可能被销毁，当dataurl的时候
#if 0
    blink::KURL url = (blink::KURL)requestNew.url();
    Vector<UChar> host = WTF::ensureUTF16UChar(url.host());

    if (!url.isValid() || !url.protocolIsData()) {
        WTF::String outstr = String::format("WebURLLoaderImpl.loadAsynchronously: %p %ws\n", this, WTF::ensureUTF16UChar(url.string()).data());
        OutputDebugStringW(outstr.charactersWithNullTermination().data());
    }
#endif
}

void WebURLLoaderImplCurl::cancel()
{
    m_requestCanceled = true;
    m_client = nullptr;
    if (m_blobLoader) {
        m_blobLoader->cancel();
        delete m_blobLoader;
        m_blobLoader = nullptr;
        return;
    }

    if (0 != m_jobIds && net::WebURLLoaderManager::sharedInstance())
        net::WebURLLoaderManager::sharedInstance()->cancel(m_jobIds);
    m_jobIds = 0;
}

void WebURLLoaderImplCurl::fileLoadImpl(const blink::KURL& url)
{
    if (requestCanceled() || !m_client)
        return;

    String fileName;
    Vector<char> data;
    DWORD errorCode = ERROR_SUCCESS;
    if (!readLocalFile(url, fileName, data, errorCode)) {
        reportLocalFileFailure(m_client, this, fileName, errorCode);
        return;
    }

    blink::WebURLLoaderClient* client = m_client;
    blink::WebURLResponse response;
    initializeLocalResponse(url, fileName, response);
    client->didReceiveResponse(this, response);
    if (requestCanceled())
        return;

    const size_t chunkSize = 32 * 1024;
    for (size_t offset = 0; offset < data.size() && !requestCanceled(); offset += chunkSize) {
        const size_t remaining = data.size() - offset;
        const int length = static_cast<int>(remaining < chunkSize ? remaining : chunkSize);
        client->didReceiveData(this, data.data() + offset, length, -1);
    }
    if (!requestCanceled()) {
        client->didFinishLoading(this, WTF::currentTime(), data.size());
    }
}

void WebURLLoaderImplCurl::setDefersLoading(bool value)
{
    //notImplemented();
}

void WebURLLoaderImplCurl::didChangePriority(blink::WebURLRequest::Priority new_priority, int intra_priority_value)
{
    //notImplemented();
}

bool WebURLLoaderImplCurl::attachThreadedDataReceiver(blink::WebThreadedDataReceiver* threaded_data_receiver)
{
    notImplemented();
    return false;
}

} // content