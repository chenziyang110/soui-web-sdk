#ifndef content_ui_CursorResources_h
#define content_ui_CursorResources_h

#include <stddef.h>
#include <string.h>

// Private, injected Win32 resource policy. No window or pixel implementation lives here.
namespace content {

struct AlphaCursorMaskLayout {
    size_t rowBytes, dataBytes, bytes;
};

inline bool alphaCursorMaskLayout(size_t width, size_t height, AlphaCursorMaskLayout& layout)
{
    if (!width || !height) return false;
    // CreateBitmap's 1bpp scan lines require WORD alignment. Round without width + 15 overflow.
    const size_t words = width / 16 + (width % 16 ? 1 : 0);
    if (words > static_cast<size_t>(-1) / 2) return false;
    const size_t rowBytes = words * 2;
    if (height > static_cast<size_t>(-1) / rowBytes) return false;
    layout.rowBytes = rowBytes;
    layout.dataBytes = width / 8 + (width % 8 ? 1 : 0);
    layout.bytes = rowBytes * height;
    return true;
}

inline bool initializeAlphaCursorMask(unsigned char* bits, size_t capacity, size_t width, size_t height)
{
    AlphaCursorMaskLayout layout;
    if (!bits || !alphaCursorMaskLayout(width, height, layout) || capacity < layout.bytes) return false;
    memset(bits, 0, layout.bytes);
    for (size_t row = 0; row < height; ++row)
        memset(bits + row * layout.rowBytes, 0xff, layout.dataBytes);
    return true;
}

template <typename Api> struct CreatedCursor {
    typename Api::Cursor cursor;
    bool cleanupComplete;
    CreatedCursor(typename Api::Cursor value = 0, bool complete = true)
        : cursor(value), cleanupComplete(complete) { }
};

template <typename Api> class CursorAssembly {
public:
    typedef typename Api::Bitmap Bitmap;
    typedef typename Api::DC DC;
    typedef typename Api::Cursor Cursor;

    CursorAssembly(Api& api, DC screen, DC surface, Bitmap borrowed)
        : m_api(api), m_screen(screen), m_surface(surface), m_borrowed(borrowed)
        , m_placeholder(0), m_surfaceOld(0), m_surfaceDetached(false), m_ok(true), m_used(false)
    {
        for (int i = 0; i < 3; ++i) { m_dcs[i] = 0; m_old[i] = 0; }
        for (int i = 0; i < 3; ++i) m_bitmaps[i] = 0;
    }

    CreatedCursor<Api> create(bool alpha, int width, int height,
        unsigned hotspotX, unsigned hotspotY, const unsigned char* alphaMask)
    {
        if (m_used) {
            m_api.report("cursor assembly is single use", m_borrowed);
            return CreatedCursor<Api>(0, false);
        }
        m_used = true;
        Cursor cursor = 0;
        if (!assemble(alpha, width, height, hotspotX, hotspotY, alphaMask, cursor))
            m_api.report("cursor assembly failed", m_borrowed);
        cleanup();
        return CreatedCursor<Api>(cursor, m_ok);
    }

private:
    CursorAssembly(const CursorAssembly&);
    CursorAssembly& operator=(const CursorAssembly&);

    bool select(int index, Bitmap bitmap)
    {
        m_old[index] = m_api.selectBitmap(m_dcs[index], bitmap);
        return !!m_old[index];
    }

    bool restoreSelections()
    {
        bool restored = true;
        for (int i = 0; i < 3; ++i) {
            if (!m_old[i]) continue;
            if (!m_api.selectBitmap(m_dcs[i], m_old[i])) {
                m_ok = false;
                restored = false;
                m_api.report("cursor DC restore failed", m_dcs[i]);
            } else m_old[i] = 0;
        }
        return restored;
    }

    bool assemble(bool alpha, int width, int height,
        unsigned x, unsigned y, const unsigned char* maskBits, Cursor& cursor)
    {
        if (!m_screen || !m_surface || !m_borrowed || width <= 0 || height <= 0) return false;
        m_dcs[0] = m_api.createMemoryDC(m_screen);
        if (!m_dcs[0]) return false;
        m_placeholder = m_api.createMask(1, 1, 0);
        if (!m_placeholder) return false;
        m_surfaceOld = m_api.selectBitmap(m_surface, m_placeholder);
        if (!m_surfaceOld) return false;
        m_surfaceDetached = true;
        if (m_surfaceOld != m_borrowed) return false;

        if (alpha) {
            if (!select(0, m_borrowed) || !m_api.setTransparent(m_dcs[0])) return false;
            if (!restoreSelections()) return false;
            m_bitmaps[0] = m_api.createMask(width, height, maskBits);
            if (!m_bitmaps[0]) return false;
            cursor = m_api.createCursor(m_bitmaps[0], m_borrowed, x, y);
        } else {
            int bitmapWidth = 0, bitmapHeight = 0;
            m_dcs[1] = m_api.createMemoryDC(m_screen);
            m_dcs[2] = m_api.createMemoryDC(m_screen);
            if (!m_dcs[1] || !m_dcs[2] || !m_api.bitmapSize(m_borrowed, bitmapWidth, bitmapHeight)) return false;
            m_bitmaps[1] = m_api.createMask(bitmapWidth, bitmapHeight, 0);
            m_bitmaps[2] = m_api.createColor(m_screen, bitmapWidth, bitmapHeight);
            if (!m_bitmaps[1] || !m_bitmaps[2]) return false;
            if (!select(0, m_borrowed) || !select(1, m_bitmaps[1]) || !select(2, m_bitmaps[2])) return false;
            if (!m_api.prepareMono(m_dcs[0], m_dcs[1], m_dcs[2], bitmapWidth, bitmapHeight)) return false;
            if (!restoreSelections()) return false;
            cursor = m_api.createCursor(m_bitmaps[1], m_bitmaps[2], x, y);
        }
        return !!cursor;
    }

    void cleanup()
    {
        restoreSelections();
        bool allDCsReleased = true;
        for (int i = 0; i < 3; ++i) {
            if (!m_dcs[i]) continue;
            if (m_api.deleteDC(m_dcs[i])) m_dcs[i] = 0;
            else {
                allDCsReleased = false;
                m_ok = false;
                m_api.report("cursor DC release failed; bitmaps retained", m_dcs[i]);
            }
        }
        bool surfaceRestored = !m_surfaceDetached;
        // Never reselect the borrowed bitmap while an unreleased working DC may hold it.
        if (m_surfaceDetached && !m_dcs[0]) {
            surfaceRestored = !!m_api.selectBitmap(m_surface, m_surfaceOld);
            if (!surfaceRestored) {
                m_ok = false;
                m_api.report("cursor borrowed surface restore failed", m_surface);
            }
        } else if (m_surfaceDetached) m_ok = false;
        for (int i = 0; i < 3; ++i) {
            if (!m_bitmaps[i]) continue;
            if (!allDCsReleased || !m_api.deleteBitmap(m_bitmaps[i])) {
                m_ok = false;
                m_api.report("cursor bitmap release incomplete", m_bitmaps[i]);
            }
        }
        if (m_placeholder && (!surfaceRestored || !m_api.deleteBitmap(m_placeholder))) {
            m_ok = false;
            m_api.report("cursor placeholder release incomplete", m_placeholder);
        }
        // m_borrowed and m_surface belong to PlatformBitmap, including failure exits.
    }

    Api& m_api;
    DC m_screen, m_surface;
    Bitmap m_borrowed, m_placeholder, m_surfaceOld;
    DC m_dcs[3];
    Bitmap m_old[3], m_bitmaps[3];
    bool m_surfaceDetached, m_ok, m_used;
};

template <typename Api> class OwnedCustomCursor {
public:
    typedef typename Api::Cursor Cursor;
    typedef typename Api::Window Window;
    explicit OwnedCustomCursor(Api api = Api())
        : m_api(api), m_thread(api.currentThread()), m_window(0), m_retiredWindow(0), m_current(0), m_retired(0)
        , m_faulted(false), m_closed(false) { }
    ~OwnedCustomCursor() { if (!m_closed) dispose(); }

    template <typename Creator> bool update(Window window, Creator creator)
    {
        if (m_closed || m_faulted || m_retired || !onOwnerThread()) return false;
        if (!m_api.canSelect(window)) {
            m_api.report("cursor install outside owner window rejected", m_current);
            return false;
        }
        CreatedCursor<Api> created = creator();
        if (!created.cleanupComplete) {
            m_faulted = true;
            m_retired = created.cursor;
            m_retiredWindow = window;
            release(m_retired, m_retiredWindow);
            m_api.report("cursor creation cleanup incomplete; further allocation blocked", created.cursor);
            return false;
        }
        if (!created.cursor) return false;
        m_api.setCursor(created.cursor); // NULL previous handle is valid.
        Cursor global = 0;
        if (m_api.localCursor() != created.cursor || !m_api.globalCursor(global) || global != created.cursor) {
            m_faulted = true;
            m_retired = created.cursor;
            m_retiredWindow = window;
            m_api.report("cursor selection not confirmed; candidate retained", created.cursor);
            return false;
        }
        m_retired = m_current;
        m_retiredWindow = m_window;
        m_window = window;
        m_current = created.cursor;
        release(m_retired, m_retiredWindow);
        return true;
    }

    bool dispose()
    {
        if (m_closed) return !m_current && !m_retired && !m_faulted;
        m_closed = true;
        if (!onOwnerThread()) return false;
        const bool retiredReleased = release(m_retired, m_retiredWindow);
        const bool currentReleased = release(m_current, m_window);
        return retiredReleased && currentReleased && !m_faulted;
    }
    Cursor current() const { return m_current; }
    Cursor retired() const { return m_retired; }

private:
    OwnedCustomCursor(const OwnedCustomCursor&);
    OwnedCustomCursor& operator=(const OwnedCustomCursor&);
    bool onOwnerThread()
    {
        if (m_api.currentThread() == m_thread) return true;
        m_api.report("cursor operation rejected on foreign thread", m_current);
        return false;
    }
    bool release(Cursor& cursor, Window window)
    {
        if (!cursor) return true;
        Cursor global = 0;
        if (!m_api.globalCursor(global)) {
            m_api.report("cursor global state unknown; owned handle retained", cursor);
            return false;
        }
        Cursor local = m_api.localCursor();
        if (local == cursor || global == cursor) {
            if (local != cursor || global != cursor || !m_api.canSelect(window)) {
                m_api.report("cursor still in use outside owner window; handle retained", cursor);
                return false;
            }
            Cursor fallback = m_api.sharedArrow();
            if (!fallback) {
                m_api.report("cursor shared fallback unavailable; owned handle retained", cursor);
                return false;
            }
            m_api.setCursor(fallback);
            if (m_api.localCursor() == cursor || !m_api.globalCursor(global) || global == cursor) {
                m_api.report("cursor detach not confirmed; owned handle retained", cursor);
                return false;
            }
        }
        if (!m_api.destroyCursor(cursor)) {
            m_api.report("cursor release failed; owned handle retained", cursor);
            return false;
        }
        cursor = 0;
        return true;
    }
    Api m_api;
    typename Api::Thread m_thread;
    Window m_window, m_retiredWindow;
    Cursor m_current, m_retired;
    bool m_faulted, m_closed;
};

} // namespace content
#endif
