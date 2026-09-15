// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/release_queue.h>

#include <chrono>
#include <tap/element_registry.h>
#include <tap/log.h>
#include <tap/owned_release_queue.h>
#include <tap/style_engine.h>
#include <tap/style_variables.h>
#include <tap/thread_init.h>
#include <tap/visual_tree_watcher.h>
#include <tap/winrt_common.h>

namespace styler::tap {
namespace {
constexpr ULONGLONG kQuietMs = 200;
thread_local OwnedReleaseQueue<DiagnosticsSession> t_pending;
thread_local ULONGLONG t_last_queue_tick = 0;
thread_local bool t_drain_armed = false;
thread_local bool t_no_dispatcher_logged = false;
thread_local winrt::Windows::System::DispatcherQueueTimer t_timer{nullptr};
thread_local winrt::event_token t_tick_token{};
thread_local bool t_initial_apply_logged = false;
}

void QueueRelease(const std::shared_ptr<DiagnosticsSession>& owner, InstanceHandle handle) {
    if (!owner || !handle) return;
    t_pending.Add(owner->owner(), handle, owner);
    // Reports can precede styling initialization. A release-only window
    // keeps this queue reachable by reset's post-Unadvise drain as well.
    if (!EnsureDispatchWindowForCurrentThread()) MarkHandleObservationIncomplete();
    t_last_queue_tick = GetTickCount64();
    FlushReleasesIfQuiet();
}

void FlushReleasesNow() {
    t_drain_armed = false;
    const auto current = AcquireSession();
    const auto released = t_pending.Drain(
        [&](const auto& owner, auto handle) {
            return owner == current && ElementHasState(FindElementId(handle));
        },
        [](const auto& owner, auto handle) {
            if (!ReleaseHandle(owner, handle)) return false;
            ForgetElementIdIfDead(handle);
            return true;
        });
    ReapDeadElementIdsIfNeeded();
    STYLER_LOG(LogLevel::Info, L"drained %zu handles, %zu retained on thread %lu",
               released, t_pending.size(), GetCurrentThreadId());
    LogHandleObservation();
    if (!t_initial_apply_logged) {
        t_initial_apply_logged = true;
        const EngineStats stats = StatsForThisThread();
        STYLER_LOG(LogLevel::Info,
            L"apply (as of first drain): %zu elements, %zu properties, %zu failed, "
            L"%zu visual-state styles deferred, %zu blur brushes, %zu blur fallbacks, %zu variables",
            stats.styled_elements, stats.applied_properties, stats.failed_styles,
            stats.deferred_visual_state_styles, stats.blur_brushes,
            stats.blur_fallbacks, DefinedVariableCount());
    }
}

void ResetInitialApplyLogged() { t_initial_apply_logged = false; }

void FlushReleasesIfQuiet() {
    if (t_pending.empty() || t_drain_armed) return;
    try {
        if (!t_timer) {
            const auto queue = winrt::Windows::System::DispatcherQueue::GetForCurrentThread();
            if (!queue) {
                MarkHandleObservationIncomplete();
                if (!t_no_dispatcher_logged) {
                    t_no_dispatcher_logged = true;
                    STYLER_LOG(LogLevel::Error, L"no DispatcherQueue on thread %lu", GetCurrentThreadId());
                }
                return;
            }
            t_timer = queue.CreateTimer();
            t_timer.IsRepeating(false);
            t_tick_token = t_timer.Tick([](const auto&, const auto&) {
                try {
                    t_drain_armed = false;
                    if (GetTickCount64() - t_last_queue_tick < kQuietMs) {
                        FlushReleasesIfQuiet();
                    } else {
                        FlushReleasesNow();
                    }
                } catch (winrt::hresult_error const& ex) {
                    STYLER_LOG(LogLevel::Error, L"drain hresult 0x%08X", static_cast<unsigned>(ex.code()));
                } catch (...) {
                    STYLER_LOG(LogLevel::Error, L"drain threw");
                }
            });
        }
        const auto elapsed = GetTickCount64() - t_last_queue_tick;
        const auto delay = elapsed < kQuietMs ? kQuietMs - elapsed : 1;
        t_timer.Interval(std::chrono::milliseconds{static_cast<std::int64_t>(delay)});
        t_timer.Start();
        t_drain_armed = true;
    } catch (winrt::hresult_error const& ex) {
        MarkHandleObservationIncomplete();
        STYLER_LOG(LogLevel::Error, L"arming drain failed 0x%08X", static_cast<unsigned>(ex.code()));
    } catch (...) {
        MarkHandleObservationIncomplete();
        STYLER_LOG(LogLevel::Error, L"arming drain threw");
    }
}

bool StopReleaseQueueOnThisThread() {
    try {
        if (t_timer) {
            t_timer.Stop();
            t_timer.Tick(t_tick_token);
            t_timer = nullptr;
            t_tick_token = {};
        }
        t_drain_armed = false;
        FlushReleasesNow();
        // This teardown drain is not the next subscription's initial flood.
        // Its later quiet drain must still publish actual application stats.
        t_initial_apply_logged = false;
        STYLER_LOG(LogLevel::Info, L"release timer stopped on thread %lu", GetCurrentThreadId());
        return t_pending.empty();
    } catch (winrt::hresult_error const& ex) {
        STYLER_LOG(LogLevel::Error, L"release queue stop failed 0x%08X", static_cast<unsigned>(ex.code()));
    } catch (...) {
        STYLER_LOG(LogLevel::Error, L"release queue stop threw");
    }
    return false;
}
}  // namespace styler::tap
