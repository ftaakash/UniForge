# UniForge: Design Review & UI Design System

**Produced by:** `design:design-system` skill (audit → corrections → tokens → components → patterns → view blueprints)
**Inputs reviewed:** `UniForge_Analysis_and_Development_Plan.md` (Plan) and `UniForge_System_Design.md` (SysDesign)
**Status:** Design only. Nothing is built yet.

The two earlier documents cover architecture, numerics and delivery well, but they specify almost nothing about the interface itself. That matters because Interface (8) is scored on "clear workflow, readable layout, responsive status, validation and usable plots", and every other criterion is *seen* through the UI during the demo and the verification discussion. This document fixes errors found in both plans and adds the missing UI layer.

---

## Part 1: Audit of the existing plans

### 1.1 Summary
**Documents reviewed:** 2 | **Corrections found:** 12 (3 high, 5 medium, 4 low) | **UI design-system coverage of current plans:** ≈ 25/100 (no tokens, fonts, component states, copy rules or layouts)

### 1.2 Corrections

| # | Sev. | Where | Issue | Correction |
|---|---|---|---|---|
| 1 | **High** | Plan §4 Task A | `.vscode/launch.json` points at `${workspaceFolder}/dist/.../UniForge.exe`. When the evaluator opens the **extracted ZIP** in VS Code, `dist/` doesn't exist, so the "one action" run config breaks. | The ZIP root ships its own `.vscode/launch.json` targeting `${workspaceFolder}/Launch.exe` (the same packaged app, as the brief requires). The repo keeps a second config "Run packaged (dist)". `package.ps1` writes the ZIP version. |
| 2 | **High** | Both | No font specified. ImGui's built-in ProggyClean is a small bitmap-style ASCII font: it renders blurry at 150 % DPI and has **no glyphs for °, ², ³, ·, ε, Δ, −**. Every unit label (`°C`, `m²`, `W/(m·K)`) would show as `?`. | Embed **Inter** (UI) + **JetBrains Mono** (numbers/logs), both SIL OFL 1.1, as compressed arrays, with glyph ranges Basic Latin + Latin-1 + Greek + `U+2212 −`, `U+00B7 ·`, `U+2264 ≤`, `U+2192 →`. Rebuild the atlas on DPI change. Licences go in `LICENSES/`. |
| 3 | **High** | SysDesign §4.4 | Launch-to-ready measured from **UniForge.exe** creation misses the time spent in `Launch.exe`, yet the user double-clicks Launch.exe. | Launcher passes `--launch-t0=<QueryPerformanceCounter>` (system-wide clock) to the app. App reports both *Launch.exe → first frame* (headline number) and *app start → first frame*. |
| 4 | Med | SysDesign §3.2 | "MSVC only supports OpenMP 2.0" is incomplete: `/openmp:llvm` gives 3.x. The real reason to avoid it is that OpenMP adds a **runtime DLL** (`vcomp140.dll` / `libomp140.x86_64.dll`) that breaks the static, no-redistributable package. | Reword the rationale: explicit `std::thread` pool = no extra runtime DLL, explainable, TSan-checkable. |
| 5 | Med | SysDesign §3.3 | "Reduced K is SPD and diagonally dominant". Interior rows are only *weakly* dominant (2 = 1 + 1). | Say "SPD and **irreducibly** diagonally dominant (strict in rows next to the Dirichlet nodes), so Thomas without pivoting is stable". An evaluator with FE background could probe this. |
| 6 | Med | Plan §6 vs SysDesign §2.1 | Package layout disagrees: Plan omits `uniforge_cli.exe`; SysDesign puts it at the root. | Put it at `app/uniforge_cli.exe` next to the GUI, with `regenerate_evidence.cmd` at the root. That's a convenience wrapper with no logic, allowed as a packaging script. |
| 7 | Med | Both | No colour-vision-safe palette for the four FEA mesh curves; no rule for the analytical curve. With default ImPlot colours, N = 40 and N = 80 overlap and look identical. | Okabe–Ito categorical palette, distinct markers per N, exact solution as a dashed neutral line drawn **on top**, plus an error plot (log–log), which is where the meshes actually separate. |
| 8 | Med | Both | No rule for the theme. Dark-theme screenshots print poorly in the 6-page report. | Light and dark token sets; default follows Windows "app mode"; report/demo screenshots captured in light. |
| 9 | Low | SysDesign §3.4 | `std::from_chars` for `double` needs libstdc++ ≥ 11 on the Linux CI image (libc++ only from 17). | Pin CI to `ubuntu-24.04` + GCC 13 / clang with libstdc++; note in DEPENDENCIES.md. |
| 10 | Low | Both | Accessibility unstated. ImGui exposes **no UI Automation tree**, so screen readers can't read it. | Disclose in limitations; still provide full keyboard navigation, focus rings, ≥ 4.5:1 text contrast (to verify), never colour-only status. |
| 11 | Low | Plan §4 Task B | "Inferno" colormap: licence not recorded. | Matplotlib's perceptually-uniform maps (inferno/viridis, van der Walt & Smith) are CC0. Record in DEPENDENCIES.md. |
| 12 | Low | Both | The design iterations themselves aren't planned as evidence. | Keep all three design docs + this audit in `docs/design/` and reference them in `ai_usage_log.md`. A real review that found errors in AI-generated designs is exactly the "critical judgement / iterative refinement" evidence the 8-mark AI criterion asks for. |

