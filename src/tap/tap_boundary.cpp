// SPDX-License-Identifier: GPL-3.0-or-later
//
// EVERY function here is callable by the XAML diagnostics layer from inside
// explorer.exe. An escaping exception kills the user's desktop, so each one is
// a catch-all that never propagates and returns S_OK even on failure -
// returning an error makes XAML stop sending events (upstream mirrors this,
// vendor/upstream/windows-11-taskbar-styler.wh.cpp:10604).
//
// Nothing else belongs in this file. "Is the boundary protected?" must be a
// question answered by opening one file.

#include <windows.h>
#include <inspectable.h>
#include <ocidl.h>

#include <new>

#include <tap/clsid.h>
#include <tap/log.h>

namespace styler::tap {

IUnknown* g_site = nullptr;

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

            if (g_site) {
                g_site->Release();
                g_site = nullptr;
            }
            if (!site) {
                return S_OK;
            }

            site->AddRef();
            g_site = site;

            wchar_t host[MAX_PATH]{};
            GetModuleFileNameW(nullptr, host, MAX_PATH);
            STYLER_LOG(LogLevel::Info, L"loaded into %s", host);
        } catch (...) {
            STYLER_LOG(LogLevel::Error, L"SetSite threw");
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetSite(REFIID riid, void** ppv) override {
        try {
            if (!g_site) {
                if (ppv) {
                    *ppv = nullptr;
                }
                return E_FAIL;
            }
            return g_site->QueryInterface(riid, ppv);
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
