
#include "content/web_impl_win/WebCryptoImpl.h"

#include <new>
#include <openssl/evp.h>
#include <windows.h>

namespace content {

namespace {

const EVP_MD* digestAlgorithm(blink::WebCryptoAlgorithmId algorithmId) {
    switch (algorithmId) {
    case blink::WebCryptoAlgorithmIdSha1:
        return EVP_sha1();
    case blink::WebCryptoAlgorithmIdSha256:
        return EVP_sha256();
    case blink::WebCryptoAlgorithmIdSha384:
        return EVP_sha384();
    case blink::WebCryptoAlgorithmIdSha512:
        return EVP_sha512();
    default:
        return nullptr;
    }
}

}

WebCryptoDigestorImpl::WebCryptoDigestorImpl(blink::WebCryptoAlgorithmId algorithmId)
    : m_context(nullptr)
    , m_expectedSize(0)
    , m_result()
    , m_failed(true)
    , m_finished(false) {
    const EVP_MD* algorithm = digestAlgorithm(algorithmId);
    if (!algorithm)
        return;
    const int digestSize = EVP_MD_size(algorithm);
    if (digestSize <= 0 || static_cast<unsigned>(digestSize) > sizeof(m_result))
        return;
    m_context = EVP_MD_CTX_create();
    if (!m_context || EVP_DigestInit_ex(m_context, algorithm, nullptr) != 1)
        return;
    m_expectedSize = static_cast<unsigned>(digestSize);
    m_failed = false;
}

WebCryptoDigestorImpl::~WebCryptoDigestorImpl() {
    if (m_context)
        EVP_MD_CTX_destroy(m_context);
}

bool WebCryptoDigestorImpl::consume(const unsigned char* data, unsigned dataSize) {
    if (m_failed || m_finished)
        return false;
    if (!dataSize)
        return true;
    if (!data || EVP_DigestUpdate(m_context, data, dataSize) != 1) {
        m_failed = true;
        return false;
    }
    return true;
}

bool WebCryptoDigestorImpl::finish(unsigned char*& resultData, unsigned& resultDataSize) {
    resultData = nullptr;
    resultDataSize = 0;
    if (m_failed || m_finished)
        return false;
    m_finished = true;
    unsigned size = 0;
    if (EVP_DigestFinal_ex(m_context, m_result, &size) != 1 || size != m_expectedSize) {
        m_failed = true;
        return false;
    }
    resultData = m_result;
    resultDataSize = size;
    return true;
}
    
WebCryptoImpl::WebCryptoImpl() {

}

blink::WebCryptoDigestor* WebCryptoImpl::createDigestor(blink::WebCryptoAlgorithmId algorithmId) {
    WebCryptoDigestorImpl* digestor = new (std::nothrow) WebCryptoDigestorImpl(algorithmId);
    if (!digestor || !digestor->isValid()) {
        delete digestor;
        return nullptr;
    }
    return digestor;
}

}

namespace blink {
  
void WebCryptoResult::completeWithError(WebCryptoErrorType, const WebString&) 
{
    DebugBreak();
}

void WebCryptoResult::reset()
{
    DebugBreak();
}

}