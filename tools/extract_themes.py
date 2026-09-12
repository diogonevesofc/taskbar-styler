#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Converts the upstream mod's C++ theme tables to JSON, and back.

The `roundtrip` command exists to prove the conversion is lossless: it
reconstructs each table with the internal `emit_theme_table` function and
checks emit(parse(x)) == x byte for byte. There is no standalone `emit` CLI
mode - only `convert` (C++ -> JSON) and `roundtrip` are exposed.
"""
from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass, field
from pathlib import Path

THEME_START = re.compile(r"^const Theme g_theme([A-Za-z0-9_&]+) = \{\{",
                         re.MULTILINE)


@dataclass
class ThemeTable:
    name: str
    targets: list[tuple[str, list[str]]] = field(default_factory=list)
    constants: list[str] = field(default_factory=list)
    resource_variables: list[str] = field(default_factory=list)
    span: tuple[int, int] = (0, 0)


_HEX4 = re.compile(r"[0-9A-Fa-f]{4}")


def _read_wide_literals(text: str, start: int, end: int) -> list[str]:
    """Extracts every L"..." literal in a region, honoring escapes.

    The vendored source uses exactly three escape forms inside L"..."
    literals: `\\"`, `\\\\`, and `\\uXXXX` (4 hex digits, used for non-ASCII
    glyph codepoints such as Segoe Fluent Icons private-use characters).
    Any other escape raises instead of being silently swallowed, so a
    future upstream escape form breaks the build loudly rather than
    corrupting a theme's selectors or style values.
    """
    out: list[str] = []
    i = start
    while i < end:
        if text.startswith('L"', i):
            i += 2
            buf: list[str] = []
            while i < end:
                c = text[i]
                if c == "\\":
                    nxt = text[i + 1]
                    if nxt == '"':
                        buf.append('"')
                        i += 2
                    elif nxt == "\\":
                        buf.append("\\")
                        i += 2
                    elif nxt == "u":
                        digits = text[i + 2:i + 6]
                        if not _HEX4.fullmatch(digits):
                            raise ValueError(
                                f"malformed \\u escape: {text[i:i + 6]!r}")
                        buf.append(chr(int(digits, 16)))
                        i += 6
                    else:
                        raise ValueError(
                            f"unknown escape sequence: {text[i:i + 2]!r}")
                    continue
                if c == '"':
                    i += 1
                    break
                buf.append(c)
                i += 1
            out.append("".join(buf))
            continue
        i += 1
    return out


def _find_matching(text: str, open_pos: int) -> int:
    """Index of the closing brace matching text[open_pos] == '{'."""
    depth = 0
    i = open_pos
    while i < len(text):
        c = text[i]
        if c == '"':
            i += 1
            while i < len(text):
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == '"':
                    break
                i += 1
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise ValueError("unclosed brace")


def parse_source(text: str) -> dict[str, ThemeTable]:
    tables: dict[str, ThemeTable] = {}

    for m in THEME_START.finditer(text):
        name = m.group(1)
        outer_open = text.index("{", m.end() - 2)
        outer_close = _find_matching(text, outer_open)
        end = text.index(";", outer_close) + 1

        table = ThemeTable(name=name, span=(m.start(), end))

        # First block: the targets.
        targets_open = outer_open + 1
        targets_close = _find_matching(text, targets_open)

        pos = targets_open
        while True:
            idx = text.find("ThemeTargetStyles{", pos)
            if idx == -1 or idx > targets_close:
                break
            brace = idx + len("ThemeTargetStyles")
            close = _find_matching(text, brace)
            literals = _read_wide_literals(text, brace, close)
            table.targets.append((literals[0], literals[1:]))
            pos = close + 1

        # Following blocks: constants and resource variables.
        rest = text[targets_close + 1:outer_close]
        blocks: list[list[str]] = []
        pos = 0
        while True:
            open_idx = rest.find("{", pos)
            if open_idx == -1:
                break
            close_idx = _find_matching(rest, open_idx)
            blocks.append(_read_wide_literals(rest, open_idx, close_idx))
            pos = close_idx + 1

        if len(blocks) > 0:
            table.constants = blocks[0]
        if len(blocks) > 1:
            table.resource_variables = blocks[1]

        tables[name] = table

    return tables


def selectable_ids(text: str) -> dict[str, str]:
    """Struct name -> selectable id.

    The comparisons are sometimes split across two lines, so whitespace is
    normalised before matching. For each `wcscmp(themeName, L"Id")`, the
    corresponding struct is looked up in the text that follows: usually the
    struct name is the id itself (with '&' standing for '_', as in
    `Oversimplified&Accentuated` -> `g_themeOversimplified_Accentuated`), so
    that exact reference is tried first. This matters for branches like
    Squircle's, where an `IsOsFeatureEnabled(...)` ternary references two
    struct pointers (the plain one and its `osFeatureVariant` target) before
    the id's own struct is reached — a "first match wins" scan would bind
    the id to the wrong struct. Only when no exact reference is found do we
    fall back to the first `&g_themeXxx;` pointer that appears afterwards.
    """
    flat = re.sub(r"\s+", " ", text)

    out: dict[str, str] = {}
    for m in re.finditer(r'wcscmp\(themeName, L"([^"]*)"\)', flat):
        theme_id = m.group(1)
        # 500 chars comfortably covers the longest branch bodies observed
        # (including multi-line comments and a feature-flag ternary) while
        # each branch's own struct reference is claimed before scanning
        # spills into the next `else if`.
        window = flat[m.end():m.end() + 500]
        candidate = theme_id.replace("&", "_")
        exact = re.search(r"&g_theme" + re.escape(candidate) + r";", window)
        if exact:
            out[candidate] = theme_id
            continue
        s = re.search(r"&g_theme([A-Za-z0-9_&]+);", window)
        if s:
            out[s.group(1)] = theme_id
    return out


def safe_filename(theme_id: str) -> str:
    """Filesystem-safe name. The real id lives in the JSON's 'id' field.

    `Oversimplified&Accentuated` is a real id; the '&' is legal on Windows
    but gets in the way in a shell and in CI.
    """
    return re.sub(r"[^A-Za-z0-9_.-]", "_", theme_id)


_CONSTANT_KEY = re.compile(r"^[A-Za-z0-9_@]+$")


def _split_pairs(entries: list[str]) -> dict[str, str]:
    out: dict[str, str] = {}
    for entry in entries:
        if "=" not in entry:
            raise ValueError(f"constant without '=': {entry!r}")
        key, value = entry.split("=", 1)
        key = key.strip()
        value = value.strip()
        # Upstream trims both sides of a constant/resourceVariable entry
        # (TrimStringView on both `key` and `valueRaw`, vendor:18584-18585,
        # 19028-19029, 19042) - trimming the value here just matches that,
        # symmetrically with the key strip two lines up. This does NOT touch
        # `emit_theme_table`'s round-trip: that function reads
        # `table.constants` / `table.resource_variables` directly (the raw
        # literal lists), never this split map, so trimming here changes zero
        # bytes of the round-trip's output.
        # A real constant name is a plain identifier. Entries mangled to
        # have no top-level '=' (see test_rejects_a_constant_without_equals)
        # still contain a later '=' nested in an attribute (e.g.
        # `BlurAmount="18"`), so checking for the mere presence of '=' is
        # not enough — the key found before that '=' must also look like a
        # real name, not a fragment of markup.
        if not _CONSTANT_KEY.match(key):
            raise ValueError(f"constant with invalid name: {entry!r}")
        if key in out:
            raise ValueError(f"duplicated constant: {key!r}")
        out[key] = value
    return out


def to_theme_json(name: str, table: ThemeTable, theme_id: str,
                  author: str) -> dict:
    doc: dict = {
        "id": theme_id,
        "name": theme_id,
        "author": author,
        "constants": _split_pairs(table.constants),
        "resourceVariables": _split_pairs(table.resource_variables),
        "rules": [
            {"target": target, "styles": styles}
            for target, styles in table.targets
        ],
    }
    if theme_id == "Squircle":
        doc["osFeatureVariant"] = {
            "featureId": 48660958,
            "themeId": "Squircle_WeatherOnTheRight",
        }
    return doc


def _escape_wide(s: str) -> str:
    """Reverses `_read_wide_literals`'s decoding, byte for byte.

    The vendored source's 55 theme spans use exactly three escape forms
    (measured across all 9933 literals therein): `\\"`, `\\\\`, and
    `\\uXXXX` with 4 uppercase hex digits for every character above
    U+007F. Nothing else is escaped.
    """
    out: list[str] = []
    for c in s:
        if c == "\\":
            out.append("\\\\")
        elif c == '"':
            out.append('\\"')
        elif ord(c) > 127:
            out.append(f"\\u{ord(c):04X}")
        else:
            out.append(c)
    return "".join(out)


def emit_theme_table(name: str, table: ThemeTable) -> str:
    """Reconstructs the exact C++ literal `parse_source` extracted `table`
    from. This is the other half of the round-trip: `emit(parse(x)) == x`
    is what proves the JSON conversion is lossless.

    Upstream formatting quirks this has to reproduce exactly:

    - Every `ThemeTargetStyles{...}` entry ends with a literal `}},`, with
      no trailing comma before it after the last style in its list (styles
      are joined by `,\\n`, and `}},` is appended directly to the last one)
      - this holds for the last target in the table too, not just interior
        ones.
    - The block separator `}, {` is emitted only when a trailing block
      actually follows. A theme's table has 0, 1, or 2 trailing blocks
      (constants only, or constants + resourceVariables) - never emit a
      `}, {` that has nothing after it, or the theme falls into the
      "0 trailing blocks" case (18 of the 55 real tables) and a spurious
      empty block would appear where upstream has none.
    - Each entry inside a trailing block (constants or resourceVariables)
      DOES get a trailing comma, including the last entry in the block.
    - The whole table always closes with `}};` on its own trailing text
      (no newline after it - `ThemeTable.span` ends right after the `;`).
    """
    lines = [f"const Theme g_theme{name} = {{{{"]

    for target, styles in table.targets:
        lines.append(f'    ThemeTargetStyles{{L"{_escape_wide(target)}", {{')
        body = [f'        L"{_escape_wide(s)}"' for s in styles]
        lines.append(",\n".join(body) + "}},")

    if table.constants or table.resource_variables:
        lines.append("}, {")
        for c in table.constants:
            lines.append(f'    L"{_escape_wide(c)}",')

    if table.resource_variables:
        lines.append("}, {")
        for v in table.resource_variables:
            lines.append(f'    L"{_escape_wide(v)}",')

    lines.append("}};")
    return "\n".join(lines)


def cmd_roundtrip(args: argparse.Namespace) -> int:
    import difflib

    text = Path(args.source).read_text(encoding="utf-8", errors="replace")
    tables = parse_source(text)

    # Same reasoning as cmd_convert's guard: THEME_START depends on exact
    # upstream formatting, so a silent reformat (or a stray missed table)
    # would make parse_source find fewer tables. Without this check, the
    # round-trip - the one command whose entire job is being the fidelity
    # proof - would report "OK" having verified zero or a partial set,
    # which is worse than not running it at all.
    if args.expect_count >= 0 and len(tables) != args.expect_count:
        print(
            f"error: expected {args.expect_count} theme tables, "
            f"found {len(tables)} - upstream formatting may have changed",
        )
        return 1

    failures = 0
    for name, table in tables.items():
        start, end = table.span
        original = text[start:end]
        emitted = emit_theme_table(name, table)
        if emitted != original:
            failures += 1
            print(f"--- MISMATCH in g_theme{name} ---")
            diff = difflib.unified_diff(
                original.splitlines(), emitted.splitlines(),
                fromfile="original", tofile="emitted", lineterm="")
            for line in list(diff)[:40]:
                print(line)
            if failures >= 3:
                print("... stopping after 3 mismatches")
                break

    if failures:
        print(f"\nFAILED: {failures} theme(s) diverged")
        return 1

    print(f"OK: {len(tables)} themes reconstructed byte for byte")
    return 0


def cmd_convert(args: argparse.Namespace) -> int:
    text = Path(args.source).read_text(encoding="utf-8", errors="replace")
    tables = parse_source(text)
    ids = selectable_ids(text)

    # THEME_START depends on exact upstream formatting (the theme region
    # sits inside `// clang-format off`). A silent reformat there would make
    # parse_source find fewer tables and this command would happily write a
    # short set. Fail loudly instead of shipping a partial conversion.
    if args.expect_count >= 0 and len(tables) != args.expect_count:
        print(
            f"error: expected {args.expect_count} theme tables, "
            f"found {len(tables)} - upstream formatting may have changed",
        )
        return 1

    credits: dict[str, str] = {}
    credits_path = Path(args.credits) if args.credits else None
    if credits_path and credits_path.exists():
        credits = json.loads(credits_path.read_text(encoding="utf-8"))

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    written = 0
    for name, table in tables.items():
        theme_id = ids.get(name, name.replace("_variant_", "_"))
        doc = to_theme_json(name, table, theme_id, credits.get(theme_id, ""))
        path = out_dir / f"{safe_filename(theme_id)}.json"
        path.write_text(
            json.dumps(doc, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8")
        written += 1

    print(f"{written} themes written to {out_dir}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("convert", help="C++ -> JSON")
    p.add_argument("--source", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--credits", default=None)
    p.add_argument(
        "--expect-count", type=int, default=55,
        help="fail if the number of parsed theme tables differs from this "
             "(default: 55); pass a negative number to disable the check")
    p.set_defaults(func=cmd_convert)

    p = sub.add_parser("roundtrip",
                       help="proves the conversion is lossless")
    p.add_argument("--source", required=True)
    p.add_argument(
        "--expect-count", type=int, default=55,
        help="fail if the number of parsed theme tables differs from this "
             "(default: 55); pass a negative number to disable the check")
    p.set_defaults(func=cmd_roundtrip)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