### 1.3 What both plans got right (keep unchanged)
Core/UI separation, D3D11 + WARP, static CRT, Thomas + dense oracle, chunked worker pool with index-ordered output, atomic result publish, CLI evidence regeneration, traceability matrix, Windows Sandbox launch test in Phase 1 and 9, commit rules.

---

## Part 2: Design principles (UniForge-specific)

1. **Evidence on screen.** Every view shows its own verification result (e.g., "max midpoint error matches qh²/8k ✓") next to the output. The evaluator should never have to open a file to confirm correctness.
2. **Units everywhere.** No number without a unit; SI only; labels use the form `name [unit]`.
3. **One way to do each thing.** Same Run/Cancel bar, same export menu, same error presentation in every view.
4. **Status is never colour-only.** Every status has an icon glyph + word + colour.
5. **Calm UI.** No animations except progress; no toasts that disappear before being read. The status bar and log panel are the single source of feedback.
6. **Simulated is obvious.** Anything not real hardware wears the violet SIMULATED badge.

---

## Part 3: Design tokens

Implemented once in `src/app/ui/Theme.hpp` (a `Tokens` struct per theme → applied to `ImGuiStyle`/`ImPlotStyle`). Views never use literal colours or sizes. A lint grep in CI fails on `ImVec4(` or `IM_COL32(` outside `ui/Theme*.cpp`.

### 3.1 Colour: neutrals & brand
| Token | Light | Dark | Use |
|---|---|---|---|
| `bg.app` | `#F5F7FA` | `#111317` | Window background |
| `bg.surface` | `#FFFFFF` | `#181B21` | Cards, panels |
| `bg.surface-alt` | `#EEF1F5` | `#20242C` | Table zebra, input fill |
| `border.default` | `#D3D9E1` | `#323844` | Card/input borders |
| `border.focus` | `#2563EB` | `#5B95FF` | Keyboard focus ring (2 px) |
| `text.primary` | `#1A1E24` | `#E7EAF0` | Body |
| `text.secondary` | `#59616E` | `#9AA3B2` | Labels, units, captions |
| `text.disabled` | `#9AA1AC` | `#5E6573` | Disabled controls |
| `accent` | `#2563EB` | `#5B95FF` | Primary buttons, selected nav, links |
| `accent.hover` / `.active` | `#1D4ED8` / `#1E40AF` | `#7AA9FF` / `#3D7BF0` | States |

