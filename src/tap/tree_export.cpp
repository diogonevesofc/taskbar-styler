// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/tree_export.h>

#include <inspectable.h>
#include <xamlom.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <vector>

#include <tap/log.h>
#include <tap/visual_tree_watcher.h>

namespace styler::tap {
namespace {

// Bounds the recursion in Materialise. A cycle in the reported relations
// would otherwise recurse until the stack dies, inside explorer. A real XAML
// tree is nowhere near this deep.
constexpr int kMaxDepth = 128;

// One element as the mutation stream reported it. The BSTRs are copied out
// immediately - they belong to the caller, not to us.
struct Reported {
    InstanceHandle handle = 0;
    InstanceHandle parent = 0;
    unsigned int child_index = 0;
    std::wstring type;
    std::wstring name;
};

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
class SnapshotCallback : public IVisualTreeServiceCallback2 {
   public:
    std::vector<Reported> reported;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown ||
            riid == __uuidof(IVisualTreeServiceCallback) ||
            riid == __uuidof(IVisualTreeServiceCallback2)) {
            *ppv = static_cast<IVisualTreeServiceCallback2*>(this);
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    // Lives on the stack for exactly one Advise/Unadvise pair, so the
    // reference count is not what keeps it alive. Never free on zero.
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

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
};

TreeNode Materialise(size_t i, const std::vector<Reported>& reported,
                     const std::vector<std::vector<size_t>>& kids, int depth) {
    TreeNode node;
    node.type = reported[i].type;
    node.name = reported[i].name;
    if (depth >= kMaxDepth) {
        STYLER_LOG(LogLevel::Error, L"tree depth cap %d hit at %s", kMaxDepth,
                   node.type.c_str());
        return node;
    }
    for (size_t k : kids[i]) {
        node.children.push_back(Materialise(k, reported, kids, depth + 1));
    }
    return node;
}

// Builds the forest from the reported parent/child pairs. An element whose
// parent handle was never itself reported is a root - the stream reports the
// taskbar's hosts without reporting whatever contains them.
std::vector<TreeNode> BuildForest(const std::vector<Reported>& reported) {
    std::map<InstanceHandle, size_t> index_of;
    for (size_t i = 0; i < reported.size(); ++i) {
        // First report of a handle wins: a handle reported twice would
        // otherwise leave the first copy's children pointing at a stale node.
        index_of.emplace(reported[i].handle, i);
    }

    std::vector<std::vector<size_t>> kids(reported.size());
    std::vector<size_t> roots;
    for (size_t i = 0; i < reported.size(); ++i) {
        if (index_of[reported[i].handle] != i) {
            continue;  // Duplicate report of an earlier handle.
        }
        auto parent = index_of.find(reported[i].parent);
        if (reported[i].parent == 0 || parent == index_of.end()) {
            roots.push_back(i);
        } else {
            kids[parent->second].push_back(i);
        }
    }

    for (std::vector<size_t>& k : kids) {
        std::stable_sort(k.begin(), k.end(), [&](size_t a, size_t b) {
            return reported[a].child_index < reported[b].child_index;
        });
    }

    std::vector<TreeNode> forest;
    for (size_t r : roots) {
        forest.push_back(Materialise(r, reported, kids, 0));
    }
    return forest;
}

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
        STYLER_LOG(LogLevel::Error, L"QI IVisualTreeService3 failed 0x%08X", hr);
        return FAILED(hr) ? hr : E_NOINTERFACE;
    }

    SnapshotCallback callback;
    hr = service->AdviseVisualTreeChange(&callback);
    if (SUCCEEDED(hr)) {
        // Whatever arrived, arrived inside the call above. Unadvise before
        // formatting so nothing is reported into `callback` while we read it.
        service->UnadviseVisualTreeChange(&callback);
    }
    service->Release();

    if (FAILED(hr)) {
        STYLER_LOG(LogLevel::Error, L"AdviseVisualTreeChange failed 0x%08X", hr);
        return hr;
    }

    if (callback.reported.empty()) {
        // Do not write an empty file and call it success. The initial flood
        // arriving inside the Advise call is measured behaviour, not a
        // documented guarantee; this is the line that says so if it changes.
        STYLER_LOG(LogLevel::Error,
                   L"AdviseVisualTreeChange reported no elements");
        return E_FAIL;
    }
    STYLER_LOG(LogLevel::Info, L"snapshot: %zu elements",
               callback.reported.size());

    std::wstring out;
    for (TreeNode& root : BuildForest(callback.reported)) {
        AssignSiblingIndices(root);
        out += FormatTree(root);
        out += L'\n';
    }

    // Every handle the stream reported is ours to release, and here is the
    // safe place: no callback is running and we are not inside XAML's walk.
    for (const Reported& r : callback.reported) {
        ReleaseHandle(r.handle);
    }

    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"w, ccs=UTF-8") != 0 || !f) {
        STYLER_LOG(LogLevel::Error, L"cannot write %s", path.c_str());
        return HRESULT_FROM_WIN32(ERROR_CANNOT_MAKE);
    }
    fputws(out.c_str(), f);
    fclose(f);

    STYLER_LOG(LogLevel::Info, L"tree exported to %s", path.c_str());
    return S_OK;
}

}  // namespace styler::tap
