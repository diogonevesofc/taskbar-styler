// SPDX-License-Identifier: GPL-3.0-or-later
#include <styler/utf.h>

#include <cstdint>

namespace styler {

std::wstring Utf8ToWide(std::string_view utf8) {
    std::wstring out;
    out.reserve(utf8.size());

    size_t i = 0;
    while (i < utf8.size()) {
        auto b0 = static_cast<unsigned char>(utf8[i]);
        char32_t cp = 0;
        size_t extra = 0;
        char32_t min_cp = 0;  // Minimum valid code point for this sequence
                               // length; 0 for ASCII and invalid lead bytes,
                               // which need no overlong check below.

        if (b0 < 0x80) {
            cp = b0;
        } else if ((b0 & 0xE0) == 0xC0) {
            cp = b0 & 0x1F;
            extra = 1;
            min_cp = 0x80;
        } else if ((b0 & 0xF0) == 0xE0) {
            cp = b0 & 0x0F;
            extra = 2;
            min_cp = 0x800;
        } else if ((b0 & 0xF8) == 0xF0) {
            cp = b0 & 0x07;
            extra = 3;
            min_cp = 0x10000;
        } else {
            cp = 0xFFFD;
        }

        if (i + extra >= utf8.size()) {
            cp = 0xFFFD;
            extra = 0;
        }

        for (size_t k = 1; k <= extra; k++) {
            auto bk = static_cast<unsigned char>(utf8[i + k]);
            if ((bk & 0xC0) != 0x80) {
                cp = 0xFFFD;
                extra = k - 1;
                break;
            }
            cp = (cp << 6) | (bk & 0x3F);
        }

        // Reject anything that is not a well-formed scalar value, replacing
        // it with U+FFFD instead of emitting it: (1) overlong encodings — a
        // decoded code point below the minimum for the sequence length that
        // produced it (0x80 for 2-byte, 0x800 for 3-byte, 0x10000 for
        // 4-byte), which can smuggle e.g. an embedded NUL or '/' past a
        // caller filtering on the decoded string; (2) surrogate code points
        // (0xD800-0xDFFF), which UTF-8 must never encode directly; and
        // (3) values beyond the Unicode range (> 0x10FFFF), which malformed
        // 4-byte lead bytes (0xF5-0xF7) or their continuations can produce.
        // This is a no-op when cp is already 0xFFFD from the checks above.
        if (min_cp != 0 &&
            (cp < min_cp || (cp >= 0xD800 && cp <= 0xDFFF) ||
             cp > 0x10FFFF)) {
            cp = 0xFFFD;
        }

        i += extra + 1;

        if (cp > 0xFFFF) {
            cp -= 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<wchar_t>(cp));
        }
    }

    return out;
}

std::string WideToUtf8(std::wstring_view wide) {
    std::string out;
    out.reserve(wide.size());

    for (size_t i = 0; i < wide.size(); i++) {
        char32_t cp = wide[i];

        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < wide.size() &&
            wide[i + 1] >= 0xDC00 && wide[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (wide[i + 1] - 0xDC00);
            i++;
        }

        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    return out;
}

}  // namespace styler