### 3.2 Colour: semantic
| Token | Light | Dark | Glyph | Meaning |
|---|---|---|---|---|
| `status.success` | `#15803D` | `#3FB950` | ✓ | Check passed, connected |
| `status.warning` | `#B45309` | `#D29922` | ! | Degraded (WARP render, speedup not assessable, degenerate triangles) |
| `status.error` | `#B91C1C` | `#F85149` | ✕ | Invalid input, failed import, device fault |
| `status.info` | `#0E7490` | `#39C5CF` | i | Running, neutral notice |
| `status.simulated` | `#7C3AED` | `#A78BFA` | ◇ | Mock hardware / synthetic data |

Contrast targets: text tokens ≥ 4.5:1 on `bg.surface`, semantic colours ≥ 3:1 (non-text). **To be verified with a contrast checker in Phase 8.** Values above are proposals, not measured.

### 3.3 Colour: data visualisation
| Token | Value | Use |
|---|---|---|
| `viz.series[0..3]` | `#D55E00`, `#0072B2`, `#009E73`, `#CC79A7` (Okabe–Ito) | N = 10, 20, 40, 80 (fixed mapping in every plot) |
| `viz.series[4..5]` | `#E69F00`, `#56B4E9` | Workers p = 2 / p = 4 extras, loss curve |
| `viz.reference` | `text.primary`, dashed 1.5 px | Analytical solution, slope-2 guide, ideal speedup line |
| `viz.colormap` | **inferno** (256 entries, CC0) | Temperature point cloud |
| `viz.mesh.fill` | `#9FB3C8` light / `#5D6B7A` dark | STL faces (flat shaded) |
| `viz.mesh.edge` | `text.secondary` @ 40 % | Wireframe overlay |
| `viz.pick` | `#00B3FF` ring, 2 px | Selected node highlight (outside the inferno range, so always visible) |
| `viz.viewport.bg` | gradient `bg.surface-alt` → `bg.app` | 3D background |

### 3.4 Typography (at 100 % DPI; all sizes × DPI scale)
| Token | Font | Size / weight | Use |
|---|---|---|---|
| `type.title` | Inter | 20 px SemiBold | View titles |
| `type.heading` | Inter | 16 px SemiBold | Card headers |
| `type.body` | Inter | 15 px Regular | Default text, controls |
| `type.label` | Inter | 13 px Regular, `text.secondary` | Field labels, units, axis labels |
| `type.mono` | JetBrains Mono | 14 px Regular | Numbers in tables, log panel, IDs |

Four atlas fonts only (title/heading share the SemiBold face at two sizes). Numeric table columns are right-aligned and use the mono face so decimal points line up.

### 3.5 Spacing, shape, motion
| Token | Value |
|---|---|
| `space.1..6` | 4, 8, 12, 16, 24, 32 px |
| Control height | 30 px (`FramePadding` 8×7) |
| Card padding | 16 px; gap between cards 12 px |
| `radius.control` / `radius.card` | 4 px / 6 px |
| `border.width` | 1 px (focus 2 px) |
| Elevation | none (flat; borders separate surfaces) |
| Motion | progress bar only; indeterminate stripe 1.2 s loop; no other animation |

### 3.6 Number & unit formatting rules
| Quantity | Format | Example |
|---|---|---|
| Temperature (UI) | 4 decimals | `188.2000 °C` |
| FE errors | 6 significant, matches brief tables | `1.250000 °C`, `1.953125e-02 °C` |
| Ratios / order | 4 decimals | `4.0000`, `2.0000` |
| Times | 3 significant + unit auto (µs/ms/s) | `412 ms` |
| Speedup / efficiency | 2 decimals / percent | `3.71×`, `92.8 %` |
| Agreement diff | scientific, 3 sig | `0.00e+00 °C (≤ 1e-08 ✓)` |
| Coordinates | 6 significant + `m` | `0.420000 m` |
| CSV exports | full precision (`%.17g`), ASCII only, header `name_unit` | `temperature_C` |

Label format: `Thermal conductivity k [W/(m·K)]`. Never `W/mK`.

---

## Part 4: Components

All components live in `src/app/ui/widgets/` as free functions with an explicit state struct (ImGui idiom), e.g. `bool ParamField(const char* id, ParamSpec&, double& value)`.

