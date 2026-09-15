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

    # A transient fetch failure (network blip, upstream hiccup) returns ""
    # indistinguishable from a real 404. Without this, a rerun during an
    # outage would silently blank out every previously-resolved credit:
    # never overwrite a known-good entry with an empty one.
    credits_path = themes_dir / "credits.json"
    previous: dict[str, str] = {}
    if credits_path.exists():
        try:
            previous = json.loads(credits_path.read_text(encoding="utf-8"))
        except (json.JSONDecodeError, OSError):
            previous = {}

    credits: dict[str, str] = {}
    misses: list[str] = []
    for theme_id in ids:
        author = fetch(theme_id)
        if not author and previous.get(theme_id):
            print(f"  keeping previous author for {theme_id} "
                  "(this fetch came back empty)", file=sys.stderr)
            author = previous[theme_id]
        credits[theme_id] = author
        if not author:
            misses.append(theme_id)
        print(f"{theme_id}: {author or '(not found)'}")

    credits_path.write_text(
        json.dumps(credits, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8")

    write_themes_md(credits)

    print(f"\n{len(credits)} themes, THEMES.md written")
    print(f"authors found: {len(credits) - len(misses)}, missing: {len(misses)}")
    if misses:
        print("missing authors for:")
        for theme_id in misses:
            print(f"  - {theme_id}")
    return 0


def write_themes_md(credits: dict[str, str]) -> None:
    resolved = sum(1 for author in credits.values() if author)
    lines = [
        "# Temas",
        "",
        "Os temas JSON foram extraidos das tabelas do mod "
        "[windows-11-taskbar-styler](https://github.com/ramensoftware/windhawk-mods)"
        ", versao 1.9 vendorizada neste projeto, sob GPL-3.0-only. O "
        "[guia de estilos](https://github.com/ramensoftware/windows-11-taskbar-styling-guide)"
        " foi consultado para creditos e referencias; esta declaracao nao atribui"
        " licenca ao guia nem as suas imagens. Credito de cada autor abaixo.",
        "",
        f"{resolved} de {len(credits)} autores resolvidos.",
        "",
        "| Tema | Autor |",
        "|---|---|",
    ]
    for theme_id, author in credits.items():
        if author:
            base = theme_id.split("_variant_")[0]
            link = ("https://github.com/ramensoftware/"
                    f"windows-11-taskbar-styling-guide/blob/main/Themes/{base}/README.md")
            name_cell = f"[{theme_id}]({link})"
        else:
            # No author means the fetch found no matching styling-guide
            # page (see fetch()) - linking would point at a README that
            # does not exist.
            name_cell = theme_id
        lines.append(f"| {name_cell} | {author or '—'} |")

    lines += [
        "",
        "`—`: autor nao encontrado no guia de estilos (variante sem pagina "
        "propria, ou tema sem entrada no guia).",
    ]

    Path("THEMES.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    raise SystemExit(main())
