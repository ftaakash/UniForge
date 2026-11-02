<#
.SYNOPSIS
    Builds, tests and packages UniForge into dist\<CandidateId>_Workbench.zip (spec §11).

.DESCRIPTION
    Packaging only: no backend logic lives here. Steps:
      1. configure (preset windows-msvc) and build Release (preset windows-release)
      2. run the test suite (ctest --preset windows-release)
      3. stage dist\<CandidateId>_Workbench\ with Launch.exe, app\UniForge.exe,
         app\uniforge_cli.exe, regenerate_evidence.cmd, sources, docs, results, licences and
         the ZIP-specific .vscode configuration, plus git\UniForge.bundle and git\git_log.txt
      4. check with dumpbin /dependents that the executables import only Windows DLLs
      5. run tools\hygiene_scan.ps1 on the staged folder
      6. zip the staged folder

.PARAMETER CandidateId
    Prefix of the package folder and ZIP name.
.PARAMETER SkipBuild
    Use the existing Release build and skip configure, build and tests (CI runs those as
    separate steps before packaging).

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\package.ps1
#>
[CmdletBinding()]
param(
    [string]$CandidateId = 'RA2311042010060',
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Root = Split-Path -Parent $PSScriptRoot
$BinDir = Join-Path $Root 'build\windows-msvc\bin\Release'
$PackageName = "${CandidateId}_Workbench"
$DistDir = Join-Path $Root 'dist'
$Stage = Join-Path $DistDir $PackageName
$Zip = Join-Path $DistDir "$PackageName.zip"

# Windows system DLLs the executables may import. Anything else (VC++ runtime, UCRT,
# d3dcompiler_47, third-party DLLs) would fail on a clean machine and fails the package.
$AllowedDlls = @(
    'kernel32.dll', 'user32.dll', 'gdi32.dll', 'shell32.dll', 'ole32.dll', 'oleaut32.dll',
    'advapi32.dll', 'imm32.dll', 'd3d11.dll', 'dxgi.dll', 'dwmapi.dll', 'comdlg32.dll',
    'shlwapi.dll', 'uxtheme.dll', 'version.dll', 'winmm.dll', 'dbghelp.dll'
)

function Write-Step([string]$message) {
    Write-Host "==> $message" -ForegroundColor Cyan
}

function Invoke-Checked([string]$exe, [string[]]$arguments) {
    & $exe @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "'$exe $($arguments -join ' ')' failed with exit code $LASTEXITCODE"
    }
}

function Get-VsInstallPath {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw 'vswhere.exe not found: install Visual Studio 2022 or the Build Tools.'
    }
    $path = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $path) { throw 'No Visual Studio installation with the C++ x64 tools was found.' }
    return $path
}

function Get-CMakeTool([string]$name) {
    $onPath = Get-Command $name -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    $bundled = Join-Path (Get-VsInstallPath) "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\$name.exe"
    if (-not (Test-Path -LiteralPath $bundled)) { throw "$name not found on PATH or in Visual Studio." }
    return $bundled
}

function Get-Dumpbin {
    $candidates = Get-ChildItem -Path (Join-Path (Get-VsInstallPath) 'VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe') |
        Sort-Object FullName
    if (-not $candidates) { throw 'dumpbin.exe not found in the Visual Studio C++ tools.' }
    return @($candidates)[-1].FullName
}

function Copy-Tree([string]$relative) {
    $source = Join-Path $Root $relative
    if (Test-Path -LiteralPath $source) {
        Copy-Item -LiteralPath $source -Destination (Join-Path $Stage $relative) -Recurse -Force
    }
}

function Test-Dependencies([string]$exe, [string]$dumpbin) {
    $output = & $dumpbin /nologo /dependents $exe
    if ($LASTEXITCODE -ne 0) { throw "dumpbin failed on $exe" }
    $dlls = @($output | Where-Object { $_ -match '^\s+(\S+\.dll)\s*$' } | ForEach-Object { $Matches[1] })
    $bad = @($dlls | Where-Object { $AllowedDlls -notcontains $_.ToLowerInvariant() })
    $name = Split-Path -Leaf $exe
    Write-Host ("    {0}: {1}" -f $name, ($dlls -join ', '))
    if ($bad.Count -gt 0) {
        throw "$name imports non-system DLLs: $($bad -join ', ')"
    }
}

