@echo off
rem Regenerates the evidence in results\ (CSV files, .meta.json sidecars, SVG plots) with the
rem packaged command-line tool. Wrapper only: the logic lives in app\uniforge_cli.exe.
cd /d "%~dp0"
app\uniforge_cli.exe all --out results
pause
