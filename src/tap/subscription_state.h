// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>

namespace styler::tap {
enum class SubscriptionPhase { Advising, Advised, StopRequested, Unadvising, Quiescing, Failed, Stopped };
enum class StopAction { Deferred, Unadvise, Quiesce, Stopped };

// The production ownership arbitration, independent of COM. A stop requested
// during Advise remains visible until Unadvise and active callbacks finish.
class SubscriptionState {
public:
    StopAction RequestStop() {
        auto phase = phase_.load();
        for (;;) {
            switch (phase) {
            case SubscriptionPhase::Advising:
                if (phase_.compare_exchange_weak(phase, SubscriptionPhase::StopRequested))
                    return StopAction::Deferred;
                break;
            case SubscriptionPhase::Advised:
            case SubscriptionPhase::Failed:
                if (phase_.compare_exchange_weak(phase, SubscriptionPhase::Unadvising))
                    return StopAction::Unadvise;
                break;
            case SubscriptionPhase::Quiescing: return StopAction::Quiesce;
            case SubscriptionPhase::Stopped: return StopAction::Stopped;
            default: return StopAction::Deferred;
            }
        }
    }

    bool AdviseFinished(bool succeeded) {
        auto expected = SubscriptionPhase::Advising;
        if (phase_.compare_exchange_strong(expected, succeeded
                ? SubscriptionPhase::Advised : SubscriptionPhase::Unadvising))
            return !succeeded;
        expected = SubscriptionPhase::StopRequested;
        return phase_.compare_exchange_strong(expected, SubscriptionPhase::Unadvising);
    }

    void UnadviseFinished(bool succeeded) {
        phase_.store(succeeded ? SubscriptionPhase::Quiescing : SubscriptionPhase::Failed);
    }
    bool CompleteQuiescence() {
        auto expected = SubscriptionPhase::Quiescing;
        return phase_.compare_exchange_strong(expected, SubscriptionPhase::Stopped);
    }
    SubscriptionPhase phase() const { return phase_.load(); }

private:
    std::atomic<SubscriptionPhase> phase_{SubscriptionPhase::Advising};
};
}  // namespace styler::tap
