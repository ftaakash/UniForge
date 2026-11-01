<#
.SYNOPSIS
    Scans a folder for things that must never ship: absolute machine paths, e-mail addresses
    and secrets (spec S4). Exits 1 and lists every finding when anything is found.

.DESCRIPTION
    Text files (no NUL byte in the first 8 KB) are checked line by line. Executables and
    libraries are checked for embedded user-profile paths (e.g. a PDB path left by /DEBUG).
    Vendored code in third_party/ and licence texts in LICENSES/ are exempt from the e-mail
    and path checks (they carry upstream author contacts) but are still checked for secrets.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\hygiene_scan.ps1 -Path dist\CandidateID_Workbench
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Path,
    [string[]]$VendoredDirs = @('third_party', 'LICENSES'),
    [string[]]$SkipDirs = @('.git', 'build', 'dist', 'out', '.vs')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$rootDir = New-Object System.IO.DirectoryInfo((Resolve-Path -LiteralPath $Path).Path)
$root = $rootDir.FullName.TrimEnd('\')

# Absolute paths: drive-letter paths with at least two segments (a user profile or project path)
# and Unix home folders. A bare drive + folder quoted as an example in docs, and
# environment-variable forms such as %LOCALAPPDATA%\UniForge, do not match.
$pathPattern = '(?<![A-Za-z0-9%$])[A-Za-z]:[\\/][^\s"''<>|*?\\/]+[\\/][^\s"''<>|*?]+|/(?:home|Users)/[A-Za-z0-9._-]+/'
$emailPattern = '[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)*\.[A-Za-z]{2,}'
$secretPatterns = @(
    'AKIA[0-9A-Z]{16}',                                   # AWS access key id
    'gh[pousr]_[A-Za-z0-9]{36,}',                         # GitHub tokens
    'github_pat_[A-Za-z0-9_]{22,}',
    'sk-[A-Za-z0-9_-]{20,}',                              # API keys in the sk- format
    'xox[baprs]-[A-Za-z0-9-]{10,}',                       # Slack tokens
    '-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----',
    '(?i)\b(?:password|passwd|secret|api[_-]?key|access[_-]?token)\b\s*[:=]\s*["''][^"''\s]{6,}["'']'
)
$binaryExtensions = @('.exe', '.dll', '.lib', '.pdb', '.obj')
$profilePathPattern = '(?i)[A-Z]:\\Users\\[^\\\x00]+\\'

$findings = New-Object System.Collections.Generic.List[string]

function Add-Finding([string]$file, [int]$line, [string]$kind, [string]$text) {
    $rel = $file.Substring($root.Length).TrimStart('\')
    if ($text.Length -gt 120) { $text = $text.Substring(0, 120) + '...' }
    $findings.Add(("{0}:{1}: {2}: {3}" -f $rel, $line, $kind, $text.Trim()))
}

function Test-IsText([string]$file) {
    $stream = [System.IO.File]::OpenRead($file)
    try {
        $buffer = New-Object byte[] 8192
        $read = $stream.Read($buffer, 0, $buffer.Length)
        for ($i = 0; $i -lt $read; $i++) { if ($buffer[$i] -eq 0) { return $false } }
        return $true
    } finally {
        $stream.Dispose()
    }
}

# Enumerate through DirectoryInfo so every FullName shares the exact spelling of $root.
$files = @($rootDir.GetFiles('*', [System.IO.SearchOption]::AllDirectories) | Where-Object {
    $rel = $_.FullName.Substring($root.Length).TrimStart('\')
    $top = ($rel -split '[\\/]')[0]
    -not ($SkipDirs -contains $top)
})

foreach ($file in $files) {
    $rel = $file.FullName.Substring($root.Length).TrimStart('\')
    $top = ($rel -split '[\\/]')[0]
    $vendored = $VendoredDirs -contains $top
    $ext = $file.Extension.ToLowerInvariant()

    if ($binaryExtensions -contains $ext) {
        $bytes = [System.IO.File]::ReadAllBytes($file.FullName)
        $ascii = [System.Text.Encoding]::GetEncoding(28591).GetString($bytes)
        $utf16 = [System.Text.Encoding]::Unicode.GetString($bytes)
        foreach ($m in @([regex]::Matches($ascii, $profilePathPattern)) + @([regex]::Matches($utf16, $profilePathPattern))) {
            Add-Finding $file.FullName 0 'absolute path in binary' $m.Value
        }
        continue
    }
    if ($file.Length -gt 16MB -or -not (Test-IsText $file.FullName)) { continue }

    $lineNo = 0
    foreach ($line in [System.IO.File]::ReadLines($file.FullName)) {
        $lineNo++
        foreach ($p in $secretPatterns) {
            if ($line -match $p) { Add-Finding $file.FullName $lineNo 'possible secret' '(redacted)' }
        }
        if ($vendored) { continue }
        if ($line -match $pathPattern) { Add-Finding $file.FullName $lineNo 'absolute path' $Matches[0] }
        if ($line -match $emailPattern) { Add-Finding $file.FullName $lineNo 'e-mail address' '(redacted)' }
    }
}

if ($findings.Count -gt 0) {
    Write-Host "Hygiene scan FAILED: $($findings.Count) finding(s) in $root" -ForegroundColor Red
    $findings | ForEach-Object { Write-Host "  $_" }
    exit 1
}
Write-Host "Hygiene scan passed: $($files.Count) files, no absolute paths, e-mail addresses or secrets." -ForegroundColor Green
exit 0
