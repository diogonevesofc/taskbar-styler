// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace styler {

// One captured style variable, as the consumer side sees it.
struct StyleVariableValue {
    // Text form, used by a bare `{{Var}}` substitution and by string
    // comparisons. Empty means "captured, but with no value".
    std::wstring text;
    // Engaged when the captured value is numeric, which is what arithmetic,
    // the relational operators and min/max require.
    std::optional<double> number;
    // False for opaque captures (a brush, a thickness): the variable exists,
    // but substituting it would emit a class name into the XAML. A bare
    // `{{Var}}` on such a variable skips the style.
    bool substitutable = false;
};

// Returns the variable, or nullptr when no capture currently defines it.
using StyleVariableLookup =
    std::function<const StyleVariableValue*(std::wstring_view)>;

// Expands every `{{ ... }}` in `input`. Brace pairs are matched innermost
// first, so `{{{x}}}` is a literal `{`, a substitution, and a literal `}`.
//
// Returns nullopt when the style must be SKIPPED rather than applied:
//   * a bare `{{Var}}` whose variable is undefined or not substitutable;
//   * a malformed expression, an unmatched `}}`, a type error (arithmetic on
//     a string), or a non-finite result.
// Inside a larger expression an undefined variable is the empty string, so a
// theme can supply its own default with the conditional operator.
//
// Every variable name the input referenced is appended to `deps` when `deps`
// is non-null, including names that turned out undefined - the caller
// registers them so the style is recomputed once something captures them.
// Pure: no Windows, no XAML.
std::optional<std::wstring> ExpandStyleVariables(
    std::wstring_view input, const StyleVariableLookup& lookup,
    std::vector<std::wstring>* deps);

// Formats a double the way a XAML attribute wants it: invariant culture,
// shortest representation that round-trips, no trailing zeros.
std::wstring FormatDoubleInvariant(double value);

}  // namespace styler
