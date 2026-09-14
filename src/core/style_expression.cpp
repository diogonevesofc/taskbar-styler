// SPDX-License-Identifier: GPL-3.0-or-later
#include <styler/style_expression.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <stdexcept>

namespace styler {
namespace {

// A value while an expression is being evaluated: either a number or a
// string. Number literals and numeric variables produce numbers; backtick
// literals and string-typed variables produce strings.
struct ExprValue {
    std::optional<double> number;
    std::wstring text;

    static ExprValue Number(double d) { return {d, std::wstring()}; }
    static ExprValue String(std::wstring s) {
        return {std::nullopt, std::move(s)};
    }
    bool IsNumber() const { return number.has_value(); }
};

class EvalError : public std::runtime_error {
   public:
    explicit EvalError(const char* what) : std::runtime_error(what) {}
};

// Recursive-descent evaluator for one `{{ ... }}` body. Operands: number
// literals, backtick-delimited string literals, variable references and
// parenthesised subexpressions. Operators: binary + - * /, unary + -, the
// comparisons < <= == >= > != (yielding 1 or 0), the conditional
// `cond ? a : b`, and the two-argument min(a, b) / max(a, b). Standard
// precedence. Arithmetic, the unary sign, the relational comparisons and
// min/max require numbers; == and != compare two numbers or two strings and
// treat a number-versus-string mismatch as unequal; the conditional's
// condition must be numeric but its branches need not be. The conditional
// short-circuits: the untaken branch is parsed (to advance the position and
// enforce syntax) but not evaluated, so it cannot fail the whole expression
// or add a dependency - see live_.
// Ported from upstream vendor:16220-16650.
class Evaluator {
   public:
    Evaluator(std::wstring_view text, const StyleVariableLookup& lookup,
              std::vector<std::wstring>* deps)
        : text_(text), lookup_(lookup), deps_(deps) {}

    // The text form of the result. Throws EvalError on any failure,
    // including a non-finite number: NaN and infinity cannot be written into
    // a XAML attribute meaningfully, and NaN would also break the
    // "did the value change?" check the propagation side does.
    std::wstring Evaluate() {
        pos_ = 0;
        SkipSpace();
        ExprValue v = ParseExpression();
        SkipSpace();
        if (pos_ != text_.size()) {
            throw EvalError("trailing text in expression");
        }
        if (!v.IsNumber()) {
            return v.text;
        }
        if (!std::isfinite(*v.number)) {
            throw EvalError("non-finite result");
        }
        return FormatDoubleInvariant(*v.number);
    }

   private:
    static bool IsSpace(wchar_t c) {
        return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
    }
    static bool IsIdentStart(wchar_t c) {
        return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
               c == L'_';
    }
    static bool IsIdentCont(wchar_t c) {
        return IsIdentStart(c) || (c >= L'0' && c <= L'9');
    }

