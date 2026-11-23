"""Fills the V2 report template with numbers from the committed results (build spec §12.4, N8).

    py -3 tools/report_v2/fill_numbers.py

Reads docs/report_v2/report.src.html and writes docs/report_v2/report.html and
docs/report_v2/provenance.csv. Every number in the prose is a placeholder that names its source,
so it can be traced to one row and column of a CSV under results/:

    {{run:ID:column|fmt}}            row ID of results/v2/csv/all_runs.csv
    {{sup:ID:column|fmt}}            row ID of results/v2_superseded/amg_theta008/runs (θ₀ = 0.08)
    {{e5:SYSTEM:SOLVER:column|fmt}}  results/v2/csv/E5_load_cases.csv
    {{tta:SYSTEM:SOLVER:column|fmt}} results/v2/csv/E2_time_to_accuracy.csv
    {{ratio:ID1:col1:ID2:col2|fmt}}  value of the first run over the second (all_runs.csv)
    {{diff:ID1:col1:ID2:col2|fmt}}   value of the first run minus the second (all_runs.csv)
    {{slope:SOLVER:column|fmt}}      least-squares slope of log(column) vs log(DOF) over the
                                     converged E2 rows of SOLVER (all_runs.csv, E2-HS7-s*-SOLVER)
    {{status:STATUS}}                number of runs with that outcome (results/v2/numbers.json)
    {{numbers:runs}}                 total runs (results/v2/numbers.json)
    {{fig:NAME}}                     docs/report_v2/fig/NAME.svg inlined (renders resolve to fig/)
    {{table:E2|E3|E4|E0}}            a full results table built from all_runs.csv (appendix)
    {{provenance}}                   the appendix table of every number above

fmt is a chain of steps separated by '|': '*1000', '/1e9' (scale), 'int' (thousands with a thin
space), 'sigN' (N significant digits), 'e' (1.2e-5 style) or a printf format such as '%.2f'.
The script fails on an unknown placeholder, a missing row or column, or a non-numeric value.
"""

import csv
import html
import json
import math
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
RES = ROOT / "results" / "v2"
SUP = ROOT / "results" / "v2_superseded" / "amg_theta008" / "runs"
SRC = ROOT / "docs" / "report_v2" / "report.src.html"
OUT = ROOT / "docs" / "report_v2" / "report.html"
PROV = ROOT / "docs" / "report_v2" / "provenance.csv"
FIG = ROOT / "docs" / "report_v2" / "fig"

THIN = " "


