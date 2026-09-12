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
    }

    CHECK(themes == 55);
    // Guards against the extractor silently dropping targets.
    CHECK(rules == 2396);
}
