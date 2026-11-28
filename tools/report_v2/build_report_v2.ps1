<#
.SYNOPSIS
    Builds docs\report_v2\UniForge_V2_Report.pdf: fills the numbers from results\v2, prints the
    HTML with headless Microsoft Edge and checks the PDF (build spec §12.4).

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\report_v2\build_report_v2.ps1
#>
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$py = (Get-Command py -ErrorAction SilentlyContinue)
if (-not $py) { throw 'Python launcher (py) not found.' }

& py -3 (Join-Path $root 'tools\report_v2\fill_numbers.py')
if ($LASTEXITCODE -ne 0) { throw 'fill_numbers.py failed.' }

$html = Join-Path $root 'docs\report_v2\report.html'
$pdf = Join-Path $root 'docs\report_v2\UniForge_V2_Report.pdf'
$edge = @(
    "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe",
    "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe"
) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $edge) { throw 'Microsoft Edge not found.' }

if (Test-Path -LiteralPath $pdf) { Remove-Item -LiteralPath $pdf }
$profileDir = Join-Path $env:TEMP 'uniforge_report_v2_edge'
$url = ([System.Uri]$html).AbsoluteUri
$p = Start-Process -FilePath $edge -PassThru -Wait -ArgumentList @(
    '--headless', '--disable-gpu', '--no-first-run', "--user-data-dir=`"$profileDir`"",
    '--allow-file-access-from-files', '--no-pdf-header-footer', '--virtual-time-budget=10000',
    "--print-to-pdf=`"$pdf`"", $url)
if (-not (Test-Path -LiteralPath $pdf)) { throw "Edge did not write $pdf (exit $($p.ExitCode))." }

& py -3 (Join-Path $root 'tools\report_v2\check_pdf.py') $pdf
if ($LASTEXITCODE -ne 0) { exit 1 }