Push-Location $Root
try {
    if (-not $SkipBuild) {
        $cmake = Get-CMakeTool 'cmake'
        $ctest = Get-CMakeTool 'ctest'
        Write-Step 'Configure (preset windows-msvc)'
        Invoke-Checked $cmake @('--preset', 'windows-msvc')
        Write-Step 'Build Release (preset windows-release)'
        Invoke-Checked $cmake @('--build', '--preset', 'windows-release')
        Write-Step 'Run tests (ctest --preset windows-release)'
        Invoke-Checked $ctest @('--preset', 'windows-release')
    }

    foreach ($exe in @('Launch.exe', 'UniForge.exe', 'uniforge_cli.exe')) {
        if (-not (Test-Path -LiteralPath (Join-Path $BinDir $exe))) {
            throw "$exe not found in $BinDir. Build first or drop -SkipBuild."
        }
    }

    Write-Step "Stage $PackageName"
    if (Test-Path -LiteralPath $Stage) { Remove-Item -LiteralPath $Stage -Recurse -Force }
    New-Item -ItemType Directory -Path (Join-Path $Stage 'app') -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $BinDir 'Launch.exe') -Destination $Stage
    Copy-Item -LiteralPath (Join-Path $BinDir 'UniForge.exe') -Destination (Join-Path $Stage 'app')
    Copy-Item -LiteralPath (Join-Path $BinDir 'uniforge_cli.exe') -Destination (Join-Path $Stage 'app')
    # Wrapper with no logic: runs app\uniforge_cli.exe all --out results from the package root.
    Copy-Item -LiteralPath (Join-Path $Root 'tools\package\regenerate_evidence.cmd') -Destination $Stage
    foreach ($dir in @('src', 'tests', 'cmake', 'third_party', 'tools', 'docs', 'LICENSES', 'data', 'results')) {
        Copy-Tree $dir
    }
    foreach ($file in @('CMakeLists.txt', 'CMakePresets.json', 'README.md', 'LICENSE', 'DEPENDENCIES.md',
                        'CHANGELOG.md', '.clang-format', '.clang-tidy', '.editorconfig', '.gitattributes',
                        '.gitignore')) {
        Copy-Item -LiteralPath (Join-Path $Root $file) -Destination $Stage
    }
    # The ZIP gets its own VS Code config that runs the packaged Launch.exe (the repo's
    # .vscode points at build/ and dist/, which do not exist inside the extracted ZIP).
    $vscode = Join-Path $Stage '.vscode'
    New-Item -ItemType Directory -Path $vscode -Force | Out-Null
    Copy-Item -Path (Join-Path $Root 'tools\package\vscode\*.json') -Destination $vscode

    # Full history for the evaluator (spec §11): a bundle of main and the phase tags, and a
    # readable log. `git clone git\UniForge.bundle UniForge` restores the repository offline.
    Write-Step 'Git bundle and log'
    $gitDir = Join-Path $Stage 'git'
    New-Item -ItemType Directory -Path $gitDir -Force | Out-Null
    $bundle = Join-Path $gitDir 'UniForge.bundle'
    # git reports progress on stderr; with 'Stop', Windows PowerShell 5.1 turns that into an error.
    $ErrorActionPreference = 'Continue'
    & git -C $Root bundle create $bundle HEAD main --tags 2>&1 | Out-Null
    $created = $LASTEXITCODE
    & git -C $Root bundle verify $bundle 2>&1 | Out-Null
    $verified = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($created -ne 0) { throw 'git bundle failed (is this a git checkout with a main branch?).' }
    if ($verified -ne 0) { throw 'git bundle verify failed.' }
    $log = & git -C $Root log --date=iso-strict --format='%h  %ad  %an  %s' --decorate=no
    $tags = & git -C $Root tag -n1 --sort=creatordate
    (@('UniForge commit history (git log, newest first)', '') + $log + @('', 'Tags', '') + $tags) |
        Set-Content -LiteralPath (Join-Path $gitDir 'git_log.txt') -Encoding utf8
    Write-Host ("    {0} commits, {1} tags" -f $log.Count, @($tags).Count)

    Write-Step 'Check executable dependencies (dumpbin /dependents)'
    $dumpbin = Get-Dumpbin
    Test-Dependencies (Join-Path $Stage 'Launch.exe') $dumpbin
    Test-Dependencies (Join-Path $Stage 'app\UniForge.exe') $dumpbin
    Test-Dependencies (Join-Path $Stage 'app\uniforge_cli.exe') $dumpbin

    Write-Step 'Hygiene scan'
    & (Join-Path $PSScriptRoot 'hygiene_scan.ps1') -Path $Stage
    if ($LASTEXITCODE -ne 0) { throw 'Hygiene scan failed; see the findings above.' }

    Write-Step "Zip $PackageName.zip"
    if (Test-Path -LiteralPath $Zip) { Remove-Item -LiteralPath $Zip -Force }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::CreateFromDirectory(
        $Stage, $Zip, [System.IO.Compression.CompressionLevel]::Optimal, $true)
    $sizeMb = (Get-Item -LiteralPath $Zip).Length / 1MB
    Write-Host ("    {0} ({1:N1} MB)" -f (Resolve-Path -LiteralPath $Zip -Relative), $sizeMb)
    if ($sizeMb -gt 15) { Write-Warning 'Package is larger than the 15 MB target (spec N10).' }
    Write-Host 'Package ready.' -ForegroundColor Green
} finally {
    Pop-Location
}
