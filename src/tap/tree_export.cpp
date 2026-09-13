// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/tree_export.h>

#include <inspectable.h>
#include <xamlom.h>

#include <cstdio>
#include <mutex>
#include <new>
#include <vector>

#include <tap/log.h>
#include <tap/visual_tree_watcher.h>

namespace styler::tap {
namespace {

// Records the initial flood and nothing else.
//
// The body of OnVisualTreeChange must stay this small. It runs from inside
// XAML's own walk, so anything beyond copying values out happens while XAML
// is mid-traversal. In particular it never releases a handle: releasing from
// inside the callback destroys the element mid-walk - upstream calls that
// "the one thing that isn't safe"
// (vendor/upstream/windows-11-taskbar-styler.wh.cpp:11168, :18379, :18404).
// Every handle collected here is released by ExportTreeToFile, after
// UnadviseVisualTreeChange has returned.
//
// Heap-allocated by ExportTreeToFile, not stack-allocated: if
// UnadviseVisualTreeChange ever fails, XAML may still hold this pointer
// after ExportTreeToFile returns, and freeing it then would leave a virtual
// call through freed memory the next time the tree mutates. See
// ExportTreeToFile for the deliberate-leak path that follows from that.
class SnapshotCallback : public IVisualTreeServiceCallback2 {
   public:
    // Add reports only; this is what BuildForest consumes.
    std::vector<Reported> reported;

    // Every handle the stream handed out and that therefore must reach
    // ReleaseHandle: element.Handle for both Add and Remove, plus
    // relation.Parent for Add. Each report is an independent hand-out -
    // UnregisterInstance closes the runtime object cached for *a handle*,
    // the only reference diagnostics keeps to an element once reported
    // (vendor:10942-10944) - so the same parent handle appearing in many
    // reports still needs releasing once per report, not once per distinct
    // value (vendor:11162-11165, :11169-11171).
    std::vector<unsigned long long> to_release;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown ||
            riid == __uuidof(IVisualTreeServiceCallback) ||
            riid == __uuidof(IVisualTreeServiceCallback2)) {
            *ppv = static_cast<IVisualTreeServiceCallback2*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&ref_));
    }

    // Never self-deletes on reaching zero: ExportTreeToFile owns this
    // object's actual lifetime explicitly (delete on the normal path,
    // deliberate leak if Unadvise fails), now that it is heap-allocated. A
    // COM refcount going to zero here would not mean XAML is done touching
    // it in the way it would for an ordinary reference-counted object.
    ULONG STDMETHODCALLTYPE Release() override {
        return static_cast<ULONG>(InterlockedDecrement(&ref_));
    }

    HRESULT STDMETHODCALLTYPE OnVisualTreeChange(
        ParentChildRelation relation, VisualElement element,
        VisualMutationType mutationType) override {
        try {
            if (mutationType == Add) {
                Reported r;
                r.handle = element.Handle;
                r.parent = relation.Parent;
                r.child_index = relation.ChildIndex;
                r.type = element.Type ? element.Type : L"";
                r.name = element.Name ? element.Name : L"";
                reported.push_back(std::move(r));

                to_release.push_back(element.Handle);
                to_release.push_back(relation.Parent);
            } else {
                // A Remove arriving inside the initial flood still hands out
                // a real handle diagnostics expects released - it is just
                // not one to add to the tree.
                to_release.push_back(element.Handle);
            }
        } catch (...) {
            // Losing one element is a smaller failure than letting a throw
            // cross back into XAML mid-walk.
        }
        return S_OK;  // Never an error: XAML stops reporting after one.
    }

    HRESULT STDMETHODCALLTYPE OnElementStateChanged(
        InstanceHandle, VisualElementState, LPCWSTR) noexcept override {
        return S_OK;
    }

   private:
    LONG ref_ = 1;
};

// Concurrent SetSite calls (tap_boundary.cpp documents the TAP is invoked
// from several explorer UI threads) can each reach ExportTreeToFile with the
// same fixed path. Without this, two overlapping exports would both truncate
// and write it. Guards only the file write below, the same way log.cpp's
// g_file_mutex guards its own file.
std::mutex g_file_mutex;

}  // namespace

