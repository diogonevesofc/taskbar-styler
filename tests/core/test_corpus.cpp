// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#include <styler/theme_loader.h>

#ifndef STYLER_THEMES_DIR
#error "STYLER_THEMES_DIR must be defined by CMake"
#endif

TEST_CASE("every shipped theme parses") {
    namespace fs = std::filesystem;

    int themes = 0;
    int rules = 0;
    int dead_rules = 0;
    int selector_dead_rules = 0;   // dead because every selector chain failed
    int style_emptied_rules = 0;   // dead because an empty style entry
                                    // wiped the whole styles list
    int diagnostics = 0;

    for (const auto& entry : fs::directory_iterator(STYLER_THEMES_DIR)) {
        if (entry.path().extension() != ".json") {
            continue;
        }
        if (entry.path().filename() == "credits.json") {
            continue;
        }

        CAPTURE(entry.path().string());
        auto theme = styler::LoadThemeFromFile(entry.path());

        CHECK_FALSE(theme.id.empty());
        CHECK_FALSE(theme.rules.empty());

        themes++;
        rules += static_cast<int>(theme.rules.size());
        diagnostics += static_cast<int>(theme.diagnostics.size());

        for (const auto& rule : theme.rules) {
            if (!rule.dead) {
                continue;
            }
            dead_rules++;
            if (rule.selector.empty()) {
                selector_dead_rules++;
            }
            if (rule.styles.empty()) {
                style_emptied_rules++;
            }
        }
    }

    CHECK(themes == 55);
    // Guards against the extractor silently dropping targets.
    CHECK(rules == 2396);

    // Pins the exact fallout of the two tolerated, reported exceptions
    // (spec §7.6) against the real corpus. The single offender for both
    // categories lives in LiquidGlass2.json:
    //   - one rule ("SnapLayout.SnapLayoutControl#SuggestionSnapLayout
    //     Windows.UI.Xaml.Controls.Border#LayoutBorder") glues two matchers
    //     with a space instead of '>' - its one selector chain is dropped,
    //     leaving `selector` empty;
    //   - four rules (#DisplayName, #Iconlmage, #TimeInnerTextBlock,
    //     #DateInnerTextBlock) carry a literal empty style string as their
    //     only style - `styles` ends up empty.
    // If any of these move, something changed in either the extractor or
    // the loader's tolerance and needs a human look, not a bumped number.
    CHECK(dead_rules == 5);
    CHECK(selector_dead_rules == 1);
    CHECK(style_emptied_rules == 4);
    CHECK(diagnostics == 5);
}
