# Demo script (3–5 minutes)

Recorded from the extracted package `RA2311042010060_Workbench`. Screen at 100 % scaling;
1600 × 1000 window. Times are targets.

## Before recording

- Extract `RA2311042010060_Workbench.zip` to a fresh folder. Delete `logs\` and `imgui.ini` if they
  exist, so the app starts from a clean layout.
- Close other heavy programs (the benchmark runs live).
- Have Explorer open on the package folder.

## Script

| Time | Do | Say |
|---|---|---|
| 0:00 | Show the extracted folder; double-click `Launch.exe` | "This is the submitted package. One double-click: Launch.exe starts the app. No installer and no runtime to install; it links the C runtime statically." |
| 0:10 | Point at the status bar "Ready in 0.13 s" and the log line | "Launch-to-ready is measured from Launch.exe to the first frame: about 0.13 s." |
| 0:20 | **Overview**: the Tasks table | "One workbench for tasks A to F. Each row shows live status and opens its view. Backprop already shows Pass because its gradient check runs on the spot." |
| 0:35 | **Geometry** → *Load samples* | "Task B. The ASCII STL of the 1 m rod and a nodal temperature CSV import on a background thread. The CSV is synthetic and labelled so." |
| 0:50 | Orbit (drag), zoom (wheel), click a node, press `F` | "Orbit, zoom to the cursor, pick the nearest node within 6 pixels. The inspector shows id, position and temperature; the legend is in °C." |
| 1:05 | Drop `tests\fixtures\stl\vertex_count_two.stl` on the window | "A malformed file gives a typed error with the line number and a hint; the previous model stays." |
| 1:20 | **Thermal** → `Ctrl+Enter` | "Task C. 1D steady conduction with two-node linear elements, N = 10, 20, 40, 80, against the exact solution." |
| 1:35 | Hover the four badges; open *Error vs h* | "The midpoint error equals qh²/8k to 1e-9, the order is 2, energy balances, and the peak is 188.2 °C at x = 0.42 m. On the log–log plot the points sit on the slope-2 line." |
| 1:55 | *Export ▾ → Convergence CSV* | "Every result exports atomically with full precision." |
| 2:05 | **Performance** → *Report* preset → `Ctrl+Enter` | "Task D. 10 000 independent rods with 8 000 elements each, serial vs 2 and 4 threads, one warm-up and five timed runs." |
| 2:25 | (while it runs) | "The UI stays responsive; Esc would cancel and keep the previous result." |
| 2:35 | Point at the table and badges | "Speedup is about 2 and 4, and the parallel results equal the serial ones bit for bit: max ΔT is 0." |
| 2:50 | **Hardware** → *Discover* → *Connect* the Nucleo → *Inject timeout* | "Task E. Three simulated devices, the Nucleo-F446RE, Basys 3 and J-Link, clearly marked SIMULATED. An injected timeout triggers the retry policy: recovered after one retry." |
| 3:10 | *Design notes →* (opens `embedded_design.md`) | "The design notes hold the cited pin and voltage table and the block diagram." |
| 3:20 | **Backprop** | "Task F. The analytic gradients match central differences to about 1e-10, and the loss falls to 1.7e-20 in 100 steps. The derivation is in my handwritten appendix." |
| 3:40 | Explorer: double-click `regenerate_evidence.cmd` | "Every committed result can be regenerated from the package with one command." |
| 3:55 | **About** | "Build, render backend, file locations and third-party licences." |
| 4:05 | End | "Tests: 289 GoogleTest cases in CI, plus Linux sanitizer builds of the core." |

## If something goes wrong on camera

- SmartScreen "Windows protected your PC": *More info → Run anyway* (unsigned executable;
  see README).
- The benchmark is slow because other programs are running: use the *Quick* preset and say so.
