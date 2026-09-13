// SPDX-License-Identifier: GPL-3.0-or-later
#include <styler/type_name.h>

#include <utility>
#include <vector>

namespace styler {

std::wstring AdjustTypeName(std::wstring_view type) {
    if (type.find_first_of(L".:") == std::wstring_view::npos) {
        if (type == L"Rectangle") {
            return L"Windows.UI.Xaml.Shapes.Rectangle";
        }
        return L"Windows.UI.Xaml.Controls." + std::wstring(type);
    }

    static const std::vector<std::pair<std::wstring_view, std::wstring_view>>
        kPrefixes = {
            {L"taskbar:", L"Taskbar."},
            {L"systemtray:", L"SystemTray."},
            {L"udk:", L"WindowsUdk.UI.Shell."},
            {L"muxc:", L"Microsoft.UI.Xaml.Controls."},
        };
    for (const auto& [prefix, ns] : kPrefixes) {
        if (type.starts_with(prefix)) {
            std::wstring out(ns);
            out += type.substr(prefix.size());
            return out;
        }
    }
    return std::wstring(type);
}

}  // namespace styler
