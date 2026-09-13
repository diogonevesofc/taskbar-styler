// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/style_engine.h>

#include <tap/log.h>

namespace styler::tap {

// Task 4 stub: the subscription is wired and drained, nothing is styled yet.
// Task 5 replaces this file's bodies, not its signatures.

void OnElementAdded(ElementId id, wux::FrameworkElement const& element,
                    const wchar_t* reported_type) {
    STYLER_LOG(LogLevel::Debug, L"add %llu %s (%s)",
               static_cast<unsigned long long>(id),
               winrt::get_class_name(element).c_str(),
               reported_type ? reported_type : L"");
}

void OnElementRemoved(ElementId id) {
    STYLER_LOG(LogLevel::Debug, L"remove %llu",
               static_cast<unsigned long long>(id));
}

bool ElementHasState(ElementId) {
    return false;
}

}  // namespace styler::tap
