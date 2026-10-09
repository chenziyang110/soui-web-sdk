#ifndef content_ui_WinCursorResources_h
#define content_ui_WinCursorResources_h

#include <windows.h>
#include <stdio.h>
#include "content/ui/CursorResources.h"

namespace content {
struct WinCursorApi {
    typedef HBITMAP Bitmap;
    typedef HDC DC;
    typedef HCURSOR Cursor;
    typedef HWND Window;
    typedef DWORD Thread;

    Thread currentThread() const { return ::GetCurrentThreadId(); }
    Cursor localCursor() const { return ::GetCursor(); }
    bool globalCursor(Cursor& cursor) const
    {
        CURSORINFO info = { sizeof(CURSORINFO) };
        if (!::GetCursorInfo(&info)) return false;
        cursor = info.hCursor;
        return true;
    }
    bool canSelect(Window window) const
    {
        if (!window || ::GetWindowThreadProcessId(window, 0) != currentThread()) return false;
        HWND capture = ::GetCapture();
        if (capture) return capture == window || !!::IsChild(window, capture);
        CURSORINFO info = { sizeof(CURSORINFO) };
        if (!::GetCursorInfo(&info)) return false;
        HWND underCursor = ::WindowFromPoint(info.ptScreenPos);
        if (underCursor != window && !::IsChild(window, underCursor)) return false;
        POINT point = info.ptScreenPos;
        RECT client;
        return !!::ScreenToClient(window, &point) && !!::GetClientRect(window, &client) && !!::PtInRect(&client, point);
    }
    Cursor sharedArrow() const { return ::LoadCursor(0, IDC_ARROW); }
    void setCursor(Cursor cursor) const { ::SetCursor(cursor); }
    bool destroyCursor(Cursor cursor) const { return !!::DestroyIcon(reinterpret_cast<HICON>(cursor)); }
    DC createMemoryDC(DC screen) const { return ::CreateCompatibleDC(screen); }
    Bitmap createMask(int width, int height, const unsigned char* bits) const { return ::CreateBitmap(width, height, 1, 1, bits); }
    Bitmap createColor(DC screen, int width, int height) const { return ::CreateCompatibleBitmap(screen, width, height); }
    Bitmap selectBitmap(DC dc, Bitmap bitmap) const
    {
        HGDIOBJ old = ::SelectObject(dc, bitmap);
        return !old || old == HGDI_ERROR ? 0 : reinterpret_cast<Bitmap>(old);
    }
    bool bitmapSize(Bitmap bitmap, int& width, int& height) const
    {
        BITMAP info;
        if (::GetObject(bitmap, sizeof(info), &info) != sizeof(info)) return false;
        width = info.bmWidth;
        height = info.bmHeight;
        return width > 0 && height > 0;
    }
    bool setTransparent(DC dc) const { return !!::SetBkMode(dc, TRANSPARENT); }
    bool prepareMono(DC working, DC andMask, DC xorMask, int width, int height) const
    {
        if (::SetBkColor(working, RGB(0, 0, 0)) == CLR_INVALID) return false;
        if (!::BitBlt(andMask, 0, 0, width, height, working, 0, 0, SRCCOPY)) return false;
        if (::SetBkColor(xorMask, RGB(255, 255, 255)) == CLR_INVALID) return false;
        if (::SetTextColor(xorMask, RGB(255, 255, 255)) == CLR_INVALID) return false;
        return !!::BitBlt(xorMask, 0, 0, width, height, andMask, 0, 0, SRCCOPY)
            && !!::BitBlt(xorMask, 0, 0, width, height, working, 0, 0, SRCAND);
    }
    Cursor createCursor(Bitmap mask, Bitmap color, unsigned x, unsigned y) const
    {
        ICONINFO icon = { 0 };
        icon.fIcon = FALSE;
        icon.xHotspot = x;
        icon.yHotspot = y;
        icon.hbmMask = mask;
        icon.hbmColor = color;
        return reinterpret_cast<Cursor>(::CreateIconIndirect(&icon));
    }
    bool deleteDC(DC dc) const { return !!::DeleteDC(dc); }
    bool deleteBitmap(Bitmap bitmap) const { return !!::DeleteObject(bitmap); }
    template <typename Handle> void report(const char* operation, Handle handle) const
    {
        char text[256];
        _snprintf_s(text, sizeof(text), _TRUNCATE, "Miniblink owned cursor: %s (handle=%p)\n", operation, reinterpret_cast<void*>(handle));
        ::OutputDebugStringA(text);
    }
};
} // namespace content
#endif