| Component | Purpose | Variants | States | Keyboard |
|---|---|---|---|---|
| **AppShell** | Nav rail + view area + bottom dock (Log) + status bar | — | normal, job-running (nav still usable) | `Ctrl+1..6` switch views, `F1` help |
| **NavRail** | Left list of views with task tag (A–F) and a small status dot | expanded 200 px / compact 56 px (< 1100 px wide) | default, hover, selected (accent bar), has-error dot | ↑/↓ + Enter |
| **ViewHeader** | Title, task tag chip, one-line purpose, **VerificationBadge row** | — | — | — |
| **VerificationBadge** | One self-check result, e.g. "Order ≈ 2 ✓" | pass / fail / pending / n.a. | pending = grey "–", tooltip shows the numbers and tolerance | focusable, tooltip on focus |
| **ParamField** | Numeric input with label, unit suffix, range, default, reset | scalar / integer / seed / checkbox-group (mesh sizes) | default, focus, **invalid** (red border + message below, Run disabled), modified-from-default (dot + ↺) | Tab order, Enter commits, Esc reverts |
| **FileField** | Path display + Browse + recent files + drag-drop target | — | empty ("No file loaded"), loading, loaded (name, size, counts), error | `Ctrl+O` |
| **RunBar** | Primary action + Cancel + inline progress | — | idle, running (Run → Cancel, progress + stage text + elapsed), cancelling, done (✓ + duration), failed | `Ctrl+Enter` run, `Esc` cancel |
| **StatusBar** | Global: current job, progress, last message, render backend, CPU count | — | idle / running / warning / error (icon + text) | — |
| **LogPanel** | Timestamped log with level filter, search, copy, export | collapsed (1 line) / expanded | — | `Ctrl+L` toggle |
| **InlineAlert** | Persistent message inside a card (import errors, WARP warning) | info / warning / error / simulated | dismissible only for info | — |
| **DataTable** | Sortable, zebra, mono numerics, per-row pass/fail column | — | empty state, loading skeleton (static, no shimmer) | arrows, `Ctrl+C` copies TSV |
| **PlotPanel** | ImPlot with labelled axes + units, legend, tabs, export | linear / log–log | empty ("Run to see results"), data | `Ctrl+E` export PNG |
| **Viewport3D** | Offscreen D3D11 render with toolbar: Fit, Reset, Wireframe, Points size, Axes | mesh / point-cloud / both | empty (drop-zone illustration + "Open STL…"), loading, rendered, WARP-warning | `F` fit, `R` reset, `W` wireframe; mouse: LMB orbit, MMB/Shift+LMB pan, wheel zoom |
| **ColorLegend** | Vertical inferno bar, min/max/5 ticks, unit `°C`, data range | auto range / locked range | — | — |
| **NodeInspector** | Selected node details | — | none selected ("Click a node"), selected | `Esc` clears |
| **DeviceCard** | One mock device: name, kind, SIMULATED badge, state chip, capabilities, telemetry sparkline, actions | MCU / FPGA / JTAG | Disconnected, Connecting (spinner text), Connected, Faulted (error alert + retry trail), Recovering | Tab through actions |
| **SimulatedBadge** | Violet "◇ SIMULATED" chip | small / inline | always visible, never dismissible | — |
| **ExportMenu** | Single dropdown: CSV, PNG, JSON/MD (bench), "Open results folder" | — | disabled until results exist (tooltip says why) | — |
| **EmptyState** | Icon + one sentence + primary action | — | — | Enter = primary action |

**Do / Don't**
| ✅ Do | ❌ Don't |
|---|---|
| Disable Run with a visible reason ("k must be > 0") | Silently ignore the click |
| Keep the last good result visible while a new job runs (dimmed + "updating…") | Clear plots at job start |
| Put tolerance next to every pass/fail | Show "PASS" without the numbers |
| Use the same series colour for the same N in every plot | Let ImPlot auto-assign colours |

---

## Part 5: Patterns

