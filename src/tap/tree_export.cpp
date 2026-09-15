// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/tree_export.h>

#include <inspectable.h>
#include <xamlom.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include <unordered_set>
#include <tap/log.h>
#include <tap/thread_init.h>
#include <tap/visual_tree_watcher.h>
#include <tap/winrt_common.h>

namespace styler::tap {
namespace {
enum class BatchPhase { Pending, Running, Completed, Canceled };
struct ReleaseBatch {
    std::shared_ptr<DiagnosticsSession> owner;
    HWND window = nullptr;
    DWORD thread = 0;
    std::vector<InstanceHandle> handles;
    std::atomic<BatchPhase> phase{BatchPhase::Pending};
};

void WINAPI ReleaseOnOwnerThread(void* parameter) {
    auto& batch = *static_cast<ReleaseBatch*>(parameter);
    auto expected = BatchPhase::Pending;
    if (!batch.phase.compare_exchange_strong(expected, BatchPhase::Running)) return;
    struct Done {
        ReleaseBatch& batch;
        ~Done() { batch.phase.store(BatchPhase::Completed); }
    } done{batch};
    std::sort(batch.handles.begin(), batch.handles.end());
    batch.handles.erase(std::unique(batch.handles.begin(), batch.handles.end()), batch.handles.end());
    std::size_t retained = 0;
    for (auto handle : batch.handles) {
        bool released = false;
        try {
            released = ReleaseHandle(batch.owner, handle);
        } catch (...) {
            MarkHandleObservationIncomplete();
        }
        if (!released) batch.handles[retained++] = handle;
    }
    batch.handles.resize(retained);
}

// The callback owns its session and follows normal COM reference counting.
// Active callbacks are counted before Observe or the data mutex: Unadvise
// alone is not treated as a join of callbacks that already entered.
class SnapshotCallback : public IVisualTreeServiceCallback2 {
public:
    explicit SnapshotCallback(std::shared_ptr<DiagnosticsSession> session)
        : owner(std::move(session)), idle(CreateEventW(nullptr, TRUE, TRUE, nullptr)) {}
    ~SnapshotCallback() { if (idle) CloseHandle(idle); }
    std::shared_ptr<DiagnosticsSession> owner;
    std::vector<Reported> reported;
    std::map<DWORD, std::shared_ptr<ReleaseBatch>> batches;
    std::mutex mutex;
    std::atomic<unsigned> active{0};
    HANDLE idle = nullptr;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != __uuidof(IVisualTreeServiceCallback) &&
            iid != __uuidof(IVisualTreeServiceCallback2)) return E_NOINTERFACE;
        *out = static_cast<IVisualTreeServiceCallback2*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto count = --references;
        if (!count) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE OnVisualTreeChange(ParentChildRelation relation,
        VisualElement element, VisualMutationType mutation) override {
        if (active.fetch_add(1) == 0 && idle) ResetEvent(idle);
        // The worker may release its reference as soon as active reaches
        // zero. Keep this object/event alive through the entire epilogue.
        winrt::com_ptr<SnapshotCallback> lifetime;
        lifetime.copy_from(this);
        struct Exit {
            SnapshotCallback& callback;
            ~Exit() {
                if (callback.active.fetch_sub(1) == 1 && callback.idle) SetEvent(callback.idle);
            }
        } exit{*this};
        owner->Observe(element.Handle);
        if (mutation == Add) owner->Observe(relation.Parent);
        try {
            std::lock_guard lock(mutex);
            const DWORD thread = GetCurrentThreadId();
            auto& batch = batches[thread];
            if (!batch) {
                batch = std::make_shared<ReleaseBatch>();
                batch->owner = owner;
                batch->thread = thread;
            }
            if (!batch->window) batch->window = EnsureDispatchWindowForCurrentThread();
            if (!batch->window) MarkHandleObservationIncomplete();
            batch->handles.push_back(element.Handle);
            if (mutation == Add) {
                batch->handles.push_back(relation.Parent);
                reported.push_back({element.Handle, relation.Parent,
                    relation.ChildIndex, element.NumChildren,
                    element.Type ? element.Type : L"", element.Name ? element.Name : L""});
            }
        } catch (...) {
            MarkHandleObservationIncomplete();
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnElementStateChanged(InstanceHandle handle,
        VisualElementState, LPCWSTR) noexcept override {
        // This report has no tree topology, but still exposes a registration.
        // Reuse the non-Add bookkeeping path without fabricating a tree node.
        VisualElement element{};
        element.Handle = handle;
        return OnVisualTreeChange({}, element, Remove);
    }

    bool WaitQuiet() {
        if (!active.load()) return true;
        // Only the export worker waits. UI threads remain pumpable.
        if (!idle || WaitForSingleObject(idle, 5000) != WAIT_OBJECT_0) return false;
        return active.load() == 0;
    }
private:
    std::atomic<ULONG> references{1};
};

struct SnapshotOperation {
    IVisualTreeService3* service = nullptr;
    SnapshotCallback* callback = nullptr;
    bool unadvised = false;
    bool releases_normalized = false;
    ~SnapshotOperation() {
        if (callback) callback->Release();
        if (service) service->Release();
    }
};
// Only the serialized export worker mutates this ownership. A failure keeps
// the entire operation reachable for an explicit retry; DLL detach does not
// run a COM destructor under the loader lock.
auto* const g_pending = new std::unique_ptr<SnapshotOperation>;
std::atomic<bool> g_snapshot_needs_stop{false};
std::mutex g_file_mutex;

HRESULT StopReporting(SnapshotOperation& operation) {
    if (!operation.unadvised) {
        const HRESULT hr = operation.service->UnadviseVisualTreeChange(operation.callback);
        if (FAILED(hr)) {
            STYLER_LOG(LogLevel::Error, L"snapshot Unadvise failed 0x%08X; ownership retained", static_cast<unsigned>(hr));
            return hr;
        }
        operation.unadvised = true;
    }
    return operation.callback->WaitQuiet() ? S_OK : E_PENDING;
}

HRESULT ReleaseSnapshot(SnapshotOperation& operation) {
    auto& batches = operation.callback->batches;
    if (!operation.releases_normalized) {
        // A parent handle may be reported by more than one surface. Dedup
        // across all batches before releasing anything, retaining the first
        // reporting thread as its release destination.
        std::unordered_set<InstanceHandle> observed;
        for (auto& [thread, batch] : batches) {
            (void)thread;
            std::size_t unique = 0;
            for (auto handle : batch->handles) {
                if (observed.insert(handle).second) batch->handles[unique++] = handle;
            }
            batch->handles.resize(unique);
        }
        operation.releases_normalized = true;
    }
    for (auto it = batches.begin(); it != batches.end();) {
        auto batch = it->second;
        auto phase = batch->phase.load();
        if (phase == BatchPhase::Pending && batch->handles.empty()) {
            it = batches.erase(it);
            continue;
        }
        if (phase == BatchPhase::Canceled) {
            // A timed-out dispatch might still arrive. Its old payload stays
            // canceled forever; the retry owns a fresh payload.
            auto retry = std::make_shared<ReleaseBatch>();
            retry->owner = batch->owner;
            retry->window = batch->window;
            retry->thread = batch->thread;
            retry->handles = batch->handles;
            it->second = batch = std::move(retry);
            phase = BatchPhase::Pending;
        }
        if (phase == BatchPhase::Pending) {
            if (!batch->window || GetWindowThreadProcessId(batch->window, nullptr) != batch->thread) {
                MarkHandleObservationIncomplete();
                ++it;
                continue;
            }
            if (!RunOwnedOnWindowThread(batch->window, ReleaseOnOwnerThread, batch)) {
                auto expected = BatchPhase::Pending;
                batch->phase.compare_exchange_strong(expected, BatchPhase::Canceled);
            }
            phase = batch->phase.load();
        }
        if (phase == BatchPhase::Completed) {
            if (batch->handles.empty()) {
                it = batches.erase(it);
                continue;
            }
            batch->phase.store(BatchPhase::Pending);
        }
        ++it;
    }
    return batches.empty() ? S_OK : E_PENDING;
}

HRESULT WriteSnapshot(const SnapshotCallback& callback, const std::wstring& path) {
    const auto incomplete = DescribeIncompleteTree(callback.reported);
    for (const auto& line : incomplete) {
        STYLER_LOG(LogLevel::Error, L"incomplete visual tree: %s", line.c_str());
    }
    // A partial batch cannot replace the last usable selector reference.
    // ExportTreeToFile still releases this snapshot before returning failure.
    if (!incomplete.empty()) return E_FAIL;
    std::wstring output;
    for (TreeNode& root : BuildForest(callback.reported)) {
        AssignSiblingIndices(root);
        output += FormatTree(root);
        output += L'\n';
    }
    if (output.empty()) {
        STYLER_LOG(LogLevel::Error, L"snapshot produced no tree (%zu elements reported)", callback.reported.size());
        return E_FAIL;
    }
    std::lock_guard lock(g_file_mutex);
    const std::wstring temporary = path + L".tmp";
    FILE* file = nullptr;
    if (_wfopen_s(&file, temporary.c_str(), L"w, ccs=UTF-8") != 0 || !file) {
        return HRESULT_FROM_WIN32(ERROR_CANNOT_MAKE);
    }
    const bool wrote = fputws(output.c_str(), file) >= 0;
    const bool closed = fclose(file) == 0;
    if (!wrote || !closed) {
        DeleteFileW(temporary.c_str());
        return HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const auto error = GetLastError();
        DeleteFileW(temporary.c_str());
        return HRESULT_FROM_WIN32(error);
    }
    STYLER_LOG(LogLevel::Info, L"tree exported to %s", path.c_str());
    return S_OK;
}
}  // namespace

bool SnapshotNeedsStop() { return g_snapshot_needs_stop.load(); }

HRESULT StopPendingSnapshot() {
    if (!*g_pending) return S_OK;
    const HRESULT stopped = StopReporting(**g_pending);
    if (FAILED(stopped)) return stopped;
    const HRESULT released = ReleaseSnapshot(**g_pending);
    if (FAILED(released)) return released;
    // Clear ownership before logging: a log allocation failure must never
    // leave a published pointer to an object that cleanup already destroyed.
    g_pending->reset();
    g_snapshot_needs_stop.store(false);
    LogHandleObservation();
    return S_OK;
}

HRESULT ExportTreeToFile(const std::wstring& path) {
    if (const HRESULT previous = StopPendingSnapshot(); FAILED(previous)) return previous;
    const auto owner = AcquireSession();
    if (!owner) return E_NOT_VALID_STATE;
    auto operation = std::make_unique<SnapshotOperation>();
    HRESULT hr = owner->diagnostics()->QueryInterface(__uuidof(IVisualTreeService3),
        reinterpret_cast<void**>(&operation->service));
    if (FAILED(hr) || !operation->service) return FAILED(hr) ? hr : E_NOINTERFACE;
    operation->callback = new SnapshotCallback(owner);
    *g_pending = std::move(operation);
    g_snapshot_needs_stop.store(true);
    const HRESULT advised = (*g_pending)->service->AdviseVisualTreeChange((*g_pending)->callback);
    const HRESULT stopped = StopReporting(**g_pending);
    if (FAILED(stopped)) return stopped;
    hr = advised;
    if (SUCCEEDED(advised)) {
        try {
            STYLER_LOG(LogLevel::Info, L"snapshot: %zu elements", (*g_pending)->callback->reported.size());
            hr = WriteSnapshot(*(*g_pending)->callback, path);
        } catch (...) {
            hr = E_FAIL;
        }
    }
    const HRESULT cleaned = StopPendingSnapshot();
    return FAILED(cleaned) ? cleaned : hr;
}
}  // namespace styler::tap
