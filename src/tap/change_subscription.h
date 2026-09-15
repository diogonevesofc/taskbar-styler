// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
namespace styler::tap {
// Advise runs on a dedicated thread because its synchronous flood requires
// the UI message loop. A stopping subscription remains owned/published until
// Unadvise succeeds and all callbacks finish. Failed stops require an explicit
// retry; they never permit a replacement subscription or diagnostics session.
HRESULT StartSubscription();
enum class StopResult { None, Stopped, Deferred, Failed };
StopResult StopSubscription();
// Called on arbitrary worker/callback threads; must only post work, never wait.
void SetSubscriptionCompletion(void (*callback)());
}  // namespace styler::tap