### 5.1 Form → Run → Result (Thermal, Performance, Backprop)
`ParamField`s (left card) → `RunBar` → results area (plots + table) → `VerificationBadge`s update in the header → `ExportMenu` enabled. Invalid input: field red + message under it + RunBar disabled with the first error as tooltip. Running: inputs stay editable but marked "changes apply on next run".

### 5.2 Import (Geometry)
`FileField` / drag-drop → RunBar shows "Parsing… 43 % (112 MB / 260 MB)" → success: InlineAlert(info) "Loaded 12 facets · 0 degenerate · bbox 1.000 × 0.010 × 0.010 m · 3 ms", auto Fit-to-view → failure: InlineAlert(error) in the card + status bar + log; **previous model stays displayed**.

### 5.3 Error message copy pattern
`<What happened> <where> — <how to fix>`, with no stack traces or error codes in the headline (the code goes in the log).
- "Line 214: expected `vertex x y z` but found `vertx 0 0 1`. The file may be truncated or hand-edited — re-export it as ASCII STL."
- "Column 5 header is `temp_C`; expected `temperature_C`. Rename the column or re-export with the schema in data/PROVENANCE.md."
- "Row 88: temperature is `nan`. Non-finite values are rejected — check the export for empty results."
- "File is empty (0 bytes). Choose an ASCII STL that contains at least one facet."
- "Binary STL detected. UniForge reads ASCII STL only — export as ASCII from your CAD tool."
- "Thermal conductivity must be greater than 0 W/(m·K)."

### 5.4 Long-job pattern
Progress with stage text + elapsed time; Cancel always available; on cancel: status "Cancelled — previous results kept"; on error: alert + log; on success: "Done in 412 ms". The benchmark also shows "Rep 3/5 · p = 2".

### 5.5 Export pattern
Save dialog defaults to `Documents\UniForge\exports\<view>_<yyyyMMdd-HHmmss>.<ext>`; writes temp → rename; status "Saved convergence.csv (4 rows)" + "Open folder" link.

### 5.6 Simulated / synthetic disclosure pattern
Hardware view banner: "All devices on this page are software simulations (Task E). No hardware is accessed." Synthetic CSV sample: the Geometry view shows a SimulatedBadge-style "SYNTHETIC DATA" chip when the file's provenance header or filename marks it so.

### 5.7 First run
App opens on **Overview**, with sample files one click away ("Load sample rod geometry", "Load sample thermal CSV (synthetic)"). The evaluator reaches a meaningful screen in one click per view.

---

## Part 6: View blueprints

### 6.0 Shell
```text
┌──────────────┬──────────────────────────────────────────────────────────────────────┐
│ UniForge     │ ViewHeader: Title · [Task C] · purpose line        [✓ badge][✓ badge]│
│              ├──────────────────────────────────────────────────────────────────────┤
│ ○ Overview   │                                                                      │
│ ● Geometry  B│                         view content                                 │
│ ● Thermal   C│                                                                      │
│ ● Performance│                                                                      │
│           D  │                                                                      │
│ ● Hardware  E│                                                                      │
│ ● Backprop  F├──────────────────────────────────────────────────────────────────────┤
│              │ Log ▾  10:22:04.512 INFO job 7 thermal.study done 12.4 ms            │
│ ⚙ About      │                                                                      │
├──────────────┴──────────────────────────────────────────────────────────────────────┤
│ ● Idle · Last: "Saved convergence.csv" · Render: D3D11 HW · CPUs 20 · Ready in 0.8 s │
└─────────────────────────────────────────────────────────────────────────────────────┘
```

### 6.1 Overview (new, small, recommended)
Six task cards (A–F), each with a one-line description, its live VerificationBadges (pending until run), and "Open view →". Plus a "Run all self-checks" button that runs FEA study + backprop checks + a quick bench preset (≈ 1 s) and lights up the badges. This makes the 3–5 min demo trivial and is pure UI over existing core calls. **No new backend features.**

