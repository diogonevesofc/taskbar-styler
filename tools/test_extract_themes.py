# SPDX-License-Identifier: GPL-3.0-or-later
"""Extractor tests. Run with: python -m pytest tools/ -q"""
import extract_themes as ex

SAMPLE = r'''
const Theme g_themeSample = {{
    ThemeTargetStyles{L"Grid#RootGrid > Rectangle#Fill", {
        L"Fill:=$Bg",
        L"Visibility=Collapsed"}},
    ThemeTargetStyles{L"Border", {
        L"CornerRadius=14"}},
}, {
    L"Bg=<WindhawkBlur BlurAmount=\"18\" TintColor=\"#25323232\"/>",
}};
'''


def test_parses_targets_and_styles():
    tables = ex.parse_source(SAMPLE)
    t = tables["Sample"]
    assert len(t.targets) == 2
    assert t.targets[0][0] == "Grid#RootGrid > Rectangle#Fill"
    assert t.targets[0][1] == ["Fill:=$Bg", "Visibility=Collapsed"]
    assert t.targets[1][1] == ["CornerRadius=14"]


def test_unescapes_embedded_quotes():
    tables = ex.parse_source(SAMPLE)
    const = tables["Sample"].constants[0]
    assert const == 'Bg=<WindhawkBlur BlurAmount="18" TintColor="#25323232"/>'


def test_splits_constants_into_a_map():
    tables = ex.parse_source(SAMPLE)
    doc = ex.to_theme_json("Sample", tables["Sample"], "Sample", "")
    assert doc["constants"]["Bg"].startswith("<WindhawkBlur")


def test_splits_constants_on_the_first_equals():
    """The value may contain '='; the key, by construction, cannot."""
    bad = SAMPLE.replace(r'L"Bg=<Windhawk', r'L"B=g=<Windhawk')
    tables = ex.parse_source(bad)
    doc = ex.to_theme_json("Sample", tables["Sample"], "Sample", "")
    assert "B" in doc["constants"]
    assert doc["constants"]["B"].startswith("g=<Windhawk")


def test_rejects_a_constant_without_equals():
    bad = SAMPLE.replace(r'L"Bg=<Windhawk', r'L"Bg<Windhawk')
    tables = ex.parse_source(bad)
    import pytest
    with pytest.raises(ValueError):
        ex.to_theme_json("Sample", tables["Sample"], "Sample", "")


def test_rejects_a_duplicated_constant():
    dup = SAMPLE.replace(
        'L"Bg=<WindhawkBlur BlurAmount=\\"18\\" TintColor=\\"#25323232\\"/>",',
        'L"Bg=a", L"Bg=b",')
    tables = ex.parse_source(dup)
    import pytest
    with pytest.raises(ValueError):
        ex.to_theme_json("Sample", tables["Sample"], "Sample", "")


def test_sanitizes_the_filename_but_keeps_the_real_id():
    assert ex.safe_filename("Oversimplified&Accentuated") == \
        "Oversimplified_Accentuated"
    assert ex.safe_filename("WinXP_variant_Zune") == "WinXP_variant_Zune"

    tables = ex.parse_source(SAMPLE)
    doc = ex.to_theme_json("Sample", tables["Sample"],
                           "Oversimplified&Accentuated", "")
    assert doc["id"] == "Oversimplified&Accentuated"


def test_finds_selectable_ids_across_line_breaks():
    src = '''
    } else if (wcscmp(themeName,
                      L"OS26_Liquid_Glass_variant_DarkMacDockCompact") == 0) {
        theme = &g_themeOS26_Liquid_Glass_variant_DarkMacDockCompact;
    } else if (wcscmp(themeName, L"Oversimplified&Accentuated") == 0) {
        theme = &g_themeOversimplified_Accentuated;
    '''
    ids = ex.selectable_ids(src)
    assert ids["OS26_Liquid_Glass_variant_DarkMacDockCompact"] == \
        "OS26_Liquid_Glass_variant_DarkMacDockCompact"
    assert ids["Oversimplified_Accentuated"] == "Oversimplified&Accentuated"