HRESULT ExportTreeToFile(const std::wstring& path) {
    // One named local, held across the whole export. AcquireSession()->...
    // written as a single expression would drop the owning temporary at the
    // end of that expression - see visual_tree_watcher.h.
    std::shared_ptr<DiagnosticsSession> session = AcquireSession();
    if (!session) {
        STYLER_LOG(LogLevel::Error,
                   L"ExportTreeToFile called with no diagnostics session");
        return E_NOT_VALID_STATE;
    }

    IVisualTreeService3* service = nullptr;
    HRESULT hr = session->diagnostics()->QueryInterface(
        __uuidof(IVisualTreeService3), reinterpret_cast<void**>(&service));
    if (FAILED(hr) || !service) {
        STYLER_LOG(LogLevel::Error, L"QI IVisualTreeService3 failed 0x%08X",
                   static_cast<unsigned>(hr));
        return FAILED(hr) ? hr : E_NOINTERFACE;
    }

    auto* callback = new (std::nothrow) SnapshotCallback();
    if (!callback) {
        service->Release();
        return E_OUTOFMEMORY;
    }

    HRESULT advise_hr = service->AdviseVisualTreeChange(callback);
    bool leak_callback = false;

    if (SUCCEEDED(advise_hr)) {
        // Unadvise immediately, synchronously, before this function touches
        // `callback` from here on: the initial flood arrives synchronously
        // inside the Advise call above (measured), but the subscription
        // stays live - and `callback` remains callable from a XAML island on
        // another explorer UI thread - until Unadvise returns. Deferring
        // Unadvise into a scope guard that only fires when this function
        // eventually returns would leave that window open across the whole
        // BuildForest/format/write below, racing a concurrent mutation
        // against this thread reading callback->reported.
        HRESULT unadvise_hr = service->UnadviseVisualTreeChange(callback);
        if (FAILED(unadvise_hr)) {
            STYLER_LOG(LogLevel::Error,
                       L"UnadviseVisualTreeChange failed 0x%08X - leaking "
                       L"the snapshot callback, XAML may still call into it",
                       static_cast<unsigned>(unadvise_hr));
            leak_callback = true;
        }
    }
    service->Release();

    // Scope guard covering every exit from here down, including an
    // exception unwinding out of BuildForest/AssignSiblingIndices/the
    // std::wstring appends below and straight into SetSite's catch(...), and
    // including Advise itself having reported some elements before
    // ultimately returning a failure HRESULT. Makes the handle release
    // structural instead of dependent on every return statement remembering
    // it, and - since Unadvise (if it ran at all) already completed above,
    // synchronously, before this guard was even constructed - always fires
    // after Unadvise, outside any callback.
    struct ReleaseOnExit {
        SnapshotCallback* callback;
        bool leak_callback;

        ~ReleaseOnExit() {
            if (leak_callback) {
                // XAML may still hold `callback` and may still be writing
                // into its vectors from another thread - touching either
                // one here would itself be unsafe. Leak the whole object.
                return;
            }
            for (unsigned long long h : callback->to_release) {
                ReleaseHandle(h);
            }
            delete callback;
        }
    } release_on_exit{callback, leak_callback};

    if (FAILED(advise_hr)) {
        STYLER_LOG(LogLevel::Error, L"AdviseVisualTreeChange failed 0x%08X",
                   static_cast<unsigned>(advise_hr));
        return advise_hr;
    }

    STYLER_LOG(LogLevel::Info, L"snapshot: %zu elements",
               callback->reported.size());

    std::wstring out;
    for (TreeNode& root : BuildForest(callback->reported)) {
        AssignSiblingIndices(root);
        out += FormatTree(root);
        out += L'\n';
    }

    if (out.empty()) {
        // Do not write an empty file and call it success. This is reached
        // either by a genuinely empty flood or by a cycle in the reported
        // relations (BuildForest leaves every cycle member unreachable from
        // any root). The initial flood arriving synchronously inside Advise
        // is measured behaviour, not a documented guarantee - this is the
        // line that says so if it ever changes.
        STYLER_LOG(LogLevel::Error,
                   L"snapshot produced no tree (%zu elements reported)",
                   callback->reported.size());
        return E_FAIL;
    }

    FILE* f = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_file_mutex);
        if (_wfopen_s(&f, path.c_str(), L"w, ccs=UTF-8") != 0 || !f) {
            STYLER_LOG(LogLevel::Error, L"cannot write %s", path.c_str());
            return HRESULT_FROM_WIN32(ERROR_CANNOT_MAKE);
        }
        fputws(out.c_str(), f);
        fclose(f);
    }

    STYLER_LOG(LogLevel::Info, L"tree exported to %s", path.c_str());
    return S_OK;
}

}  // namespace styler::tap
