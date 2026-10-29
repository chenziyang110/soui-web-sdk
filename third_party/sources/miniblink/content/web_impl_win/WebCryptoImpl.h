#ifndef COMPONENTS_WEBCRYPTO_WEBCRYPTO_IMPL_H_
#define COMPONENTS_WEBCRYPTO_WEBCRYPTO_IMPL_H_

#include "third_party/WebKit/public/platform/WebCrypto.h"

struct env_md_ctx_st;

namespace content {

class WebCryptoDigestorImpl : public blink::WebCryptoDigestor {
public:
    explicit WebCryptoDigestorImpl(blink::WebCryptoAlgorithmId algorithmId);
    virtual ~WebCryptoDigestorImpl() override;
    virtual bool consume(const unsigned char* data, unsigned dataSize) override;
    virtual bool finish(unsigned char*& resultData, unsigned& resultDataSize) override;
    bool isValid() const { return !m_failed; }

private:
    WebCryptoDigestorImpl(const WebCryptoDigestorImpl&) = delete;
    WebCryptoDigestorImpl& operator=(const WebCryptoDigestorImpl&) = delete;

    env_md_ctx_st* m_context;
    unsigned m_expectedSize;
    unsigned char m_result[64]; // SHA-512 is the largest supported digest.
    bool m_failed;
    bool m_finished;
};
    
class WebCryptoImpl : public blink::WebCrypto {
public:
    WebCryptoImpl();
    virtual blink::WebCryptoDigestor* createDigestor(blink::WebCryptoAlgorithmId algorithmId) override;

private:
    WebCryptoDigestorImpl* m_webCryptoDigestorImpl;
};

}

#endif