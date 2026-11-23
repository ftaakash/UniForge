"""Checks the V2 report PDF before it is sent (build spec §12.4).

    py -3 tools/report_v2/check_pdf.py [docs/report_v2/UniForge_V2_Report.pdf]

Fails (exit 1) on: a page count outside 18-25, a file over 15 MB, an unresolved {{placeholder}},
a leftover [EST] or [VERIFY] marker, text that looks like source code, an e-mail address or an
absolute machine path. Needs pypdf.
"""

import pathlib
import re
import sys

from pypdf import PdfReader

ROOT = pathlib.Path(__file__).resolve().parents[2]
pdf = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "docs" / "report_v2" / "UniForge_V2_Report.pdf"

CHECKS = [
    ("unresolved placeholder", re.compile(r"\{\{[^}]*\}\}")),
    ("estimate or verify marker", re.compile(r"\[(?:EST|VERIFY)[^\]]*\]")),
    ("source code", re.compile(r"#include|std::|\bfor \(|\bwhile \(|\breturn [A-Za-z_]+;|=>\s*\{|\bconst auto\b|\bnullptr\b")),
    ("e-mail address", re.compile(r"[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)*\.[A-Za-z]{2,}")),
    ("absolute path", re.compile(r"\b[A-Za-z]:[\\/][^\s]+|/(?:home|Users)/[A-Za-z0-9._-]+/")),
]


def main():
    problems = []
    size = pdf.stat().st_size
    reader = PdfReader(str(pdf))
    pages = len(reader.pages)
    if not 18 <= pages <= 25:
        problems.append(f"{pages} pages (allowed 18-25)")
    if size > 15 * 1024 * 1024:
        problems.append(f"{size / 1e6:.1f} MB (allowed 15 MB)")
    for i, page in enumerate(reader.pages, start=1):
        text = page.extract_text() or ""
        for name, pat in CHECKS:
            for m in pat.finditer(text):
                problems.append(f"page {i}: {name}: {m.group(0)[:60]!r}")
    print(f"{pdf.name}: {pages} pages, {size / 1e6:.2f} MB")
    if problems:
        print("\n".join(problems))
        sys.exit(1)
    print("check_pdf passed: no placeholders, markers, code, e-mail addresses or paths")


if __name__ == "__main__":
    main()
