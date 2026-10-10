// Exact method bytes from third_party/sources/miniblink/content/browser/WebPage.cpp
WebPage::WebPage(void* foreignPtr)
{
    m_pageImpl = nullptr;
    m_pageNetExtraData = adoptRef(new net::PageNetExtraData());
#if (defined ENABLE_WKE) && (ENABLE_WKE == 1)
    m_wkeWebView = nullptr;
    m_wkeClientHandler = nullptr;
    std::unique_ptr<wke::CWebViewHandler> handler(new wke::CWebViewHandler());
    m_wkeHandler = handler.get();
    memset(m_wkeHandler, 0, sizeof(wke::CWebViewHandler));
#endif
    m_isContextMenuEnable = true;

    if (!m_webPageSet)
        m_webPageSet = new WTF::HashSet<WebPage*>();
    m_webPageSet->add(this);
#if (defined ENABLE_WKE) && (ENABLE_WKE == 1)
    handler.release();
#endif
}

// Exact method bytes from third_party/sources/miniblink/content/browser/WebPage.cpp
WebPage::~WebPage()
{
#if (defined ENABLE_WKE) && (ENABLE_WKE == 1)
    delete m_wkeHandler;
    m_wkeHandler = nullptr;
#endif
    if (m_pageImpl) {
        delete m_pageImpl;
        m_pageImpl = 0;
    }
    
    m_webPageSet->remove(this);
}
