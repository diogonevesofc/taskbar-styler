// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

namespace styler {

// The one file that crosses the process boundary, Tray/CLI -> TAP (spec
// section 4.2). `theme` is a theme id (file stem under themes/); empty means
// "no theme". `log_level` is "error", "info" or "debug"; empty keeps the
// TAP's default.
struct Config {
    std::wstring theme;
    std::wstring log_level;
};

Config ParseConfigJson(std::string_view utf8);  // Throws ParseError.
std::string SerializeConfigJson(const Config& config);

}  // namespace styler
