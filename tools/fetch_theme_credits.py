#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fetches each theme's author from the upstream styling-guide repo.

Runs once; the result is committed. The app never touches the network.
"""
from __future__ import annotations

import json
import re
import sys
import urllib.request
from pathlib import Path

BASE = ("https://raw.githubusercontent.com/ramensoftware/"
        "windows-11-taskbar-styling-guide/main/Themes/{}/README.md")

AUTHOR = re.compile(r"\*\*Author\*\*:\s*\[([^\]]+)\]")


def fetch(theme_id: str) -> str:
    # The guide uses the base name, without the variant suffix.
    base_name = theme_id.split("_variant_")[0]
    url = BASE.format(base_name)
    try:
        with urllib.request.urlopen(url, timeout=20) as r:
            if r.status != 200:
                return ""
            body = r.read().decode("utf-8", errors="replace")
    except Exception as exc:  # noqa: BLE001
        print(f"  warning: {theme_id}: {exc}", file=sys.stderr)
        return ""

    m = AUTHOR.search(body)
    return m.group(1) if m else ""


def main() -> int:
    themes_dir = Path("themes")
    # The filename is sanitised; the true id lives inside the JSON.
    ids = sorted(
        json.loads(p.read_text(encoding="utf-8"))["id"]
        for p in themes_dir.glob("*.json")
        if p.name != "credits.json"
    )

    credits: dict[str, str] = {}
    misses: list[str] = []
    for theme_id in ids:
        author = fetch(theme_id)
        credits[theme_id] = author
        if not author:
            misses.append(theme_id)
        print(f"{theme_id}: {author or '(not found)'}")

    (themes_dir / "credits.json").write_text(
        json.dumps(credits, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8")

    lines = [
        "# Temas",
        "",
        "Todos os temas vieram do mod "
        "[windows-11-taskbar-styler](https://github.com/ramensoftware/windhawk-mods)"
        " e do "
        "[guia de estilos](https://github.com/ramensoftware/windows-11-taskbar-styling-guide),"
        " sob GPL-3.0. Credito de cada autor abaixo.",
        "",
        "| Tema | Autor |",
        "|---|---|",
    ]
    for theme_id, author in credits.items():
        base = theme_id.split("_variant_")[0]
        link = ("https://github.com/ramensoftware/"
                f"windows-11-taskbar-styling-guide/blob/main/Themes/{base}/README.md")
        lines.append(f"| [{theme_id}]({link}) | {author or '—'} |")

    Path("THEMES.md").write_text("\n".join(lines) + "\n", encoding="utf-8")

    print(f"\n{len(credits)} themes, THEMES.md written")
    print(f"authors found: {len(credits) - len(misses)}, missing: {len(misses)}")
    if misses:
        print("missing authors for:")
        for theme_id in misses:
            print(f"  - {theme_id}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
