# SPDX-License-Identifier: GPL-3.0-or-later
"""Extractor tests. Run with: python -m pytest tools/ -q"""
from pathlib import Path

import extract_themes as ex

VENDOR_SOURCE = (Path(__file__).resolve().parent.parent / "vendor" /
                  "upstream" / "windows-11-taskbar-styler.wh.cpp")

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


def test_selectable_ids_resolves_a_squircle_shaped_ternary_to_the_base_struct():
    """Squircle's real branch names the variant pointer first in a ternary:

        theme = IsOsFeatureEnabled(...).value_or(false)
                    ? &g_themeSquircle_variant_WeatherOnTheRight
                    : &g_themeSquircle;

    A "first pointer after the wcscmp wins" scan binds the id to the wrong
    struct here. The base id must resolve to the base struct, not the
    variant one that happens to appear first in the text.
    """
    src = '''
    } else if (wcscmp(themeName, L"Widget") == 0) {
        theme = IsOsFeatureEnabled(123).value_or(false)
                    ? &g_themeWidget_variant_Extra
                    : &g_themeWidget;
    } else if (wcscmp(themeName, L"Other") == 0) {
        theme = &g_themeOther;
    '''
    ids = ex.selectable_ids(src)
    assert ids["Widget"] == "Widget"
    assert "Widget_variant_Extra" not in ids
    assert ids["Other"] == "Other"


def test_accepts_an_at_sign_in_constant_names():
    """Real upstream keys include state-suffixed names like Accent1@Dark."""
    pairs = ex._split_pairs(
        ["Accent1@Dark={ThemeResource SystemAccentColorLight3}"])
    assert pairs["Accent1@Dark"] == "{ThemeResource SystemAccentColorLight3}"


def test_does_not_trim_constant_values():
    """Ruling 10: the JSON is a faithful transliteration, so Task 6's
    byte-for-byte round-trip stays possible. Trimming belongs to constant
    resolution in the TAP (Plano 2), not to this converter. `mainRadius =
    8` (with the surrounding spaces) is one of 64 real entries in the
    vendored source that have whitespace around '='; the value must come
    out as ' 8', not '8'."""
    assert ex._split_pairs(["mainRadius = 8"])["mainRadius"] == " 8"


def test_span_covers_the_whole_statement():
    tables = ex.parse_source(SAMPLE)
    a, b = tables["Sample"].span
    assert SAMPLE[a:b].startswith("const Theme g_themeSample = {{")
    assert SAMPLE[a:b].endswith("};")


SAMPLE_THREE_BLOCKS = r'''
const Theme g_themeThreeBlocks = {{
    ThemeTargetStyles{L"Border", {
        L"CornerRadius:=$mainRadius"}},
}, {
    L"mainRadius=8",
}, {
    L"AccentColor=blue",
}};
'''


def test_populates_constants_and_resource_variables_from_separate_blocks():
    tables = ex.parse_source(SAMPLE_THREE_BLOCKS)
    t = tables["ThreeBlocks"]
    assert t.constants == ["mainRadius=8"]
    assert t.resource_variables == ["AccentColor=blue"]


def test_squircle_gets_the_os_feature_variant_field():
    tables = ex.parse_source(SAMPLE)
    doc = ex.to_theme_json("Sample", tables["Sample"], "Squircle", "")
    assert doc["osFeatureVariant"] == {
        "featureId": 48660958,
        "themeId": "Squircle_WeatherOnTheRight",
    }


def test_decodes_uxxxx_escapes():
    """The vendored source encodes non-ASCII glyphs (Segoe Fluent
    Icons) as a backslash followed by 'u' and 4 hex digits; these
    must decode to the real character, not the six literal
    characters '\\uXXXX'."""
    src = ('\n'
           'const Theme g_themeIcon = {{\n'
           '    ThemeTargetStyles{L"TextBlock#Icon", {\n'
           '        L"Text=\\uE971"}},\n'
           '}, {\n'
           '}};\n')
    assert "\\uE971" in src  # sanity: a real backslash, not the decoded char
    tables = ex.parse_source(src)
    styles = tables["Icon"].targets[0][1]
    assert styles == ["Text=" + chr(0xE971)]


def test_rejects_an_unknown_escape_sequence():
    """Only \\", \\\\ and \\uXXXX occur in the vendored source. Any other
    escape must fail loudly instead of being silently swallowed."""
    src = r'''
const Theme g_themeBad = {{
    ThemeTargetStyles{L"Border", {
        L"Foo=\q"}},
}, {
}};
'''
    import pytest
    with pytest.raises(ValueError):
        ex.parse_source(src)


def test_roundtrip_of_the_sample():
    tables = ex.parse_source(SAMPLE)
    table = tables["Sample"]
    start, end = table.span
    assert ex.emit_theme_table("Sample", table) == SAMPLE[start:end]


def test_roundtrip_of_every_real_theme():
    """The real proof: all 55 tables in the upstream source."""
    text = VENDOR_SOURCE.read_text(encoding="utf-8")
    tables = ex.parse_source(text)
    assert len(tables) == 55

    for name, table in tables.items():
        start, end = table.span
        assert ex.emit_theme_table(name, table) == text[start:end], name


def test_real_source_has_55_tables_and_54_selectable_ids():
    """Integration check against the actual vendored source: 55 theme
    tables, 54 selectable ids, and exactly one struct with no selectable
    id — Squircle_variant_WeatherOnTheRight, the osFeatureVariant target
    of Squircle."""
    text = VENDOR_SOURCE.read_text(encoding="utf-8")
    tables = ex.parse_source(text)
    ids = ex.selectable_ids(text)
    assert len(tables) == 55
    assert len(ids) == 54
    unmapped = set(tables) - set(ids)
    assert unmapped == {"Squircle_variant_WeatherOnTheRight"}
