"""Fills the V3 report template with numbers from the committed V3 results.

    py -3 tools/report_v3/fill_numbers.py

Reads docs/report_v3/report.src.html and writes docs/report_v3/report.html and
docs/report_v3/provenance.csv. As in the V2 report, every number in the prose is a placeholder
that names its source, so it can be traced to one row and column of a file under results/v3:

    a value reference REF is one of
        ID@column                   row ID of results/v3/csv/all_runs.csv (V3A-V3F, V3H);
                                    column 'total' = setup_s + solve_s_median
        g/PHASE/COND/ROUND@column   results/v3/csv/V3G_timing.csv (e.g. g/balanced/pin/1@solve_median_s)
        petsc/CONFIG/ID@column      results/v3/csv/V3F_petsc.csv (e.g. petsc/GAMG-default/V3F-...@total_s)

    {{v:REF|fmt}}            the value
    {{ratio:REF1:REF2|fmt}}  REF1 / REF2
    {{pct:REF1:REF2|fmt}}    (REF1 / REF2 - 1) * 100, the change in per cent
    {{runs}} / {{runs:EXP}}  number of runs (results/v3/numbers.json)
    {{fig:NAME}}             docs/report_v3/fig/NAME.svg inlined
    {{provenance}}           the appendix table of every number above

fmt is a chain of steps separated by '|': '*1000', '/1e6' (scale), 'int' (thousands with a thin
space), 'sigN' (N significant digits), 'e' (1.2e-5 style), 'abs', or a printf format such as
'%.2f'. The script fails on an unknown placeholder, a missing row or column, or a non-numeric
value.
"""

import csv
import html
import json
import math
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
RES = ROOT / "results" / "v3"
SRC = ROOT / "docs" / "report_v3" / "report.src.html"
OUT = ROOT / "docs" / "report_v3" / "report.html"
PROV = ROOT / "docs" / "report_v3" / "provenance.csv"
FIG = ROOT / "docs" / "report_v3" / "fig"

THIN = " "


