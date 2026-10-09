// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Some helper functions for working with the clipboard and IDataObjects.

#include "content/ui/ClipboardUtil.h"

#include "base/strings/string_util.h"
#include "base/json/json_reader.h"
#include "base/values.h"
#include <cstring>
#include <memory>
#include <utility>

// #if USING_VC6RT == 1
// #define PURE = 0
// #endif
// #include <ShlObj.h>

namespace content {

unsigned int ClipboardUtil::getHtmlFormatType()
{
    static unsigned int s_HtmlFormatType = ::RegisterClipboardFormat(L"HTML Format");
    return s_HtmlFormatType;
}

unsigned int ClipboardUtil::getWebKitSmartPasteFormatType()
{
    static unsigned int s_WebKitSmartPasteFormatType = ::RegisterClipboardFormat(L"WebKit Smart Paste Format");
    return s_WebKitSmartPasteFormatType;
}

unsigned int ClipboardUtil::getUrlWFormatType()
{
    static unsigned int s_UrlWFormatType = ::RegisterClipboardFormat(CFSTR_INETURLW);
    return s_UrlWFormatType;
}

unsigned int ClipboardUtil::getRtfFormatType()
{
    static unsigned int s_RtfFormatType = ::RegisterClipboardFormat(L"Rich Text Format");
    return s_RtfFormatType;
}

const unsigned int ClipboardUtil::getWebCustomDataFormatType()
{
    // TODO(dcheng): This name is temporary. See http://crbug.com/106449.
    static unsigned int s_WebCustomDataFormatType = ::RegisterClipboardFormat(L"Chromium Web Custom MIME Data Format");
    return s_WebCustomDataFormatType;
}

FORMATETC* ClipboardUtil::getPlainTextWFormatType()
{
    static FORMATETC textFormat = { CF_UNICODETEXT, 0, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    return &textFormat;
}

FORMATETC* ClipboardUtil::getPlainTextFormatType()
{
    static FORMATETC textFormat = { CF_UNICODETEXT, 0, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    return &textFormat;
}

FORMATETC* ClipboardUtil::urlWFormat()
{
    unsigned int cf = getUrlWFormatType();
    static FORMATETC urlFormat = { (CLIPFORMAT)cf, 0, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    return &urlFormat;
}

FORMATETC* ClipboardUtil::urlFormat()
{
    static unsigned int cf = RegisterClipboardFormat(L"UniformResourceLocator");
    static FORMATETC urlFormat = { (CLIPFORMAT)cf, 0, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    return &urlFormat;
}

FORMATETC* ClipboardUtil::getCustomTextsType()
{
    static unsigned int cf = RegisterClipboardFormat(L"MiniBlinkCustomTextsType");
    static FORMATETC urlFormat = { (CLIPFORMAT)cf, 0, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    return &urlFormat;
}

bool ClipboardUtil::getWebLocData(IDataObject* dataObject, std::string& url, std::string* title)
{
    bool succeeded = false;
    return succeeded;
}

std::string ClipboardUtil::extractURL(const std::string& inURL, std::string* title)
{
    std::string url = inURL;
    int splitLoc = url.find('\n');
    if (splitLoc > 0) {
        if (title)
            *title = url.substr(splitLoc + 1);
        url = url.substr(0, splitLoc);
    } else if (title)
        *title = url;
    return url;
}

std::string ClipboardUtil::getURL(IDataObject* dataObject, std::string* title)
{
    STGMEDIUM store;
    std::string url;
    if (getWebLocData(dataObject, url, title))
        return url;

    if (dataObject->GetData(urlWFormat(), &store) >= 0) {
        // URL using Unicode
        wchar_t* data = static_cast<wchar_t*>(GlobalLock(store.hGlobal));

        std::string dataA = base::WideToUTF8(std::wstring(data, wcslen(data)));
        url = extractURL(dataA, title);
        GlobalUnlock(store.hGlobal);
        ReleaseStgMedium(&store);
    } else if (dataObject->GetData(urlFormat(), &store) >= 0) {
        // URL using ASCII
        char* data = static_cast<char*>(GlobalLock(store.hGlobal));
        url = extractURL(std::string(data), title);
        GlobalUnlock(store.hGlobal);
        ReleaseStgMedium(&store);
    }
    return url;
}

std::string ClipboardUtil::getPlainText(IDataObject* dataObject)
{
    STGMEDIUM store;
    std::string text;
    if (SUCCEEDED(dataObject->GetData(getPlainTextWFormatType(), &store))) {
        // Unicode text
        wchar_t* data = static_cast<wchar_t*>(::GlobalLock(store.hGlobal));
        text = base::WideToUTF8(std::wstring(data, wcslen(data)));
        GlobalUnlock(store.hGlobal);
        ReleaseStgMedium(&store);
    } else if (SUCCEEDED(dataObject->GetData(getPlainTextFormatType(), &store))) {
        // ASCII text
        char* data = static_cast<char*>(GlobalLock(store.hGlobal));
        text = (data);
        GlobalUnlock(store.hGlobal);
        ReleaseStgMedium(&store);
    } else {
        // FIXME: Originally, we called getURL() here because dragging and dropping files doesn't
        // populate the drag with text data. Per https://bugs.webkit.org/show_bug.cgi?id=38826, this
        // is undesirable, so maybe this line can be removed.
        text = getURL(dataObject, nullptr);
    }
    return text;
}

base::DictionaryValue* ClipboardUtil::getCustomPlainTexts(IDataObject* dataObject)
{
    if (!dataObject)
        return nullptr;

    STGMEDIUM store;
    std::string text;
    if (!(SUCCEEDED(dataObject->GetData(getCustomTextsType(), &store))))
        return nullptr;

    wchar_t* data = static_cast<wchar_t*>(::GlobalLock(store.hGlobal));
    size_t size = GlobalSize(store.hGlobal) / sizeof(wchar_t);
    for (size_t i = 0; i < size; ++i) {
        if (L'\0' != data[i])
            continue;
        size = i;
        break;
    }
    if (0 == size)
        return nullptr;
    std::wstring utf16(data, size);
    std::string json = base::WideToUTF8(utf16);
    if (0 == json.size())
        return nullptr;
    
    base::JSONReader jsonReader;
    std::unique_ptr<base::Value> jsonVal = jsonReader.ReadToValue(json);
    if (!jsonVal || !jsonVal->IsType(base::Value::TYPE_DICTIONARY))
        return nullptr;

    base::DictionaryValue* dictVal = nullptr;
    if (!jsonVal->GetAsDictionary(&dictVal))
        return nullptr;

    jsonVal.release();

    GlobalUnlock(store.hGlobal);
    ReleaseStgMedium(&store);
    return dictVal;
}


namespace {
const size_t kBoundedClipboardBytes = 1024 * 1024;
const size_t kBoundedClipboardItems = 256;

class BoundedMediumOwner {
public:
    STGMEDIUM medium = {};
    bool accepted = false;
    ~BoundedMediumOwner() { if (accepted) ::ReleaseStgMedium(&medium); }
    BoundedMediumOwner() = default;
    BoundedMediumOwner(const BoundedMediumOwner&) = delete;
    BoundedMediumOwner& operator=(const BoundedMediumOwner&) = delete;
};

class BoundedGlobalLock {
public:
    explicit BoundedGlobalLock(HGLOBAL handle)
        : m_handle(handle), m_bytes(::GlobalLock(handle)) { }
    ~BoundedGlobalLock() { if (m_bytes) ::GlobalUnlock(m_handle); }
    const void* bytes() const { return m_bytes; }
    BoundedGlobalLock(const BoundedGlobalLock&) = delete;
    BoundedGlobalLock& operator=(const BoundedGlobalLock&) = delete;
private:
    HGLOBAL m_handle;
    const void* m_bytes;
};

bool boundedWideToUtf8(const WCHAR* text, size_t length, size_t limit, std::string& result)
{
    if (!length) { result.clear(); return true; }
    // Input length is below the 1MiB medium cap, hence representable as int.
    const int required = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        text, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    if (required <= 0 || static_cast<size_t>(required) > limit) return false;
    result.resize(required);
    return ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text,
        static_cast<int>(length), &result[0], required, nullptr, nullptr) == required;
}

bool boundedAnsiCodePage(IDataObject* dataObject, size_t limit, UINT& codePage)
{
    FORMATETC format = { CF_LOCALE, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    BoundedMediumOwner owner;
    const HRESULT status = dataObject->GetData(&format, &owner.medium);
    if (FAILED(status)) {
        if (status != DV_E_FORMATETC) return false;
        codePage = CP_ACP;
        return true;
    }
    owner.accepted = true;
    if (status != S_OK || owner.medium.tymed != TYMED_HGLOBAL || !owner.medium.hGlobal)
        return false;
    const SIZE_T bytes = ::GlobalSize(owner.medium.hGlobal);
    if (bytes < sizeof(LCID) || bytes > kBoundedClipboardBytes || bytes > limit) return false;
    BoundedGlobalLock locked(owner.medium.hGlobal);
    if (!locked.bytes()) return false;
    LCID locale = 0;
    std::memcpy(&locale, locked.bytes(), sizeof(locale));
    DWORD suppliedCodePage = 0;
    const int codePageCharacters = sizeof(suppliedCodePage) / sizeof(WCHAR);
    if (::GetLocaleInfoW(locale, LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER,
        reinterpret_cast<LPWSTR>(&suppliedCodePage), codePageCharacters) != codePageCharacters
        || !suppliedCodePage || !::IsValidCodePage(suppliedCodePage)) return false;
    codePage = suppliedCodePage;
    return true;
}

ClipboardUtil::BoundedReadResult boundedMediumText(IDataObject* dataObject,
    FORMATETC format, bool wide, size_t limit, std::string& result)
{
    BoundedMediumOwner owner;
    const HRESULT status = dataObject->GetData(&format, &owner.medium);
    if (FAILED(status))
        return status == DV_E_FORMATETC ? ClipboardUtil::BoundedReadAbsent : ClipboardUtil::BoundedReadFailure;
    owner.accepted = true; // First operation after success; every exit releases.
    if (status != S_OK || owner.medium.tymed != TYMED_HGLOBAL || !owner.medium.hGlobal)
        return ClipboardUtil::BoundedReadFailure;
    const SIZE_T bytes = ::GlobalSize(owner.medium.hGlobal);
    if (!bytes || bytes > kBoundedClipboardBytes || bytes > limit
        || (wide && (bytes < sizeof(WCHAR) || bytes % sizeof(WCHAR))))
        return ClipboardUtil::BoundedReadFailure;
    UINT codePage = CP_ACP;
    if (!wide && !boundedAnsiCodePage(dataObject, limit, codePage)) return ClipboardUtil::BoundedReadFailure;
    BoundedGlobalLock locked(owner.medium.hGlobal); // Unlock precedes medium release.
    if (!locked.bytes()) return ClipboardUtil::BoundedReadFailure;
    if (wide) {
        const WCHAR* text = static_cast<const WCHAR*>(locked.bytes());
        const size_t available = bytes / sizeof(WCHAR);
        size_t length = 0;
        while (length < available && text[length]) ++length;
        if (length == available) return ClipboardUtil::BoundedReadFailure;
        if (!boundedWideToUtf8(text, length, limit, result)) return ClipboardUtil::BoundedReadFailure;
    } else {
        const char* text = static_cast<const char*>(locked.bytes());
        const char* end = static_cast<const char*>(std::memchr(text, 0, bytes));
        if (!end) return ClipboardUtil::BoundedReadFailure;
        const size_t length = static_cast<size_t>(end - text);
        if (!length) { result.clear(); return ClipboardUtil::BoundedReadSuccess; }
        const int required = ::MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS,
            text, static_cast<int>(length), nullptr, 0);
        if (required <= 0 || static_cast<size_t>(required) > limit / sizeof(WCHAR))
            return ClipboardUtil::BoundedReadFailure;
        std::wstring converted(required, 0);
        if (::MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, text,
            static_cast<int>(length), &converted[0], required) != required
            || !boundedWideToUtf8(converted.data(), converted.size(), limit, result))
            return ClipboardUtil::BoundedReadFailure;
    }
    return ClipboardUtil::BoundedReadSuccess;
}

bool chargeBoundedString(const std::string& text, size_t& remaining)
{
    if (text.size() > remaining) return false;
    int wideLength = 0;
    if (!text.empty()) {
        wideLength = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (wideLength <= 0) return false;
    }
    // Both representations and terminators are bounded; arithmetic cannot
    // overflow because each input string is at most kBoundedClipboardBytes.
    const size_t charge = text.size() + 1 + (static_cast<size_t>(wideLength) + 1) * sizeof(WCHAR);
    if (charge > remaining) return false;
    remaining -= charge;
    return true;
}

bool chargeBoundedItem(const ClipboardUtil::BoundedTextItem& item, size_t& remaining)
{
    return chargeBoundedString(item.type, remaining) && chargeBoundedString(item.data, remaining);
}
} // namespace

ClipboardUtil::BoundedReadResult ClipboardUtil::getPlainTextBounded(IDataObject* dataObject,
    size_t& remainingPayloadBytes, BoundedTextItem& output) try
{
    if (!dataObject || remainingPayloadBytes > kBoundedClipboardBytes
        || !output.type.empty() || !output.data.empty()) return BoundedReadFailure;
    BoundedTextItem candidate;
    FORMATETC format = { CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    BoundedReadResult status = boundedMediumText(dataObject, format, true, remainingPayloadBytes, candidate.data);
    if (status == BoundedReadAbsent) {
        format.cfFormat = CF_TEXT; // Correct narrow format without changing legacy getters.
        status = boundedMediumText(dataObject, format, false, remainingPayloadBytes, candidate.data);
    }
    if (status != BoundedReadSuccess) return status;
    candidate.type = kMimeTypeText;
    size_t remaining = remainingPayloadBytes;
    if (!chargeBoundedItem(candidate, remaining)) return BoundedReadFailure;
    output.type.swap(candidate.type);
    output.data.swap(candidate.data);
    remainingPayloadBytes = remaining;
    return BoundedReadSuccess;
}
catch (...) { return BoundedReadFailure; }

ClipboardUtil::BoundedReadResult ClipboardUtil::getCustomPlainTextsBounded(IDataObject* dataObject,
    size_t& remainingPayloadBytes, std::vector<BoundedTextItem>& output, size_t maxItems) try
{
    if (!dataObject || remainingPayloadBytes > kBoundedClipboardBytes || !output.empty()
        || maxItems > kBoundedClipboardItems) return BoundedReadFailure;
    std::string json;
    const BoundedReadResult status = boundedMediumText(dataObject, *getCustomTextsType(), true,
        remainingPayloadBytes, json);
    if (status != BoundedReadSuccess) return status;
    base::JSONReader reader(base::JSON_PARSE_RFC);
    std::unique_ptr<base::Value> parsed = reader.ReadToValue(json);
    base::DictionaryValue* dictionary = nullptr;
    if (!parsed || !parsed->GetAsDictionary(&dictionary) || !dictionary) return BoundedReadFailure;
    size_t remaining = remainingPayloadBytes;
    std::vector<BoundedTextItem> candidate;
    for (base::DictionaryValue::Iterator it(*dictionary); !it.IsAtEnd(); it.Advance()) {
        if (candidate.size() >= maxItems || !it.value().IsType(base::Value::TYPE_STRING))
            return BoundedReadFailure;
        BoundedTextItem item;
        item.type = it.key();
        if (!it.value().GetAsString(&item.data) || !chargeBoundedItem(item, remaining))
            return BoundedReadFailure;
        candidate.push_back(std::move(item));
    }
    output.swap(candidate);
    remainingPayloadBytes = remaining;
    return BoundedReadSuccess;
}
catch (...) { return BoundedReadFailure; }

HGLOBAL ClipboardUtil::createGlobalData(const std::string& url, const std::string& title)
{
    std::wstring mutableURL(base::UTF8ToWide(url));
    std::wstring mutableTitle(base::UTF8ToWide(title));

    SIZE_T size = mutableURL.size() + mutableTitle.size() + 2; // +1 for "\n" and +1 for null terminator
    HGLOBAL cbData = ::GlobalAlloc(GPTR, size * sizeof(wchar_t));

    if (cbData) {
        PWSTR buffer = static_cast<PWSTR>(GlobalLock(cbData));
        _snwprintf(buffer, size, L"%s\n%s", &mutableURL[0], &mutableTitle[0]);
        GlobalUnlock(cbData);
    }
    return cbData;
}

// template <typename charT> HGLOBAL ClipboardUtil::createGlobalData(const std::basic_string<charT>& str)
// {
//     std::wstring strW(base::UTF8ToWide(str));
// 
//     HGLOBAL data = ::GlobalAlloc(GMEM_MOVEABLE, ((strW.size() + 1) * sizeof(wchar_t)));
//     if (data) {
//         wchar_t* rawData = static_cast<wchar_t*>(::GlobalLock(data));
//         memcpy(rawData, &strW[0], strW.size() * sizeof(wchar_t));
//         rawData[strW.size()] = L'\0';
//         ::GlobalUnlock(data);
//     }
//     return data;
// }

std::string ClipboardUtil::htmlToCFHtml(const std::string& html, const std::string& baseUrl)
{
    if (html.empty())
        return std::string();

#define MAX_DIGITS 10
#define MAKE_NUMBER_FORMAT_1(digits) MAKE_NUMBER_FORMAT_2(digits)
#define MAKE_NUMBER_FORMAT_2(digits) "%0" #digits "u"
#define NUMBER_FORMAT MAKE_NUMBER_FORMAT_1(MAX_DIGITS)

    static const char* header = "Version:0.9\r\n"
        "StartHTML:" NUMBER_FORMAT "\r\n"
        "EndHTML:" NUMBER_FORMAT "\r\n"
        "StartFragment:" NUMBER_FORMAT "\r\n"
        "EndFragment:" NUMBER_FORMAT "\r\n";
    static const char* sourceUrlPrefix = "SourceURL:";

    static const char* startMarkup = "<html>\r\n<body>\r\n<!--StartFragment-->";
    static const char* endMarkup = "<!--EndFragment-->\r\n</body>\r\n</html>";

    // Calculate offsets
    size_t startHtmlOffset = strlen(header) - strlen(NUMBER_FORMAT) * 4 + MAX_DIGITS * 4;
    if (!baseUrl.empty()) {
        startHtmlOffset += strlen(sourceUrlPrefix) + baseUrl.length() + 2;  // Add 2 for \r\n.
    }
    size_t startFragmentOffset = startHtmlOffset + strlen(startMarkup);
    size_t endFragmentOffset = startFragmentOffset + html.length();
    size_t endHtmlOffset = endFragmentOffset + strlen(endMarkup);

    std::vector<char> buffer;
    buffer.resize(2000);
    sprintf(&buffer[0], header, startHtmlOffset, endHtmlOffset, startFragmentOffset, endFragmentOffset);
    std::string result = &buffer[0];

    if (!baseUrl.empty()) {
        result.append(sourceUrlPrefix);
        result.append(baseUrl.c_str());
        result.append("\r\n");
    }
    result.append(startMarkup);
    result.append(html.c_str());
    result.append(endMarkup);

#undef MAX_DIGITS
#undef MAKE_NUMBER_FORMAT_1
#undef MAKE_NUMBER_FORMAT_2
#undef NUMBER_FORMAT

    return result;
}

void ClipboardUtil::cfHtmlExtractMetadata(const std::string& cfHtml, std::string* baseUrl, size_t* htmlStart, size_t* fragmentStart, size_t* fragmentEnd)
{
    // Obtain baseUrl if present.
    if (baseUrl) {
        static std::string srcUrlStr("SourceURL:");
        size_t lineStart = cfHtml.find(srcUrlStr);

        if (lineStart != std::string::npos) {
            size_t srcEnd = cfHtml.find("\n", lineStart);
            size_t srcStart = lineStart + srcUrlStr.length();

            if (srcEnd != std::string::npos && srcStart != std::string::npos) {
                *baseUrl = cfHtml.substr(srcStart, srcEnd - srcStart);
                base::TrimWhitespace(*baseUrl, base::TRIM_ALL, baseUrl);
            }
        }
    }

    // Find the markup between "<!--StartFragment-->" and "<!--EndFragment-->".
    // If the comments cannot be found, like copying from OpenOffice Writer,
    // we simply fall back to using StartFragment/EndFragment bytecount values
    // to determine the fragment indexes.
    std::string cfHtmlLower = base::ToLowerASCII(cfHtml);
    size_t markupStart = cfHtmlLower.find("<html", 0);
    if (htmlStart)
        *htmlStart = markupStart;
    
    size_t tagStart = cfHtml.find("<!--StartFragment", markupStart);
    if (tagStart == std::string::npos) {
        static std::string startFragmentStr("StartFragment:");
        size_t startFragmentStart = cfHtml.find(startFragmentStr);

        if (startFragmentStart != std::string::npos)
            *fragmentStart = static_cast<size_t>(atoi(cfHtml.c_str() + startFragmentStart + startFragmentStr.length()));

        static std::string endFragmentStr("EndFragment:");
        size_t endFragmentStart = cfHtml.find(endFragmentStr);

        if (endFragmentStart != std::string::npos)
            *fragmentEnd = static_cast<size_t>(atoi(cfHtml.c_str() + endFragmentStart + endFragmentStr.length()));
        
    } else {
        *fragmentStart = cfHtml.find('>', tagStart) + 1;
        size_t tagEnd = cfHtml.rfind("<!--EndFragment", std::string::npos);
        *fragmentEnd = cfHtml.rfind('<', tagEnd);
    }
}

}