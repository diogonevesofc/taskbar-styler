// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/release_queue.h>

#include <chrono>
#include <vector>

#include <tap/element_registry.h>
#include <tap/log.h>
#include <tap/release_policy.h>
#include <tap/style_engine.h>
#include <tap/visual_tree_watcher.h>
#include <tap/winrt_common.h>

namespace styler::tap {
namespace {

constexpr ULONGLONG kQuietMs = 200;  // Long enough to sit out a tree build.
constexpr unsigned kDrainDelayMs = 1;  // Only has to leave the report's frame.

thread_local std::vector<unsigned long long> t_pending;
thread_local ULONGLONG t_last_queue_tick = 0;
thread_local bool t_drain_armed = false;
thread_local bool t_no_dispatcher_logged = false;
thread_local winrt::Windows::System::DispatcherQueueTimer t_timer{nullptr};
// Set after this thread's first drain, so the "initial apply" stats below
// are logged exactly once, not on every later add/remove.
thread_local bool t_initial_apply_logged = false;

}  // namespace

void QueueRelease(InstanceHandle handle) {
    if (!handle) {
        return;
    }
    t_pending.push_back(handle);
    t_last_queue_tick = GetTickCount64();
}

void FlushReleasesNow() {
    t_drain_armed = false;
    std::vector<unsigned long long> pending = std::move(t_pending);
    t_pending.clear();

    ReleaseResult result =
        HandlesToRelease(std::move(pending), [](unsigned long long h) {
            return ElementHasState(FindElementId(h));
        });
    for (unsigned long long h : result.to_release) {
        ReleaseHandle(h);
        ForgetElementIdIfDead(h);
    }
    // Releases above are what let elements die unreported; sweep for those
    // here, outside any walk.
    ReapDeadElementIdsIfNeeded();
    // "held" is the live-handle gauge spec section 7.2 asks for: handles we
    // deliberately keep because their element carries state. Found in
    // review: this used to be pending.size() - to_release.size(), the RAW
    // (undeduped) queue length minus the released count, which counted
    // duplicate parent handles (one push per child sharing a parent) as
    // "held" - a lot of them, whenever a burst has many siblings, for zero
    // real reason. unique_count is deduped and drops the zero sentinel
    // (release_policy.h), so this is 0 whenever ElementHasState never
    // returns true, on any input - it must not grow while the taskbar sits
    // still, and it does not, now.
    STYLER_LOG(LogLevel::Info, L"drained %zu handles, %zu held (%ld released so far)",
               result.to_release.size(), result.unique_count - result.to_release.size(),
               ReleasedHandleCount());

    // This thread's first drain only ever runs once the queue has sat quiet
    // for kQuietMs (FlushReleasesIfQuiet), which the initial subscription
    // flood's own dense burst of Add reports cannot do until it is over -
    // unlike logging right after StartSubscription() returns in SetSite,
    // which fires before XAML's marshalled walk can even start (see
    // tap_boundary.cpp). So this is where Task 5's "initial apply" counters
    // are complete. Review finding (minor): FlushReleasesIfQuiet checks
    // staleness at the START of a report, against the PREVIOUS report's
    // queue time - so arming (and this log line) needs one MORE tree
    // change to arrive after the flood's own last report, whenever that
    // happens to be (a clock tick, a hover, anything). Naming that in the
    // line itself so a taskbar that goes instantly idle right after a
    // clean load does not read as a load that silently did nothing.
    if (!t_initial_apply_logged) {
        t_initial_apply_logged = true;
        EngineStats stats = StatsForThisThread();
        STYLER_LOG(LogLevel::Info,
                   L"initial apply (as of first drain): %zu elements, %zu "
                   L"properties, %zu failed, %zu visual-state styles deferred",
                   stats.styled_elements, stats.applied_properties,
                   stats.failed_styles, stats.deferred_visual_state_styles);
    }
}

void FlushReleasesIfQuiet() {
    if (t_pending.empty() || t_drain_armed ||
        GetTickCount64() - t_last_queue_tick < kQuietMs) {
        return;
    }
    try {
        if (!t_timer) {
            auto queue = winrt::Windows::System::DispatcherQueue::GetForCurrentThread();
            if (!queue) {
                // Releasing from here is the one thing that isn't safe, so
                // the elements stay held. Said once per thread.
                if (!t_no_dispatcher_logged) {
                    t_no_dispatcher_logged = true;
                    STYLER_LOG(LogLevel::Error,
                               L"no DispatcherQueue on thread %lu: %zu handles held",
                               GetCurrentThreadId(), t_pending.size());
                }
                return;
            }
            t_timer = queue.CreateTimer();
            t_timer.IsRepeating(false);
            t_timer.Interval(std::chrono::milliseconds{kDrainDelayMs});
            // The returned event_token would only matter if we ever needed
            // to unsubscribe this handler and keep the timer - we never do
            // either (the timer is thread_local and outlives the thread),
            // so it is discarded rather than kept unread.
            t_timer.Tick(
                [](winrt::Windows::System::DispatcherQueueTimer const&,
                   wf::IInspectable const&) {
                    try {
                        FlushReleasesNow();
                    } catch (winrt::hresult_error const& ex) {
                        STYLER_LOG(LogLevel::Error, L"drain hresult 0x%08X",
                                   static_cast<unsigned>(ex.code()));
                    } catch (...) {
                        STYLER_LOG(LogLevel::Error, L"drain threw");
                    }
                });
        }
        t_timer.Start();
        t_drain_armed = true;
    } catch (winrt::hresult_error const& ex) {
        STYLER_LOG(LogLevel::Error, L"arming drain failed 0x%08X",
                   static_cast<unsigned>(ex.code()));
    } catch (...) {
        STYLER_LOG(LogLevel::Error, L"arming drain threw");
    }
}

}  // namespace styler::tap