def read_csv(path):
    with open(path, newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


RUNS = {r["id"]: r for r in read_csv(RES / "csv" / "all_runs.csv")}
TIMING = {(r["phase"], r["cond"], r["round"]): r for r in read_csv(RES / "csv" / "V3G_timing.csv")}
PETSC = {(r["config"], r["id"]): r for r in read_csv(RES / "csv" / "V3F_petsc.csv")}
NUMBERS = json.loads((RES / "numbers.json").read_text(encoding="utf-8"))

FILES = {
    "R": "results/v3/csv/all_runs.csv",
    "G": "results/v3/csv/V3G_timing.csv",
    "P": "results/v3/csv/V3F_petsc.csv",
    "N": "results/v3/numbers.json",
}


def fail(msg):
    sys.exit("fill_numbers: " + msg)


def number(value, where):
    try:
        v = float(value)
    except (TypeError, ValueError):
        fail(f"{where}: '{value}' is not a number")
    if not math.isfinite(v):
        fail(f"{where}: not finite")
    return v


def ref(text):
    """Value of a reference, with (file code, row, column) for the provenance table."""
    if "@" not in text:
        fail(f"reference '{text}' has no @column")
    key, col = text.rsplit("@", 1)
    if key.startswith("g/"):
        parts = key.split("/")
        if len(parts) != 4:
            fail(f"timing reference '{text}': want g/PHASE/COND/ROUND@column")
        row = TIMING.get(tuple(parts[1:]))
        code, rowname = "G", " ".join(parts[1:])
    elif key.startswith("petsc/"):
        parts = key.split("/")
        if len(parts) != 3:
            fail(f"PETSc reference '{text}': want petsc/CONFIG/ID@column")
        row = PETSC.get((parts[1], parts[2]))
        code, rowname = "P", f"{parts[1]} {parts[2]}"
    else:
        row = RUNS.get(key)
        code, rowname = "R", key
    if row is None:
        fail(f"row '{key}' not found")
    if code == "R" and col == "total":
        # Total = setup + median solve, as in every table of docs/v3_experiments.md (the CSV's
        # total_s_median is the median of per-repetition totals, a slightly different number).
        v = number(row["setup_s"], text) + number(row["solve_s_median"], text)
        return v, (code, rowname, "setup_s + solve_s_median")
    if col not in row:
        fail(f"row '{key}': no column {col}")
    return number(row[col], text), (code, rowname, col)


def fmt_value(v, steps):
    out = None
    for s in steps:
        if s.startswith("*"):
            v *= float(s[1:])
        elif s.startswith("/"):
            v /= float(s[1:])
        elif s == "abs":
            v = abs(v)
        elif s == "int":
            out = f"{round(v):,}".replace(",", THIN)
        elif s.startswith("sig"):
            n = int(s[3:])
            if v == 0:
                out = "0"
            else:
                digits = n - 1 - int(math.floor(math.log10(abs(v))))
                out = f"{round(v, digits):.{max(digits, 0)}f}"
        elif s == "e":
            m, e = f"{v:.1e}".split("e")
            out = f"{m}e{int(e)}"
        elif s.startswith("%"):
            out = s % v
        else:
            fail(f"unknown format step '{s}'")
    if out is None:
        out = f"{v:g}"
    return out.replace("-", "−")


PROVENANCE = {}


def resolve(token):
    body, *steps = token.split("|")
    kind, _, rest = body.partition(":")
    if kind == "fig":
        p = FIG / f"{rest}.svg"
        if not p.exists():
            fail(f"figure {rest} not found")
        svg = p.read_text(encoding="utf-8")
        svg = re.sub(r"^<\?xml[^>]*>\s*", "", svg)
        svg = re.sub(r'id="([^"]+)"', lambda m: f'id="{rest}-{m.group(1)}"', svg)
        svg = re.sub(r"url\(#([^)]+)\)", lambda m: f"url(#{rest}-{m.group(1)})", svg)
        svg = re.sub(r"<title>.*?</title>\n?", "", svg, count=1, flags=re.S)
        return svg
    if kind == "provenance":
        return None
    if kind == "v":
        v, (code, row, col) = ref(rest)
    elif kind in ("ratio", "pct"):
        a_text, sep, b_text = rest.partition(":")
        if not sep:
            fail(f"{kind} needs two references: {token}")
        a, (ca, ra, cola) = ref(a_text)
        b, (cb, rb, colb) = ref(b_text)
        v = a / b if kind == "ratio" else (a / b - 1.0) * 100.0
        code = ca if ca == cb else f"{ca}/{cb}"
        row, col = f"{ra} / {rb}", (cola if cola == colb else f"{cola} / {colb}")
        if kind == "pct":
            col += " (change %)"
    elif kind == "runs":
        v = number(NUMBERS["per_experiment"][rest] if rest else NUMBERS["runs"], token)
        code, row, col = "N", "per_experiment" if rest else "runs", rest or "runs"
    else:
        fail(f"unknown placeholder {{{{{token}}}}}")
    text = fmt_value(v, steps or [])
    PROVENANCE.setdefault(token, (text, code, row, col))
    return text


def provenance_table():
    rows = sorted(PROVENANCE.values(), key=lambda t: (t[1], t[2], t[3]))
    legend = ("<p class=\"small\">Each number in this report, with the file under results/v3 it was "
              "filled in from, the run or row, and the column. Files: R = csv/all_runs.csv (one row "
              "per run of V3A–V3F and V3H), G = csv/V3G_timing.csv (phase, condition, round), "
              "P = csv/V3F_petsc.csv (setting, exported system), N = numbers.json. A ratio names "
              "both rows. Full list in docs/report_v3/provenance.csv.</p>")
    half = (len(rows) + 1) // 2
    tables = []
    for part in (rows[:half], rows[half:]):
        t = ['<table class="prov"><tr><th class="n">Value</th><th>F</th><th>Run / row</th><th>Column</th></tr>']
        for text, code, row, col in part:
            t.append(f"<tr><td class=\"n\">{html.escape(text)}</td><td>{code}</td>"
                     f"<td>{html.escape(row)}</td><td>{html.escape(col)}</td></tr>")
        t.append("</table>")
        tables.append("\n".join(t))
    return legend + '\n<div class="provcols"><div>' + tables[0] + "</div><div>" + tables[1] + "</div></div>"


def main():
    src = SRC.read_text(encoding="utf-8")
    pat = re.compile(r"\{\{([^{}]+)\}\}")
    filled = pat.sub(lambda m: resolve(m.group(1).strip()) or m.group(0), src)
    filled = filled.replace("{{provenance}}", provenance_table())
    left = pat.findall(filled)
    if left:
        fail(f"unresolved placeholders: {left[:5]}")
    OUT.write_text(filled, encoding="utf-8", newline="\n")
    with open(PROV, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f, lineterminator="\n")
        w.writerow(["placeholder", "value", "file", "run_or_row", "column"])
        for token, (text, code, row, col) in sorted(PROVENANCE.items()):
            w.writerow([token, text, FILES.get(code, code), row, col])
    print(f"{OUT.relative_to(ROOT)}: {len(PROVENANCE)} numbers filled; provenance in {PROV.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
