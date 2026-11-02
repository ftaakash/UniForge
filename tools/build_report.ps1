<#
.SYNOPSIS
    Builds docs\report\report.pdf from docs\report\report.html with headless Microsoft Edge.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\build_report.ps1
#>
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')
$html = Join-Path $root 'docs\report\report.html'
$pdf = Join-Path $root 'docs\report\report.pdf'
$edge = @(
    "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe",
    "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe"
) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $edge) { throw 'Microsoft Edge not found.' }

$profileDir = Join-Path $env:TEMP 'uniforge_report_edge'
$url = ([System.Uri]$html).AbsoluteUri
$p = Start-Process -FilePath $edge -PassThru -Wait -ArgumentList @(
    '--headless', '--disable-gpu', '--no-first-run', "--user-data-dir=`"$profileDir`"",
    '--no-pdf-header-footer', "--print-to-pdf=`"$pdf`"", $url)
if (-not (Test-Path -LiteralPath $pdf)) { throw "Edge did not write $pdf (exit $($p.ExitCode))." }
$text = [System.IO.File]::ReadAllText($pdf, [System.Text.Encoding]::GetEncoding(28591))
$pages = ([regex]::Matches($text, '/Type\s*/Page[^s]')).Count
"$pdf ($pages pages, $([math]::Round((Get-Item -LiteralPath $pdf).Length / 1KB)) KB)"
if ($pages -gt 6) { Write-Warning 'The brief allows at most 6 pages.'; exit 1 }
