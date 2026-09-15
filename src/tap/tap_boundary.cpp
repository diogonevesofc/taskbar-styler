// SPDX-License-Identifier: GPL-3.0-or-later
//
// EVERY function here is callable by the XAML diagnostics layer from inside
// explorer.exe. An escaping exception kills the user's desktop, so every one
// of them is a catch-all that never lets an exception propagate.
//
// That is NOT the same as "always returns S_OK". Only the XAML event
// callbacks (OnVisualTreeChange, OnElementStateChanged - added in later
// tasks) and SetSite additionally never return a failure HRESULT: an error
// from an event callback makes XAML stop sending events, and an error from
// SetSite aborts activation entirely (upstream mirrors this,
// vendor/upstream/windows-11-taskbar-styler.wh.cpp:10604). Everything else
// here - GetSite, QueryInterface, CreateInstance, DllGetClassObject - returns
// a real HRESULT, because the caller uses it to decide what to do (e.g.
// GetSite returns E_FAIL when there is no site: S_OK there would claim
// *ppv is valid when it is not, which breaks the IObjectWithSite contract).
//
// Nothing else belongs in this file. "Is the boundary protected?" must be a
// question answered by opening one file.

#include <windows.h>
#include <inspectable.h>
#include <ocidl.h>

#include <memory>
#include <new>
#include <string>

#include <tap/clsid.h>
#include <tap/log.h>
#include <tap/site.h>
#include <tap/theme_session.h>

namespace styler::tap {

namespace {

class TaskbarStylerTap : public IObjectWithSite {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_IObjectWithSite) {
            *ppv = static_cast<IObjectWithSite*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return InterlockedIncrement(&m_ref);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&m_ref);
        if (r == 0) {
            delete this;
        }
        return r;
    }

    HRESULT STDMETHODCALLTYPE SetSite(IUnknown* site) override {
        try {
            STYLER_LOG(LogLevel::Info, L"SetSite(%p)", site);
            std::shared_ptr<IUnknown> owned;
            if (site) {
                site->AddRef();
                owned = std::shared_ptr<IUnknown>(site, [](IUnknown* value) { value->Release(); });
            }
            RequestSessionChange(std::move(owned));
        } catch (...) {
            STYLER_LOG(LogLevel::Error, L"SetSite threw");
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSite(REFIID riid, void** ppv) override {
        try {
            auto site = AcquireSite();
            if (!site) {
                if (ppv) {
                    *ppv = nullptr;
                }
                return E_FAIL;
            }
            return site->QueryInterface(riid, ppv);
        } catch (...) {
            STYLER_LOG(LogLevel::Error, L"GetSite threw");
            return E_FAIL;
        }
    }

private:
    LONG m_ref = 1;
};

class TapFactory : public IClassFactory {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return InterlockedIncrement(&m_ref);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&m_ref);
        if (r == 0) {
            delete this;
        }
        return r;
    }

    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID riid,
                                             void** ppv) override {
        try {
            if (outer) {
                return CLASS_E_NOAGGREGATION;
            }
            auto* tap = new (std::nothrow) TaskbarStylerTap();
            if (!tap) {
                return E_OUTOFMEMORY;
            }
            HRESULT hr = tap->QueryInterface(riid, ppv);
            tap->Release();
            return hr;
        } catch (...) {
            STYLER_LOG(LogLevel::Error, L"CreateInstance threw");
            return E_FAIL;
        }
    }

    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override { return S_OK; }

private:
    LONG m_ref = 1;
};

}  // namespace



}  // namespace styler::tap

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    try {
        if (rclsid != styler::tap::CLSID_TaskbarStylerTap) {
            return CLASS_E_CLASSNOTAVAILABLE;
        }
        auto* factory = new (std::nothrow) styler::tap::TapFactory();
        if (!factory) {
            return E_OUTOFMEMORY;
        }
        HRESULT hr = factory->QueryInterface(riid, ppv);
        factory->Release();
        return hr;
    } catch (...) {
        return E_FAIL;
    }
}

STDAPI DllCanUnloadNow() {
    // Deliberately never unloadable. Tearing a COM DLL out of a live process
    // with XAML callbacks possibly in flight is fragile; spec section 6.4 says
    // so plainly, and the CLI's `unload` explains it instead of faking it.
    return S_FALSE;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
    }
    return TRUE;
}
