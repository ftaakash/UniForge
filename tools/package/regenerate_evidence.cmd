@echo off
rem Regenerates the evidence in results\ (CSV files, .meta.json sidecars, SVG plots) and the
rem sample files in data\ with the packaged command-line tool. Wrapper only: the logic lives in
rem app\uniforge_cli.exe.
cd /d "%~dp0"
app\uniforge_cli.exe all --out results
pause