def read_csv(path):
    with open(path, newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


RUNS = {r["id"]: r for r in read_csv(RES / "csv" / "all_runs.csv")}
E5 = {(r["system"], r["solver"]): r for r in read_csv(RES / "csv" / "E5_load_cases.csv")}
TTA = {(r["system"], r["solver"]): r for r in read_csv(RES / "csv" / "E2_time_to_accuracy.csv")}
NUMBERS = json.loads((RES / "numbers.json").read_text(encoding="utf-8"))


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


def run_value(rid, col, table=None):
    if table is None:
        row = RUNS.get(rid)
        src = "results/v2/csv/all_runs.csv"
    else:
        p = SUP / f"{rid}.csv"
        if not p.exists():
            fail(f"superseded run {rid} not found")
        row = read_csv(p)[0]
        src = f"results/v2_superseded/amg_theta008/runs/{rid}.csv"
    if row is None:
        fail(f"run {rid} not in all_runs.csv")
    if col not in row:
        fail(f"run {rid}: no column {col}")
    return number(row[col], f"{rid}.{col}"), src


def fmt_value(v, steps):
    out = None
    for s in steps:
        if s.startswith("*"):
            v *= float(s[1:])
        elif s.startswith("/"):
            v /= float(s[1:])
        elif s == "int":
            out = f"{round(v):,}".replace(",", THIN)
        elif s.startswith("sig"):
            n = int(s[3:])
            if v == 0:
                out = "0"
            else:
                digits = n - 1 - int(math.floor(math.log10(abs(v))))
                r = round(v, digits)
                out = f"{r:.{max(digits, 0)}f}"
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


def slope(solver, col):
    pts, ids = [], []
    for rid, r in RUNS.items():
        m = re.fullmatch(r"E2-HS7-s[0-9.]+-" + re.escape(solver), rid)
        if m and r["solve_status"] == "Converged" and r["setup_status"] == "Converged":
            pts.append((math.log(number(r["dof"], rid)), math.log(number(r[col], rid))))
            ids.append(rid)
    if len(pts) < 3:
        fail(f"slope {solver}: only {len(pts)} points")
    mx = sum(p[0] for p in pts) / len(pts)
    my = sum(p[1] for p in pts) / len(pts)
    b = sum((x - mx) * (y - my) for x, y in pts) / sum((x - mx) ** 2 for x, _ in pts)
    return b, ids


PROVENANCE = {}

SOLVERS = ["DIRECT-ND", "DIRECT-RCM", "PCG-J", "PCG-IC0", "AMG", "AMG-PCG"]


def cell(rid):
    r = RUNS.get(rid)
    if r is None:
        return "–"
    if r["setup_status"] == "OverBudget":
        return "over 8 GB"
    if r["solve_status"] != "Converged" or r["setup_status"] != "Converged":
        return (r["solve_status"] or r["setup_status"]).lower()
    t = fmt_value(number(r["total_s_median"], rid), ["sig3"])
    it = r["iterations"]
    return t if it in ("", "0") else f"{t} / {it}"


def results_table(exp):
    if exp == "E2":
        keys = sorted({r["system"] for r in RUNS.values() if r["experiment"] == "E2" and r["role"] == "solve"},
                      key=lambda k: -float(k.split("-s")[1]))
        rows = [(f"s = {k.split('-s')[1]} mm, {fmt_value(number(RUNS['E2-' + k + '-PCG-J']['dof'], k), ['int'])} DOF",
                 lambda solver, k=k: f"E2-{k}-{solver}") for k in keys]
        head = "HS-7 mesh"
    elif exp == "E3":
        ids = sorted({rid.rsplit("-s0.500-", 1)[0] for rid in RUNS if rid.startswith("E3-HS7-")})
        rows = [(i[len("E3-HS7-"):] or "baseline", lambda solver, i=i: f"{i}-s0.500-{solver}") for i in ids]
        head = "E3 variant (s = 0.5 mm)"
    elif exp == "E4":
        rows = [(d, lambda solver, d=d: f"E4-{d}-s1.000-{solver}") for d in ["plate", "fins", "HS7", "pinfin", "bar", "cube"]]
        head = "Design (s = 1 mm)"
    elif exp == "E0":
        rows = [(f"{o} s = {s} mm", lambda solver, o=o, s=s: f"E0-{o}-s{s}-{solver}")
                for o, s in [("bar", "10.000"), ("bar", "5.000"), ("bar", "2.500"),
                             ("cube", "5.802"), ("cube", "2.901"), ("cube", "1.934")]]
        head = "Object"
    else:
        fail(f"unknown table {exp}")
    out = [f'<table class="res"><tr><th>{head}</th>' + "".join(f'<th class="n">{s}</th>' for s in SOLVERS) + "</tr>"]
    for label, rid in rows:
        out.append(f"<tr><td>{html.escape(label)}</td>" + "".join(f'<td class="n">{cell(rid(s))}</td>' for s in SOLVERS) + "</tr>")
    out.append("</table>")
    PROVENANCE.setdefault(f"table:{exp}", (f"table {exp}", "results/v2/csv/all_runs.csv",
                                            f"all {exp} solve rows", "total_s_median / iterations"))
    return "\n".join(out)


def resolve(token):
    body, *steps = token.split("|")
    kind, _, rest = body.partition(":")
    parts = rest.split(":")
    if kind == "fig":
        p = FIG / f"{rest}.svg"
        if not p.exists():
            fail(f"figure {rest} not found")
        svg = p.read_text(encoding="utf-8")
        svg = re.sub(r"^<\?xml[^>]*>\s*", "", svg)
        svg = svg.replace('href="' + rest + "-", 'href="fig/' + rest + "-")
        # Inlined SVGs share one document: make element ids unique per figure.
        svg = re.sub(r'id="([^"]+)"', lambda m: f'id="{rest}-{m.group(1)}"', svg)
        svg = re.sub(r"url\(#([^)]+)\)", lambda m: f"url(#{rest}-{m.group(1)})", svg)
        svg = re.sub(r"<title>.*?</title>\n?", "", svg, count=1, flags=re.S)
        return svg, None
    if kind == "provenance":
        return None, None
    if kind == "table":
        return results_table(rest), None
    if kind == "run" and len(parts) == 2:
        v, src = run_value(parts[0], parts[1])
        where = (src, parts[0], parts[1])
    elif kind == "sup" and len(parts) == 2:
        v, src = run_value(parts[0], parts[1], table="sup")
        where = (src, parts[0], parts[1])
    elif kind == "e5" and len(parts) == 3:
        row = E5.get((parts[0], parts[1]))
        if row is None or parts[2] not in row:
            fail(f"E5 {parts}: not found")
        v = number(row[parts[2]], token)
        where = ("results/v2/csv/E5_load_cases.csv", f"{parts[0]} {parts[1]}", parts[2])
    elif kind == "tta" and len(parts) == 3:
        row = TTA.get((parts[0], parts[1]))
        if row is None or parts[2] not in row:
            fail(f"time-to-accuracy {parts}: not found")
        v = number(row[parts[2]], token)
        where = ("results/v2/csv/E2_time_to_accuracy.csv", f"{parts[0]} {parts[1]}", parts[2])
    elif kind == "ratio" and len(parts) == 4:
        a, _ = run_value(parts[0], parts[1])
        b, _ = run_value(parts[2], parts[3])
        v = a / b
        where = ("results/v2/csv/all_runs.csv", f"{parts[0]} / {parts[2]}", f"{parts[1]} / {parts[3]}")
    elif kind == "diff" and len(parts) == 4:
        a, _ = run_value(parts[0], parts[1])
        b, _ = run_value(parts[2], parts[3])
        v = a - b
        where = ("results/v2/csv/all_runs.csv", f"{parts[0]} − {parts[2]}", f"{parts[1]} − {parts[3]}")
    elif kind == "slope" and len(parts) == 2:
        v, ids = slope(parts[0], parts[1])
        where = ("results/v2/csv/all_runs.csv", f"{len(ids)} E2 rows of {parts[0]} (fit)",
                 f"log {parts[1]} vs log dof")
    elif kind == "status" and len(parts) == 1:
        v = number(NUMBERS["per_status"].get(rest, 0), token)
        where = ("results/v2/numbers.json", "per_status", rest)
    elif kind == "numbers" and rest == "runs":
        v = number(NUMBERS["runs"], token)
        where = ("results/v2/numbers.json", "runs", "runs")
    else:
        fail(f"unknown placeholder {{{{{token}}}}}")
    text = fmt_value(v, steps or [])
    PROVENANCE.setdefault(token, (text, *where))
    return text, where


def provenance_table():
    rows = sorted(PROVENANCE.items(), key=lambda kv: (kv[1][1], kv[1][2], kv[1][3]))
    out = ['<table class="prov"><tr><th>Value</th><th>File</th><th>Run / row</th><th>Column</th></tr>']
    for _, (text, src, rid, col) in rows:
        out.append(f"<tr><td class=\"n\">{html.escape(text)}</td><td>{html.escape(src.replace('results/', ''))}</td>"
                   f"<td>{html.escape(rid)}</td><td>{html.escape(col)}</td></tr>")
    out.append("</table>")
    return "\n".join(out)


def main():
    src = SRC.read_text(encoding="utf-8")
    pat = re.compile(r"\{\{([^{}]+)\}\}")
    filled = pat.sub(lambda m: resolve(m.group(1).strip())[0] or m.group(0), src)
    filled = filled.replace("{{provenance}}", provenance_table())
    left = pat.findall(filled)
    if left:
        fail(f"unresolved placeholders: {left[:5]}")
    OUT.write_text(filled, encoding="utf-8", newline="\n")
    with open(PROV, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f, lineterminator="\n")
        w.writerow(["placeholder", "value", "file", "run_or_row", "column"])
        for token, (text, s, rid, col) in sorted(PROVENANCE.items()):
            w.writerow([token, text, s, rid, col])
    print(f"{OUT.relative_to(ROOT)}: {len(PROVENANCE)} numbers filled; provenance in {PROV.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
