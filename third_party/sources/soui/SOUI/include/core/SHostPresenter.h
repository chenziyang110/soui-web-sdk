#pragma once
#include <core/SHostWnd.h>
#include <interface/SHostPresenter-i.h>
namespace SOUI
{
    class SHostWnd;
    class SOUI_EXP SHostPresenter : public TObjRefImpl<IHostPresenter>
    {
    protected:
        SHostWnd *m_pHostWnd;
    public:
        SHostPresenter(SHostWnd *pHostWnd);
        ~SHostPresenter(void);
    public:
        STDMETHOD_(void, OnHostCreate)(THIS) override;
        STDMETHOD_(void, OnHostDestroy)(THIS) override;
        STDMETHOD_(void, OnHostResize)(THIS_ SIZE szHost) override;
        STDMETHOD_(void, OnHostPresent)(THIS_ HDC hdc, IRenderTarget *pMemRT, LPCRECT rcDirty, BYTE byAlpha) override;
        STDMETHOD_(void, OnHostAlpha)(THIS_ BYTE byAlpha) override;
    protected:
        void UpdateLayerFromRenderTarget(IRenderTarget *pRT, BYTE byAlpha, LPCRECT prcDirty = NULL);
    };

}
