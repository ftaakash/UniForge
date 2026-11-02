# Phase 1 evidence: clean-machine launch

Screenshots for the Phase 1 exit criterion "ZIP launches in Windows Sandbox" (spec §11, §13).

| File | What it shows | Where |
|---|---|---|
| `dev_overview_cancel_keeps_result.png` | Overview demo job: run #2 completed, run #3 cancelled, run #2 still the published result; status bar and log agree | Development PC (hardware D3D11) |
| `sandbox_overview.png` | First start from `Desktop\UniForge test ünï\`: all text and symbols render, ready in 0.09 s | Windows Sandbox, GPU disabled |
| `sandbox_job_cancelled_previous_kept.png` | Run #1 done (2.22 s), run #2 cancelled at step 13/20, run #1 still the published result | Windows Sandbox, GPU disabled |
| `sandbox_about.png` | About: version, render backend, both launch-to-ready numbers (0.090 s / 0.070 s) | Windows Sandbox, GPU disabled |
| `sandbox_launch.log` | `logs/launch.log` from that run | Windows Sandbox, GPU disabled |

These Sandbox screenshots predate one fix. The adapter really was the software "Microsoft
Basic Render Driver", but that build labelled it `D3D11 HW`. Since the fix it shows
`! D3D11 WARP · Software rendering`; see the AI-use log, Phase 1, row 7.

## Windows Sandbox checklist

Windows Sandbox is a fresh, disposable Windows 11 with no VC++ redistributable and no GPU
driver (it renders with WARP), so it is a good stand-in for the evaluator's clean machine.

1. Enable it once: *Turn Windows features on or off → Windows Sandbox*, then restart.
2. Build the package: `powershell -ExecutionPolicy Bypass -File tools\package.ps1`.
3. Start *Windows Sandbox*, copy `dist\RA2311042010060_Workbench.zip` into it (copy/paste works).
4. In the Sandbox, extract the ZIP (right-click → *Extract All*) and double-click `Launch.exe`.
   If SmartScreen appears: *More info → Run anyway* (note it).
5. Check and screenshot:
   - [ ] The window opens on Overview; text and symbols render (no `?` boxes) → `sandbox_overview.png`
   - [ ] Status bar shows `! D3D11 WARP · Software rendering` and `Ready in … s`
   - [ ] `Ctrl+Enter` runs the demo job: progress, stage and elapsed time move; it finishes with ✓ Done → `sandbox_job_done.png`
   - [ ] Run again, press `Esc` mid-run: "Cancelled — previous result kept", last completed result unchanged → `sandbox_job_cancelled.png`
   - [ ] `Ctrl+1` … `Ctrl+7` (or clicks) open every view; each placeholder says when it arrives
   - [ ] About shows version, render backend (WARP) and both launch-to-ready numbers → `sandbox_about.png`
   - [ ] `logs\launch.log` and `logs\uniforge.log` exist next to `Launch.exe`
6. Optional: extract a second copy to a folder with spaces and accents (e.g. `C:\Test ünï\`) and
   launch again.
7. Copy the screenshots and `logs\launch.log` out of the Sandbox into this folder
   (`launch.log` as `sandbox_launch.log`).
