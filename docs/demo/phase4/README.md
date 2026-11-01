# Phase 4 evidence: Geometry and Thermal views

Exit criterion (spec §13): A1–A3, B1/B2 and C2/C4 UI rows green, with screenshots.

**Screenshots are not taken yet.** They need someone at the screen (the automated session could
not drive the app). Take them at 100 % display scaling with `--theme=light`, save them here
with the names below, and tick the list.

| File | What it should show |
|---|---|
| `geometry_samples.png` | Geometry after **Load samples**: rod mesh + coloured points, legend `T [°C]` on the right, bbox dims bottom-right, axes triad, ✓ STL valid / ✓ CSV valid badges, `◇ SYNTHETIC DATA` chip |
| `geometry_pick.png` | A node picked: blue ring in the viewport, NodeInspector with id, x/y/z [m], T [°C] |
| `geometry_error.png` | A malformed file (e.g. `tests/fixtures/stl/vertex_count_two.stl`) dropped on the window: ✕ alert with line number + hint; the previous model still shown |
| `thermal_T_vs_x.png` | Thermal after **Run study** with brief defaults: four FE curves (fixed colours/markers) + dashed exact curve, four ✓ badges |
| `thermal_error_vs_h.png` | Error vs h tab: log–log, measured points on the dashed slope-2 line |
| `thermal_table_export.png` | Convergence table (mono, right-aligned, pass column) and "✓ Saved convergence.csv (4 rows) · Open folder" |
| `overview_live.png` | Overview with B and C rows showing live status after the two views were used |
| `T_vs_x.png` | A PNG produced by Export ▾ → Current plot (PNG) |

## Manual checklist

Start: `build\windows-msvc\bin\Release\UniForge.exe --theme=light` (or `Launch.exe` from the
package). Repeat the Geometry part once with `--force-warp`.

Geometry (Task B)
- [ ] **Load samples** imports both files; progress shows; the camera auto-fits.
- [ ] LMB drag orbits, MMB or Shift+LMB drag pans (scene follows the cursor), wheel zooms toward the cursor.
- [ ] `F` fits, `R` resets, `W` toggles wireframe (hover the viewport); toolbar buttons do the same.
- [ ] Show mesh / Show points toggles and the point-size slider work.
- [ ] Clicking a point selects it (ring + NodeInspector); clicking empty space clears it.
- [ ] Legend min/max match the CSV summary (Tmin/Tmax).
- [ ] Browse… opens the native dialog in `data/geometry` / `data/thermal`.
- [ ] Dropping a `.stl` / `.csv` from Explorer imports it.
- [ ] Dropping a malformed fixture shows the error alert; the previous model stays.
- [ ] Cancel during a large import (`uniforge_cli import-bench` leaves none behind; generate one
      with `gen-samples --data` into a temp folder or use any big STL) keeps the previous model.
- [ ] With `--force-warp` the viewport renders (status bar says software rendering); About/Log show the backend.

Thermal (Task C)
- [ ] Brief defaults; ↺ resets each field; an invalid value (k = 0) outlines the field, explains
      the range and disables Run with the reason as tooltip.
- [ ] Unticking all meshes disables Run ("Select at least one mesh size").
- [ ] `Ctrl+Enter` runs; badges: midpoint error = qh²/8k, order ≈ 2, energy balance, T_max @ x*
      all ✓; tooltips show measured value and tolerance.
- [ ] Table: 1.250000, 3.125000e-01, 7.812500e-02, 1.953125e-02 °C; ratio 4.0000; order 2.0000.
- [ ] Header click sorts; `Ctrl+C` over the table copies TSV.
- [ ] Export ▾: convergence.csv matches `results/csv/convergence.csv` values; nodal and midpoint CSVs open; PNG saved.
- [ ] Changing a parameter and re-running updates plots; the last result stays dimmed while it runs.

Overview
- [ ] B and C rows show live status after the views were used; "Open view →" navigates.

## Not covered in Phase 4
- 200 % DPI check (Phase 8 polish), "Run all self-checks" on Overview (cut, spec cut-list item 1).