### 6.2 Geometry (Task B)
```text
┌ Files ───────────────┐┌ Viewport3D ─────────────────────────────── [Fit][Reset][▦][•]┐
│ STL  rod.stl  [Browse]│                                                     ┌──────┐ │
│  12 facets · 1.0 m    ││                                                     │188.2 │ │
│ CSV  rod_T_SYNTH [..] ││          (mesh + coloured points)                   │ ▓▓▓▓ │ │
│  ◇ SYNTHETIC DATA     ││                                                     │ 20.0 │ │
│ Show: ☑Mesh ☑Points   ││                                                     │  °C  │ │
│ Point size [4 px]     ││                                                     └──────┘ │
├ Node inspector ──────┤│                                                               │
│ node_id   412        ││  axes triad                     bbox 1.000×0.010×0.010 m     │
│ x,y,z [m] 0.42,…     │└───────────────────────────────────────────────────────────────┘
│ T  188.20 °C         │
└──────────────────────┘
```
Badges: `STL valid ✓`, `CSV valid ✓`, `Non-finite rejected ✓` (from import stats).

### 6.3 Thermal Analysis (Task C)
```text
┌ Parameters ────────────┐┌ [T vs x] [Error vs h (log–log)] [Midpoint errors] ──────────┐
│ L [m]            1.0   ││  T [°C]                                                      │
│ A [m²]         1e-04   ││  200┤        ___                                             │
│ k [W/(m·K)]     10     ││     │     _-     -_      ── N=10  ── N=20                    │
│ q [W/m³]     10000     ││  100┤ _-           -_    ── N=40  ── N=80  - - exact         │
│ T(0) [°C]      100     ││     │                -_                                       │
│ T(L) [°C]       20     ││   20┼────────────────────  x [m]                              │
│ Meshes ☑10 ☑20 ☑40 ☑80  │└──────────────────────────────────────────────────────────────┘
│ ↺ Reset to brief values││
│ [▶ Run study]  [Cancel]││
└────────────────────────┘
┌ Convergence ─────────────────────────────────────────────────────────────── [Export ▾]┐
│ N │ h [m] │ max midpoint err [°C] │ qh²/8k [°C] │ ratio │ order │ nodal err │ energy │✓│
│10 │ 0.100 │ 1.250000             │ 1.250000    │   –   │   –   │ 2.8e-14   │ 1e-15 │✓│
└───────────────────────────────────────────────────────────────────────────────────────┘
```
Header badges: `Midpoint error = qh²/8k ✓`, `Order ≈ 2 ✓`, `Energy balance ✓`, `T_max 188.2 °C @ 0.42 m ✓`. Values shown here are the analytical expectations; the table will show computed values.

### 6.4 Performance (Task D)
Left: ParamFields (seed, cases, elements, worker set {1,2,4} with cap note "capped at 20 logical CPUs"), preset chips *Quick (≈ 30 ms)* / *Report (≈ 1 s+)*. Right tabs: **Timing** (bar chart of median with min–max whiskers per p + ideal-speedup dashed line), **Speedup & efficiency**, **System** (CPU, physical/logical, RAM, compiler, flags, workload, seed, warm-up, reps). Table: p │ reps (5) │ median │ speedup │ efficiency. Badges: `Serial = parallel (max |ΔT| 0.0e+00 ≤ 1e-8) ✓`, `Order preserved ✓`, `5 reps + warm-up ✓`. If logical CPUs = 1: warning alert "Speedup cannot be assessed on a single logical CPU; parallel path demonstrated."

### 6.5 Hardware (Task E)
Banner (simulated). `[Discover devices]` → three DeviceCards side by side (wrap to stack < 1100 px):
```text
┌ NUCLEO-F446RE  ◇ SIMULATED ┐┌ Basys 3 (Artix-7) ◇ SIMULATED ┐┌ J-Link EDU Mini ◇ SIMULATED ┐
│ Microcontroller · USB VCP   ││ FPGA · USB-UART + USB-JTAG    ││ JTAG/SWD debug probe         │
│ ● Connected                 ││ ○ Disconnected                ││ ✕ Faulted: timeout (3/3)     │
│ Caps: UART telemetry, GPIO  ││ Caps: XADC temp, DONE status  ││ Caps: IDCODE scan, VTref     │
│ vdd 3.30 V  die 31.2 °C ∿∿  ││                               ││ Retry trail: 100/200/400 ms  │
│ [Disconnect][Inject timeout]││ [Connect]                     ││ [Retry] [Reset fault]        │
└─────────────────────────────┘└───────────────────────────────┘└──────────────────────────────┘
```
A link "Design notes →" opens `docs/embedded_design.md` (block diagram + pin table) in the default viewer. Board names stay provisional until the datasheet check in Phase 6.

