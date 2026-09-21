#pragma once

namespace SOUI {

#undef INTERFACE
#define INTERFACE IHostPresenter
    DECLARE_INTERFACE_(IHostPresenter, IObjRef)
    {
        virtual long AddRef() PURE;

        virtual long Release() PURE;

        virtual void OnFinalRelease() PURE;

        //////////////////////////////////////////////////////////////////////////

        STDMETHOD_(void, OnHostCreate)(THIS) PURE;
        STDMETHOD_(void, OnHostDestroy)(THIS) PURE;
        STDMETHOD_(void, OnHostResize)(THIS_ SIZE szHost) PURE;
        STDMETHOD_(void, OnHostPresent)(THIS_ HDC hdc, IRenderTarget *pMemRT, LPCRECT rcDirty, BYTE byAlpha) PURE;
        STDMETHOD_(void, OnHostAlpha)(THIS_ BYTE byAlpha) PURE;
    };

}