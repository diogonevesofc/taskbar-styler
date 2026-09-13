// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <windows.h>

namespace styler::tap {

// Subscribes to IVisualTreeService3::AdviseVisualTreeChange for the life of
// the session. The initial flood arrives synchronously inside Start on the
// calling thread (measured: spike, Plano 2); later reports arrive on
// whichever UI thread mutates its tree. Idempotent: a second Start with a
// live subscription is a no-op that returns S_FALSE.
HRESULT StartSubscription();

// Unadvises. If Unadvise fails the callback object is leaked on purpose -
// XAML may still call into it, and a freed vtable inside explorer is worse
// than one small leak (same stance as ReleaseOnExit in tree_export.cpp).
void StopSubscription();

}  // namespace styler::tap
