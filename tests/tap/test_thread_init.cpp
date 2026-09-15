// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <future>
#include <stdexcept>
#include <thread>
#include <utility>

#include <tap/thread_init.h>

namespace {

// A real Win32 message loop: these tests exercise the synchronous hook/send
// contract without loading XAML or touching the user's Explorer process.
class HostThread {
public:
    HostThread() {
        std::promise<std::pair<DWORD, HWND>> ready;
        auto result = ready.get_future();
        thread_ = std::thread([ready = std::move(ready)]() mutable {
            HWND host = CreateWindowExW(0, L"STATIC", L"TAP dispatch test", 0,
                                        0, 0, 0, 0, nullptr, nullptr,
                                        GetModuleHandleW(nullptr), nullptr);
            MSG message{};
            PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
            ready.set_value({GetCurrentThreadId(), host});
            while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (IsWindow(host)) {
                DestroyWindow(host);
            }
            // Deliberately leave the dispatch window to OS thread cleanup.
        });
        auto [id, host] = result.get();
        id_ = id;
        host_ = host;
    }

    ~HostThread() {
        PostThreadMessageW(id_, WM_QUIT, 0, 0);
        thread_.join();
    }

    HWND host() const { return host_; }
    DWORD id() const { return id_; }

private:
    std::thread thread_;
    DWORD id_ = 0;
    HWND host_ = nullptr;
};

void WINAPI Initialize(void*) {
    styler::tap::InitializeForCurrentThread();
}

void WINAPI CloseHost(void* parameter) {
    DestroyWindow(static_cast<HWND>(parameter));
}

void WINAPI ThrowFromCallback(void*) {
    throw std::runtime_error("test callback failure");
}

HWND DispatchWindowFor(DWORD thread_id) {
    for (HWND window : styler::tap::GetInitializedThreadWnds()) {
        if (GetWindowThreadProcessId(window, nullptr) == thread_id) {
            return window;
        }
    }
    return nullptr;
}

struct PostedFixture {
    HANDLE entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE finished = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ~PostedFixture() {
        SetEvent(release);
        CloseHandle(entered);
        CloseHandle(release);
        CloseHandle(finished);
    }
};
thread_local PostedFixture* t_posted_fixture = nullptr;
void PostedHandler(unsigned command, std::uint64_t) {
    if (command == 1) {
        SetEvent(t_posted_fixture->entered);
        WaitForSingleObject(t_posted_fixture->release, 2000);
    } else {
        SetEvent(t_posted_fixture->finished);
    }
}
void WINAPI ConfigurePosted(void* value) {
    t_posted_fixture = static_cast<PostedFixture*>(value);
    styler::tap::SetCommandHandlerForCurrentThread(PostedHandler);
}
struct OwnedDispatchResult { DWORD thread = 0; };
void WINAPI RecordOwnedThread(void* value) {
    static_cast<OwnedDispatchResult*>(value)->thread = GetCurrentThreadId();
}
void WINAPI CreateReleaseOnlyDispatcher(void*) {
    CHECK(styler::tap::EnsureDispatchWindowForCurrentThread() != nullptr);
    CHECK_FALSE(styler::tap::IsInitializedForCurrentThread());
}

}  // namespace

TEST_CASE("initialized thread remains reachable after its shell host closes") {
    const auto before = styler::tap::GetInitializedThreadWnds().size();
    HWND dispatch = nullptr;
    {
        HostThread host;
        REQUIRE(host.host() != nullptr);
        REQUIRE(styler::tap::RunOnWindowThread(host.host(), Initialize, nullptr));
        dispatch = DispatchWindowFor(host.id());
        REQUIRE(dispatch != nullptr);
        CHECK_FALSE(IsWindowVisible(dispatch));

        // Multiple hosts/notifications on one thread must not add endpoints.
        REQUIRE(styler::tap::RunOnWindowThread(host.host(), Initialize, nullptr));
        CHECK(styler::tap::GetInitializedThreadWnds().size() == before + 1);
        REQUIRE(styler::tap::RunOnWindowThread(host.host(), CloseHost,
                                              host.host()));
        CHECK_FALSE(IsWindow(host.host()));
        CHECK(DispatchWindowFor(host.id()) == dispatch);
        CHECK(styler::tap::RunOnWindowThread(dispatch, Initialize, nullptr));
    }
    // No registry/TID entry survives normal thread exit, even without an
    // explicit UninitializeForCurrentThread call.
    CHECK_FALSE(IsWindow(dispatch));
    CHECK(styler::tap::GetInitializedThreadWnds().size() == before);
}

TEST_CASE("dispatch callback failure is reported and does not escape the loop") {
    HostThread host;
    REQUIRE(host.host() != nullptr);
    REQUIRE(styler::tap::RunOnWindowThread(host.host(), Initialize, nullptr));
    HWND dispatch = DispatchWindowFor(host.id());
    REQUIRE(dispatch != nullptr);
    CHECK_FALSE(styler::tap::RunOnWindowThread(dispatch, ThrowFromCallback, nullptr));
    CHECK(styler::tap::RunOnWindowThread(dispatch, Initialize, nullptr));
}

TEST_CASE("posted control commands do not wait for a busy owner UI") {
    PostedFixture fixture;
    HostThread host;
    REQUIRE(styler::tap::RunOnWindowThread(host.host(), ConfigurePosted, &fixture));
    const HWND dispatch = DispatchWindowFor(host.id());
    REQUIRE(dispatch != nullptr);
    CHECK(styler::tap::PostThreadCommand(dispatch, 1, 1));
    CHECK(WaitForSingleObject(fixture.entered, 1000) == WAIT_OBJECT_0);
    // This call returns while the first handler is still blocked above.
    CHECK(styler::tap::PostThreadCommand(dispatch, 2, 1));
    CHECK(WaitForSingleObject(fixture.finished, 0) == WAIT_TIMEOUT);
    SetEvent(fixture.release);
    CHECK(WaitForSingleObject(fixture.finished, 1000) == WAIT_OBJECT_0);
}

TEST_CASE("snapshot release dispatch owns its payload without enabling styling") {
    HostThread host;
    REQUIRE(styler::tap::RunOnWindowThread(host.host(), CreateReleaseOnlyDispatcher, nullptr));
    const HWND dispatch = DispatchWindowFor(host.id());
    REQUIRE(dispatch != nullptr);
    auto result = std::make_shared<OwnedDispatchResult>();
    const std::weak_ptr<OwnedDispatchResult> weak = result;
    CHECK(styler::tap::RunOwnedOnWindowThread(dispatch, RecordOwnedThread, result));
    CHECK(result->thread == host.id());
    result.reset();
    CHECK(weak.expired());
    CHECK(styler::tap::RunOnWindowThread(dispatch, Initialize, nullptr));
    CHECK(DispatchWindowFor(host.id()) == dispatch);
}