### 6.6 Backprop (Task F)
Three cards: **Forward pass** (x, y, w, b, v, c → z, h, ŷ, e, J), **Gradients** (table: param │ analytic │ central FD (ε = 1e-6) │ |diff| │ ≤ 1e-5 ✓), **After one update** (w, b, v, c new values). Below: loss plot over 100 steps (log y toggle) + 2-line explanation text. Badges: `4/4 FD checks ✓`, `Update matches brief ✓`.

### 6.7 About / Diagnostics
Version + git hash, build date, compiler + flags, render backend (HW/WARP), launch-to-ready (both numbers), paths in use (relative to package root), licences list, AI-assistance disclosure line, "Open logs folder".

### 6.8 Responsive behaviour
Minimum window 1024 × 680. Below 1100 px width: NavRail compact, DeviceCards stack, parameter card goes above the plots. Layout is computed from tokens, not saved ImGui docking positions, so a fresh machine always looks right.

---

## Part 7: Accessibility & limitations
- Full keyboard navigation (`NavEnableKeyboard`), visible 2 px focus ring, shortcuts listed in F1 help overlay.
- Status never colour-only (glyph + text).
- DPI-aware (per-monitor v2), fonts re-rasterised on DPI change; tested at 100/150/200 %.
- **Limitation (disclosed):** no screen-reader support, because ImGui doesn't expose UI Automation.

---

## Part 8: Implementation mapping & design QA
| Item | Where |
|---|---|
| Tokens | `src/app/ui/Theme.hpp/.cpp` (light/dark structs → ImGuiStyle/ImPlotStyle) |
| Fonts | `src/app/ui/Fonts.cpp` + `third_party/fonts/` (OFL texts in `LICENSES/`) |
| Widgets | `src/app/ui/widgets/*.cpp` (one file per component above) |
| Views | `src/app/views/*View.cpp`, each ≤ ~300 lines, composed only of widgets |
| Formatting | `src/app/ui/Format.hpp` (rules from §3.6, unit-tested) |
| Copy | error hints originate in core `Error.hint`; UI never invents messages |

**Design QA checklist (Phase 8, screenshots into `docs/demo/`):** every view at 100 % and 150 % DPI, light and dark; every component state reached at least once; no `?` glyphs; every number has a unit; every badge shows its tolerance; contrast measured; keyboard-only walkthrough of the demo script.

---

## Part 9: Comparison of the three documents & recommendation

| Aspect | Plan | SysDesign | This doc |
|---|---|---|---|
| Rubric/evidence strategy | ✅ strong | ✅ traceability IDs | adds on-screen verification badges |
| Architecture & threading | ✅ | ✅ most detailed | unchanged |
| Numerics | ✅ | ✅ (minor wording fix #5) | — |
| Packaging | ✅ (VS Code path bug #1) | ✅ (launch-time gap #3) | fixes #1, #3, #6 |
| UI/visual design | ✗ minimal | ✗ minimal | ✅ tokens, fonts, components, layouts, copy |
| AI-judgement evidence | log only | — | design iterations as evidence (#12) |

**Recommendation for the final build spec:** take **SysDesign as the backbone** (requirements IDs, contracts, threading, NFRs), apply **corrections #1–#12**, and use **this document as the UI specification**. The Plan stays the source for phase scheduling and the "things only you can do" list. Two items to decide in the merge review:
1. Add the **Overview** view (recommended: it's cheap and makes the demo and the evaluator's first minute much stronger).
2. Default theme: follow Windows setting (recommended) vs always light.
