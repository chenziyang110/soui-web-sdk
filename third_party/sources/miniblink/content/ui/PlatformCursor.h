
#ifndef content_browser_PlatformCursor_h
#define content_browser_PlatformCursor_h

#include <windows.h>
#include "third_party/WebKit/Source/platform/win/HWndDC.h"
#include "skia/ext/platform_canvas.h"
#include "base/WindowsVersion.h"
#include "content/ui/WinCursorResources.h"

namespace content {

BITMAPINFO createBitmapInfoForSize(int width, int height, int bitCount)
{
    BITMAPINFO bitmapInfo;
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = bitCount;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    return bitmapInfo;
}

CreatedCursor<WinCursorApi> createOwnedCursorImpl(const blink::WebCursorInfo& cursorInfo)
{
    const SkBitmap& img = cursorInfo.customImage.getSkBitmap();
    const blink::IntPoint& hotSpot = cursorInfo.hotSpot;
    IntPoint effectiveHotSpot = hotSpot; // determineHotSpot(img, hotSpot);
    static bool doAlpha = base::getWindowsVersion(nullptr, nullptr) > base::WindowsVista;
    if (img.width() <= 0 || img.height() <= 0)
        return CreatedCursor<WinCursorApi>();
    HWndDC dc(0);
    skia::PlatformBitmap platformBitmap;
    if (!platformBitmap.Allocate(img.width(), img.height(), false)) {
        ::OutputDebugStringA("Miniblink owned cursor: PlatformBitmap allocation failed\n");
        return CreatedCursor<WinCursorApi>();
    }
    const SkBitmap& tmpSkBitmap = platformBitmap.GetBitmap();
    SkCanvas canvas(tmpSkBitmap);
    canvas.drawBitmap(img, 0, 0);
    HDC surface = platformBitmap.GetSurface();
    HBITMAP borrowed = surface ? reinterpret_cast<HBITMAP>(::GetCurrentObject(surface, OBJ_BITMAP)) : nullptr;
    Vector<unsigned char, 128> maskBits;
    if (doAlpha) {
        AlphaCursorMaskLayout layout;
        if (!alphaCursorMaskLayout(static_cast<size_t>(img.width()), static_cast<size_t>(img.height()), layout))
            return CreatedCursor<WinCursorApi>();
        maskBits.fill(0, layout.bytes);
        if (!initializeAlphaCursorMask(maskBits.data(), maskBits.size(), img.width(), img.height()))
            return CreatedCursor<WinCursorApi>();
    }
    WinCursorApi api;
    CursorAssembly<WinCursorApi> assembly(api, dc, surface, borrowed);
    return assembly.create(doAlpha, img.width(), img.height(),
        effectiveHotSpot.x(), effectiveHotSpot.y(), doAlpha ? maskBits.data() : nullptr);
}

}

#endif // content_browser_PlatformCursor_h
