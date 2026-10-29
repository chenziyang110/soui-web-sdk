#include "content/web_impl_win/WebCryptoImpl.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// The real adapter TU also emits unused WebCrypto platform virtuals. Their
// external WebString paths are outside this digestor data test. Keep the real
// Blink types and fail immediately if any of those paths is actually used.
namespace blink {

void WebString::reset() {
    std::abort();
}

void WebString::assign(const WebString&) {
    std::abort();
}

WebString WebString::fromUTF8(const char*, size_t) {
    std::abort();
}

}

namespace {

void expect(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

std::string hex(const unsigned char* data, unsigned size) {
    const char digits[] = "0123456789abcdef";
    std::string result;
    for (unsigned i = 0; i < size; ++i) {
        result += digits[data[i] >> 4];
        result += digits[data[i] & 15];
    }
    return result;
}

std::vector<unsigned char> digest(blink::WebCryptoAlgorithmId algorithm,
    const std::vector<unsigned char>& input, unsigned chunkSize) {
    std::unique_ptr<content::WebCryptoDigestorImpl> value(new content::WebCryptoDigestorImpl(algorithm));
    expect(value->isValid(), "supported digest must initialize");
    expect(value->consume(nullptr, 0), "empty chunk must succeed");
    for (size_t offset = 0; offset < input.size();) {
        const unsigned size = static_cast<unsigned>(std::min<size_t>(chunkSize, input.size() - offset));
        expect(value->consume(input.data() + offset, size), "chunk must be consumed");
        offset += size;
    }
    unsigned char* result = nullptr;
    unsigned size = 0;
    expect(value->finish(result, size), "finish must succeed once");
    expect(result != nullptr && size > 0 && size <= 64, "digest length must be bounded");
    const std::vector<unsigned char> saved(result, result + size);
    expect(!value->consume(nullptr, 0), "consume after finish must fail");
    unsigned char* repeated = result;
    unsigned repeatedSize = size;
    expect(!value->finish(repeated, repeatedSize), "second finish must fail");
    expect(repeated == nullptr && repeatedSize == 0, "failed finish must clear outputs");
    expect(std::equal(saved.begin(), saved.end(), result), "result must remain valid until destruction");
    return saved;
}

void knownVectors() {
    struct Vector {
        blink::WebCryptoAlgorithmId algorithm;
        const char* empty;
        const char* abc;
    };
    const Vector vectors[] = {
        {blink::WebCryptoAlgorithmIdSha1,
            "da39a3ee5e6b4b0d3255bfef95601890afd80709",
            "a9993e364706816aba3e25717850c26c9cd0d89d"},
        {blink::WebCryptoAlgorithmIdSha256,
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {blink::WebCryptoAlgorithmIdSha384,
            "38b060a751ac96384cd9327eb1b1e36a21fdb71114be07434c0cc7bf63f6e1da274edebfe76f65fbd51ad2f14898b95b",
            "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7"},
        {blink::WebCryptoAlgorithmIdSha512,
            "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e",
            "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"},
    };
    const std::vector<unsigned char> empty;
    const std::vector<unsigned char> abc = {'a', 'b', 'c'};
    std::vector<unsigned char> binary(1025);
    for (size_t i = 0; i < binary.size(); ++i)
        binary[i] = static_cast<unsigned char>(i);
    for (const Vector& vector : vectors) {
        const std::vector<unsigned char> emptyResult = digest(vector.algorithm, empty, 1);
        expect(hex(emptyResult.data(), static_cast<unsigned>(emptyResult.size())) == vector.empty, "empty known vector");
        const std::vector<unsigned char> abcResult = digest(vector.algorithm, abc, 3);
        expect(hex(abcResult.data(), static_cast<unsigned>(abcResult.size())) == vector.abc, "abc known vector");
        expect(abcResult == digest(vector.algorithm, abc, 1), "abc chunk equivalence");
        expect(digest(vector.algorithm, binary, 1025) == digest(vector.algorithm, binary, 17), "binary chunk equivalence");
    }
}

void invalidInputAndAlgorithms() {
    content::WebCryptoDigestorImpl nonDigest(blink::WebCryptoAlgorithmIdAesCbc);
    content::WebCryptoDigestorImpl unknown(static_cast<blink::WebCryptoAlgorithmId>(999));
    expect(!nonDigest.isValid() && !unknown.isValid(), "unsupported algorithms must fail closed");
    expect(!nonDigest.consume(nullptr, 0) && !unknown.consume(nullptr, 0), "unsupported digestors must reject input");
    std::unique_ptr<content::WebCryptoDigestorImpl> failed(new content::WebCryptoDigestorImpl(blink::WebCryptoAlgorithmIdSha256));
    expect(failed->isValid(), "valid digest must initialize");
    expect(!failed->consume(nullptr, 1), "nonempty null input must fail");
    expect(!failed->consume(reinterpret_cast<const unsigned char*>("a"), 1), "input failure must be sticky");
    unsigned char sentinel = 0;
    unsigned char* result = &sentinel;
    unsigned size = 123;
    expect(!failed->finish(result, size), "failed digest must not finish");
    expect(result == nullptr && size == 0, "failure must not expose a partial digest");

    std::unique_ptr<content::WebCryptoDigestorImpl> first(new content::WebCryptoDigestorImpl(blink::WebCryptoAlgorithmIdSha1));
    std::unique_ptr<content::WebCryptoDigestorImpl> second(new content::WebCryptoDigestorImpl(blink::WebCryptoAlgorithmIdSha1));
    expect(first->isValid() && second->isValid(), "independent digestors must initialize");
    expect(first->consume(reinterpret_cast<const unsigned char*>("a"), 1), "first digestor input");
    expect(second->consume(reinterpret_cast<const unsigned char*>("abc"), 3), "second digestor input");
    expect(first->consume(reinterpret_cast<const unsigned char*>("bc"), 2), "interleaved digestor input");
    unsigned char* firstResult = nullptr;
    unsigned char* secondResult = nullptr;
    unsigned firstSize = 0;
    unsigned secondSize = 0;
    expect(first->finish(firstResult, firstSize) && second->finish(secondResult, secondSize), "independent finish");
    expect(firstSize == secondSize && std::memcmp(firstResult, secondResult, firstSize) == 0, "contexts must not share input state");
}

void domDigestPrefixRegression() {
    // DOMPatchSupport hashes the raw NodeType, nodeName and nodeValue, then
    // base64-encodes only ten digest bytes. The old raw-input implementation
    // silently treated these different text nodes as the same digest.
    static_assert(sizeof(unsigned) == 4, "Windows NodeType regression expects four bytes");
    const unsigned textNodeType = 3;
    std::vector<unsigned char> text42(sizeof(textNodeType));
    std::memcpy(text42.data(), &textNodeType, sizeof(textNodeType));
    const char suffix[] = "#text42";
    text42.insert(text42.end(), suffix, suffix + sizeof(suffix) - 1);
    std::vector<unsigned char> text43(text42);
    text43.back() = '3';
    expect(std::equal(text42.begin(), text42.begin() + 10, text43.begin()), "fixture must reproduce the old ten-byte collision");
    const std::vector<unsigned char> before = digest(blink::WebCryptoAlgorithmIdSha1, text42, 4);
    const std::vector<unsigned char> after = digest(blink::WebCryptoAlgorithmIdSha1, text43, 4);
    expect(before.size() == 20 && after.size() == 20, "DOM digest must be real SHA-1");
    expect(!std::equal(before.begin(), before.begin() + 10, after.begin()), "42 and 43 must differ in DOM's ten-byte digest prefix");
}

}

int main() {
    try {
        knownVectors();
        invalidInputAndAlgorithms();
        domDigestPrefixRegression();
        std::puts("PASS: four SHA vectors, streaming, lifecycle, failure closure and DOM 42/43 digest regression");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
