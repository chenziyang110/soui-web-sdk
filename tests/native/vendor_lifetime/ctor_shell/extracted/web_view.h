// Exact method bytes from third_party/sources/miniblink/wke/wkeWebView.cpp
CWebView::CWebView(COLORREF color)
    : m_hWnd(NULL)
    , m_transparent(false)
    , m_width(0)
    , m_height(0)
    , m_webPage(nullptr)
    , m_zoomFactor(1)
    , m_awake(true)
    , m_title("", 0)
    , m_cookie("", 0)
    , m_name("miniblink", 0)
    , m_url("", 0)
    , m_isCokieEnabled(true)
    , m_isCreatedDevTools(false)
{
    if (!brokerNoteViewCreated())
        throw std::logic_error("Raw view construction is unavailable in cursor broker mode");
    net::ActivatingObjCheck* const activating = net::ActivatingObjCheck::inst();
    m_id = activating->genId();
    activating->add(m_id);

    try {
        // A complete, uninitialized shell has not published engine resources.
        m_webPage = new content::WebPage(nullptr);
    } catch (...) {
        activating->remove(m_id);
        throw;
    }

    try {
        // Engine construction has no proven partial abort; never return FAILED.
        _initPage(color);
        _initHandler();
        _initMemoryDC();

        m_settings.size = 0;
        m_webPage->wkeHandler().isWke = true;

        g_liveWebViews.insert(this);
        diagnostics::view_created();
    } catch (...) {
        std::terminate();
    }
}

// Exact method bytes from third_party/sources/miniblink/wke/wkeWebView.cpp
CWebView::~CWebView()
{
    net::ActivatingObjCheck::inst()->remove(m_id);
    g_liveWebViews.erase(this);
    m_webPage->close();
    diagnostics::view_destroyed();
}

// Exact method bytes from third_party/sources/miniblink/wke/wkeWebView.cpp
CWebView* CWebView::createForCursorBroker(COLORREF color)
{
    if (!brokerOwnedFactoryPermission())
        return nullptr;
    return new CWebView(color);
}

// Exact method bytes from third_party/sources/miniblink/wke/wkeWebView.cpp
void CWebView::_initPage(COLORREF color)
{
    m_webPage->initWkeWebView(this);
    m_webPage->init(nullptr, color);

//     blink::Page::PageClients pageClients;
//     pageClients.chromeClient = new ChromeClient(this);
//     pageClients.contextMenuClient = new ContextMenuClient;
//     pageClients.inspectorClient = new InspectorClient;
//     pageClients.editorClient = new EditorClient;
//     pageClients.dragClient = new DragClient;
// 
//     m_page = adoptPtr(new blink::Page(pageClients));
//     blink::Settings* settings = m_page->settings();
//     settings->setMinimumFontSize(0);
//     settings->setMinimumLogicalFontSize(9);
//     settings->setDefaultFontSize(16);
//     settings->setDefaultFixedFontSize(13);
//     settings->setJavaScriptEnabled(true);
//     settings->setPluginsEnabled(true);
//     settings->setLoadsImagesAutomatically(true);
//     settings->setDefaultTextEncodingName(icuwin_getDefaultEncoding());
//     
//     settings->setStandardFontFamily("Times New Roman");
//     settings->setFixedFontFamily("Courier New");
//     settings->setSerifFontFamily("Times New Roman");
//     settings->setSansSerifFontFamily("Arial");
//     settings->setCursiveFontFamily("Comic Sans MS");
//     settings->setFantasyFontFamily("Times New Roman");
//     settings->setPictographFontFamily("Times New Roman");
// 
//     settings->setAllowUniversalAccessFromFileURLs(true);
//     settings->setAllowFileAccessFromFileURLs(true);
//     settings->setJavaScriptCanAccessClipboard(true);
//     settings->setShouldPrintBackgrounds(true);
//     settings->setTextAreasAreResizable(true);
//     settings->setLocalStorageEnabled(true);
// 
//     UChar dir[256];
//     GetCurrentDirectory(256, dir);
//     wcscat(dir, L"\\localStorage");
//     settings->setLocalStorageDatabasePath(dir);
//     blink::DatabaseTracker::tracker().setDatabaseDirectoryPath(dir);
// 
//     FrameLoaderClient* loader = new FrameLoaderClient(this, m_page.get());
//     m_mainFrame = blink::Frame::create(m_page.get(), NULL, loader).get();
//     loader->setFrame(m_mainFrame);
//     m_mainFrame->init();
// 
//     page()->focusController()->setActive(true);
}

// Exact method bytes from third_party/sources/miniblink/wke/wkeWebView.cpp
void CWebView::_initHandler()
{
    m_webPage->wkeHandler().alertBoxCallback = defaultRunAlertBox;
    m_webPage->wkeHandler().confirmBoxCallback = defaultRunConfirmBox;
    m_webPage->wkeHandler().promptBoxCallback = defaultRunPromptBox;
}

// Exact method bytes from third_party/sources/miniblink/wke/wkeWebView.cpp
void CWebView::_initMemoryDC()
{
    //m_hdc = adoptPtr(::CreateCompatibleDC(0));
}
