<#
.SYNOPSIS
    Edge-case runs of the packaged app (spec §11 and §13, Phase 8).

.DESCRIPTION
    Copies the staged package to a folder whose path has spaces and non-ASCII characters, then:
      1. Launch.exe from that path               -> "launch-to-ready" in logs\uniforge.log
      2. app\UniForge.exe --force-warp           -> WARP device, ready
      3. read-only package folder (ACL deny write) -> logs fall back to %LOCALAPPDATA%\UniForge
      4. app\UniForge.exe --simulate-crash       -> logs\crash_<pid>.txt written
      5. app\uniforge_cli.exe all --out <non-ASCII path> -> exit code 0
    Writes a Markdown table to -Out. Every GUI process is closed by the script.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\edge_case_runs.ps1
#>
param(
    [string]$Package = (Join-Path $PSScriptRoot '..\dist\RA2311042010060_Workbench'),
    [string]$Out = (Join-Path $PSScriptRoot '..\results\edge_cases.md')
)
$ErrorActionPreference = 'Stop'

$Package = (Resolve-Path -LiteralPath $Package).Path
$base = Join-Path $env:TEMP ("UniForge edge " + [char]0x00FC + [char]0x00F1 + [char]0x00EF + ' ' + [char]0x6D4B + [char]0x8BD5)
if (Test-Path -LiteralPath $base) { Remove-Item -LiteralPath $base -Recurse -Force }
$pkg = Join-Path $base ('pkg with spaces ' + [char]0x00E9)
$ro = Join-Path $base 'read-only pkg'
New-Item -ItemType Directory -Force -Path $pkg, $ro | Out-Null
foreach ($dest in $pkg, $ro) {
    foreach ($item in 'Launch.exe', 'app', 'docs', 'data', 'LICENSES') {
        Copy-Item -LiteralPath (Join-Path $Package $item) -Destination $dest -Recurse
    }
}
$rows = New-Object System.Collections.Generic.List[string]

function Wait-ForLine([string]$file, [string]$pattern, [int]$seconds = 20) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
        if (Test-Path -LiteralPath $file) {
            $hit = Select-String -LiteralPath $file -Pattern $pattern -SimpleMatch | Select-Object -Last 1
            if ($hit) { return $hit.Line.Trim() }
        }
        Start-Sleep -Milliseconds 250
    }
    return $null
}
function Stop-UniForge {
    Get-Process UniForge -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 500
}
function Add-Row([string]$case, [bool]$pass, [string]$evidence) {
    $mark = if ($pass) { 'pass' } else { 'FAIL' }
    $rows.Add("| $case | $mark | $($evidence -replace '\|', '/') |")
    Write-Host ("[{0}] {1}: {2}" -f $mark, $case, $evidence)
}

Stop-UniForge

# 1. Launch.exe from a path with spaces and non-ASCII characters
$log = Join-Path $pkg 'logs\uniforge.log'
Start-Process -FilePath (Join-Path $pkg 'Launch.exe') -WorkingDirectory $pkg
$line = Wait-ForLine $log 'launch-to-ready'
Stop-UniForge
Add-Row 'Launch.exe from a path with spaces + non-ASCII' ($null -ne $line) $(if ($line) { $line.Substring($line.IndexOf('launch-to-ready')) } else { 'no ready line' })

# 2. WARP software device
Remove-Item -LiteralPath (Join-Path $pkg 'logs') -Recurse -Force
Start-Process -FilePath (Join-Path $pkg 'app\UniForge.exe') -ArgumentList '--force-warp' -WorkingDirectory $pkg
$ready = Wait-ForLine $log 'launch-to-ready'
$warp = Select-String -LiteralPath $log -Pattern 'WARP' -SimpleMatch -ErrorAction SilentlyContinue | Select-Object -First 1
Stop-UniForge
Add-Row '`--force-warp` (no GPU driver path)' ($null -ne $ready -and $null -ne $warp) $(if ($warp) { $warp.Line.Trim().Substring(25) } else { 'no WARP line' })

# 3. Read-only package folder: deny write for the current user
$user = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
& icacls $ro /deny "${user}:(WD,AD)" | Out-Null
try {
    $fallbackLog = Join-Path $env:LOCALAPPDATA 'UniForge\logs\uniforge.log'
    $before = if (Test-Path -LiteralPath $fallbackLog) { (Get-Item -LiteralPath $fallbackLog).Length } else { 0 }
    Start-Process -FilePath (Join-Path $ro 'Launch.exe') -WorkingDirectory $ro
    $line = Wait-ForLine $fallbackLog 'package dir is read-only'
    $ready = Wait-ForLine $fallbackLog 'launch-to-ready'
    Stop-UniForge
    $wroteInPackage = Test-Path -LiteralPath (Join-Path $ro 'logs')
    Add-Row 'Read-only package folder' ($null -ne $line -and $null -ne $ready -and -not $wroteInPackage) $(if ($line) { 'logs + imgui.ini fall back to %LOCALAPPDATA%\UniForge; nothing written in the package' } else { 'no fallback line' })
} finally {
    & icacls $ro /remove:d $user | Out-Null
}

# 4. Crash handler
Remove-Item -LiteralPath (Join-Path $pkg 'logs') -Recurse -Force -ErrorAction SilentlyContinue
$p = Start-Process -FilePath (Join-Path $pkg 'app\UniForge.exe') -ArgumentList '--simulate-crash' -WorkingDirectory $pkg -PassThru
$crash = Join-Path $pkg ("logs\crash_{0}.txt" -f $p.Id)
$code = Wait-ForLine $crash 'exception code'
Stop-UniForge # closes the message box with the process
Add-Row '`--simulate-crash` (unhandled exception)' ($null -ne $code) $(if ($code) { "logs\crash_<pid>.txt written, $code" } else { 'no crash report' })

# 5. CLI evidence regeneration into a non-ASCII output folder
$cliOut = Join-Path $base ('cli out ' + [char]0x00FC)
& (Join-Path $pkg 'app\uniforge_cli.exe') all --out $cliOut *> (Join-Path $base 'cli.txt')
$exit = $LASTEXITCODE
$files = (Get-ChildItem -LiteralPath $cliOut -Recurse -File -ErrorAction SilentlyContinue | Measure-Object).Count
Add-Row '`uniforge_cli all --out` a non-ASCII path' ($exit -eq 0 -and $files -gt 0) "exit $exit, $files files written"

$os = (Get-CimInstance Win32_OperatingSystem).Caption
$md = @(
    '# Edge-case runs (packaged app)',
    '',
    "Generated by ``tools/edge_case_runs.ps1`` on $((Get-Date).ToUniversalTime().ToString('yyyy-MM-dd HH:mm')) UTC, $os.",
    "The package was copied to ``%TEMP%\$(Split-Path -Leaf $base)\$(Split-Path -Leaf $pkg)``.",
    '',
    '| Case | Result | Evidence |',
    '|---|---|---|'
) + $rows
$md -join "`n" | Set-Content -LiteralPath $Out -Encoding utf8
Remove-Item -LiteralPath $base -Recurse -Force -ErrorAction SilentlyContinue
if ($rows | Where-Object { $_ -match '\| FAIL \|' }) { exit 1 }
