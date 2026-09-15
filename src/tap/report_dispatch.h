// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <exception>

namespace styler::tap {

// Styling is optional; ownership bookkeeping is mandatory for every report.
// Preserve a style failure for the caller's diagnostic boundary only after
// the report has had its opportunity to enter the deferred-release queue.
template <class Style, class Enqueue>
void DispatchObservedReport(bool initialized, Style&& style, Enqueue&& enqueue) {
    std::exception_ptr style_failure;
    try {
        if (initialized) style();
    } catch (...) {
        style_failure = std::current_exception();
    }
    enqueue();
    if (style_failure) std::rethrow_exception(style_failure);
}

}  // namespace styler::tap