    void SkipSpace() {
        while (pos_ < text_.size() && IsSpace(text_[pos_])) {
            ++pos_;
        }
    }
    bool Take(wchar_t c) {
        SkipSpace();
        if (pos_ < text_.size() && text_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }
    bool Take(std::wstring_view s) {
        SkipSpace();
        if (text_.compare(pos_, s.size(), s) == 0) {
            pos_ += s.size();
            return true;
        }
        return false;
    }
    wchar_t Peek() const { return pos_ < text_.size() ? text_[pos_] : L'\0'; }

    const StyleVariableValue* Lookup(std::wstring_view name) const;

    // In a dead ternary branch (live_ == false) the value is discarded, so a
    // non-numeric operand is tolerated (reported as 0) instead of aborting
    // the whole expression. Mirrors upstream's RequireNumber
    // (vendor:16296-16307).
    double RequireNumber(const ExprValue& v) const {
        if (v.IsNumber()) {
            return *v.number;
        }
        if (live_) {
            throw EvalError("expected a number");
        }
        return 0.0;
    }

    static bool ValuesEqual(const ExprValue& a, const ExprValue& b) {
        if (a.IsNumber() != b.IsNumber()) {
            return false;  // A number never equals a string.
        }
        return a.IsNumber() ? *a.number == *b.number : a.text == b.text;
    }

    ExprValue ParseExpression() { return ParseTernary(); }

    // Short-circuit, matching upstream (vendor:16330-16357): only the taken
    // branch is evaluated. The untaken branch is still parsed - to advance
    // the position and enforce syntax - with live_ cleared, which suppresses
    // that branch's value-level errors (division by zero, a non-numeric or
    // undefined variable, an unknown function) and its dependency capture.
    ExprValue ParseTernary() {
        ExprValue cond = ParseEquality();
        SkipSpace();
        if (Peek() != L'?') {
            return cond;
        }
        ++pos_;
        bool cond_true = RequireNumber(cond) != 0.0;
        bool prev_live = live_;

        live_ = prev_live && cond_true;
        ExprValue then_value = ParseExpression();
        live_ = prev_live;

        if (!Take(L':')) {
            throw EvalError("expected ':' in conditional");
        }

        live_ = prev_live && !cond_true;
        ExprValue else_value = ParseTernary();
        live_ = prev_live;

        return cond_true ? then_value : else_value;
    }

    ExprValue ParseEquality() {
        ExprValue v = ParseRelational();
        for (;;) {
            SkipSpace();
            if (Take(L"==")) {
                v = ExprValue::Number(ValuesEqual(v, ParseRelational()) ? 1.0
                                                                       : 0.0);
            } else if (Take(L"!=")) {
                v = ExprValue::Number(ValuesEqual(v, ParseRelational()) ? 0.0
                                                                       : 1.0);
            } else {
                return v;
            }
        }
    }

    ExprValue ParseRelational() {
        ExprValue v = ParseAdditive();
        for (;;) {
            SkipSpace();
            double lhs = 0.0;
            if (Take(L"<=")) {
                lhs = RequireNumber(v);
                v = ExprValue::Number(lhs <= RequireNumber(ParseAdditive()) ? 1.0
                                                                           : 0.0);
            } else if (Take(L">=")) {
                lhs = RequireNumber(v);
                v = ExprValue::Number(lhs >= RequireNumber(ParseAdditive()) ? 1.0
                                                                           : 0.0);
            } else if (Peek() == L'<') {
                ++pos_;
                lhs = RequireNumber(v);
                v = ExprValue::Number(lhs < RequireNumber(ParseAdditive()) ? 1.0
                                                                          : 0.0);
            } else if (Peek() == L'>') {
                ++pos_;
                lhs = RequireNumber(v);
                v = ExprValue::Number(lhs > RequireNumber(ParseAdditive()) ? 1.0
                                                                          : 0.0);
            } else {
                return v;
            }
        }
    }

    ExprValue ParseAdditive() {
        ExprValue v = ParseTerm();
        for (;;) {
            SkipSpace();
            wchar_t c = Peek();
            if (c != L'+' && c != L'-') {
                return v;
            }
            ++pos_;
            double lhs = RequireNumber(v);
            double rhs = RequireNumber(ParseTerm());
            v = ExprValue::Number(c == L'+' ? lhs + rhs : lhs - rhs);
        }
    }

    ExprValue ParseTerm() {
        ExprValue v = ParseUnary();
        for (;;) {
            SkipSpace();
            wchar_t c = Peek();
            if (c != L'*' && c != L'/') {
                return v;
            }
            ++pos_;
            double lhs = RequireNumber(v);
            double rhs = RequireNumber(ParseUnary());
            if (c == L'/' && rhs == 0.0) {
                if (live_) {
                    throw EvalError("division by zero");
                }
                // Dead ternary branch: the result is discarded, so skip the
                // divide instead of throwing or producing inf/nan.
                v = ExprValue::Number(lhs);
                continue;
            }
            v = ExprValue::Number(c == L'*' ? lhs * rhs : lhs / rhs);
        }
    }

    ExprValue ParseUnary() {
        SkipSpace();
        wchar_t c = Peek();
        if (c == L'-') {
            ++pos_;
            return ExprValue::Number(-RequireNumber(ParseUnary()));
        }
        if (c == L'+') {
            ++pos_;
            return ExprValue::Number(RequireNumber(ParseUnary()));
        }
        return ParsePrimary();
    }

    ExprValue ParsePrimary() {
        SkipSpace();
        if (pos_ >= text_.size()) {
            throw EvalError("unexpected end of expression");
        }
        wchar_t c = text_[pos_];

        if (c == L'(') {
            ++pos_;
            ExprValue v = ParseExpression();
            if (!Take(L')')) {
                throw EvalError("expected ')'");
            }
            return v;
        }

        if (c == L'`') {
            // A doubled backtick encodes one literal backtick.
            ++pos_;
            std::wstring out;
            for (;;) {
                if (pos_ >= text_.size()) {
                    throw EvalError("unterminated string literal");
                }
                if (text_[pos_] == L'`') {
                    if (pos_ + 1 < text_.size() && text_[pos_ + 1] == L'`') {
                        out += L'`';
                        pos_ += 2;
                        continue;
                    }
                    ++pos_;
                    return ExprValue::String(std::move(out));
                }
                out += text_[pos_++];
            }
        }

        if ((c >= L'0' && c <= L'9') || c == L'.') {
            size_t start = pos_;
            while (pos_ < text_.size() &&
                   ((text_[pos_] >= L'0' && text_[pos_] <= L'9') ||
                    text_[pos_] == L'.')) {
                ++pos_;
            }
            std::wstring_view digits = text_.substr(start, pos_ - start);
            // Narrow explicitly: digits are ASCII ('0'-'9', '.'), but an
            // iterator-range basic_string construction narrows implicitly
            // and trips /W4 C4244 (same fix as blur.cpp's AppendNarrow).
            std::string narrow;
            narrow.reserve(digits.size());
            for (wchar_t d : digits) {
                narrow += static_cast<char>(d);
            }
            double out = 0.0;
            auto [ptr, ec] = std::from_chars(
                narrow.data(), narrow.data() + narrow.size(), out);
            if (ec != std::errc{} || ptr != narrow.data() + narrow.size()) {
                throw EvalError("bad number literal");
            }
            return ExprValue::Number(out);
        }

        if (IsIdentStart(c)) {
            size_t start = pos_;
            while (pos_ < text_.size() && IsIdentCont(text_[pos_])) {
                ++pos_;
            }
            std::wstring_view name = text_.substr(start, pos_ - start);
            SkipSpace();
            if (Peek() == L'(') {
                // The args are always parsed - even for an unknown name or a
                // dead branch - so the position ends up past the call no
                // matter what; only whether an unknown name is an error
                // depends on live_ (vendor:16575-16597).
                ++pos_;
                ExprValue a = ParseExpression();
                if (!Take(L',')) {
                    throw EvalError("expected ',' in min/max");
                }
                ExprValue b = ParseExpression();
                if (!Take(L')')) {
                    throw EvalError("expected ')' after min/max");
                }
                double x = RequireNumber(a);
                double y = RequireNumber(b);
                if (name == L"min") {
                    return ExprValue::Number(x < y ? x : y);
                }
                if (name == L"max") {
                    return ExprValue::Number(x > y ? x : y);
                }
                if (live_) {
                    throw EvalError("unknown function");
                }
                return ExprValue::Number(0.0);
            }
            if (!live_) {
                // Dead ternary branch: skip the lookup along with dependency
                // capture, same as the value-level errors below - the branch
                // must not abort the whole expression or make Task 6
                // recompute on a variable this style does not actually use
                // (vendor:16601-16608).
                return ExprValue::String(std::wstring());
            }
            const StyleVariableValue* var = Lookup(name);
            if (!var) {
                // Undefined inside an expression is the empty string, so a
                // theme can default with `{{w == `` ? 80 : w}}`. The numeric
                // operators then fail on it, which skips the style rather
                // than pretending it is 0.
                return ExprValue::String(std::wstring());
            }
            if (var->number) {
                return ExprValue::Number(*var->number);
            }
            return ExprValue::String(var->text);
        }

        throw EvalError("unexpected character");
    }

    std::wstring_view text_;
    const StyleVariableLookup& lookup_;
    std::vector<std::wstring>* deps_;
    size_t pos_ = 0;
    // False while parsing the untaken branch of a ternary: value-level
    // errors and dependency capture are suppressed for it (vendor:16333).
    bool live_ = true;
};

void AddDependency(std::vector<std::wstring>* deps, std::wstring_view name) {
    if (!deps) {
        return;
    }
    std::wstring owned(name);
    if (std::find(deps->begin(), deps->end(), owned) == deps->end()) {
        deps->push_back(std::move(owned));
    }
}

const StyleVariableValue* Evaluator::Lookup(std::wstring_view name) const {
    AddDependency(deps_, name);
    return lookup_ ? lookup_(name) : nullptr;
}

// The whole body is a single identifier and nothing else: the "bare
// reference" form, which substitutes the captured text verbatim and which an
// undefined variable must skip rather than blank out. Empty when it is not.
std::wstring_view BareIdentifier(std::wstring_view body) {
    auto is_space = [](wchar_t c) { return c == L' ' || c == L'\t'; };
    auto is_start = [](wchar_t c) {
        return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || c == L'_';
    };
    auto is_cont = [&](wchar_t c) {
        return is_start(c) || (c >= L'0' && c <= L'9');
    };
    size_t i = 0;
    while (i < body.size() && is_space(body[i])) {
        ++i;
    }
    size_t start = i;
    if (i >= body.size() || !is_start(body[i])) {
        return {};
    }
    while (i < body.size() && is_cont(body[i])) {
        ++i;
    }
    size_t end = i;
    while (i < body.size() && is_space(body[i])) {
        ++i;
    }
    return i == body.size() ? body.substr(start, end - start)
                            : std::wstring_view{};
}

}  // namespace

std::wstring FormatDoubleInvariant(double value) {
    // to_chars with no format flag gives the shortest representation that
    // round-trips, always with '.' as the separator - which is exactly what a
    // XAML attribute needs and what the process locale must not be allowed to
    // change.
    std::array<char, 64> buffer{};
    auto [ptr, ec] =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (ec != std::errc{}) {
        return L"0";
    }
    return std::wstring(buffer.data(), ptr);
}

std::optional<std::wstring> ExpandStyleVariables(
    std::wstring_view input, const StyleVariableLookup& lookup,
    std::vector<std::wstring>* deps) {
    std::wstring result(input);
    size_t scan_from = 0;

    for (;;) {
        // Leftmost `}}` at or after scan_from...
        size_t close = std::wstring::npos;
        for (size_t i = scan_from; i + 1 < result.size(); ++i) {
            if (result[i] == L'}' && result[i + 1] == L'}') {
                close = i;
                break;
            }
        }
        if (close == std::wstring::npos) {
            return result;
        }

        // ...and the RIGHTMOST `{{` strictly before it. That pairing is what
        // makes `{{{x}}}` parse as `{` + substitution + `}`.
        size_t open = std::wstring::npos;
        for (size_t j = close; j >= 1; --j) {
            if (result[j - 1] == L'{' && result[j] == L'{') {
                open = j - 1;
                break;
            }
            if (j == 1) {
                break;
            }
        }
        if (open == std::wstring::npos) {
            return std::nullopt;  // Unmatched `}}`.
        }

        std::wstring_view body(result.data() + open + 2, close - open - 2);
        std::wstring expanded;
        // A whole-value bare reference is the only form allowed to carry a
        // non-numeric captured value through verbatim, and the only one an
        // undefined variable must skip rather than blank out.
        if (std::wstring_view bare = BareIdentifier(body); !bare.empty()) {
            AddDependency(deps, bare);
            const StyleVariableValue* var = lookup ? lookup(bare) : nullptr;
            if (!var || !var->substitutable) {
                return std::nullopt;
            }
            expanded = var->text;
        } else {
            try {
                expanded = Evaluator(body, lookup, deps).Evaluate();
            } catch (const std::exception&) {
                return std::nullopt;
            }
        }

        result.replace(open, close + 2 - open, expanded);
        scan_from = open + expanded.size();
    }
}

}  // namespace styler
