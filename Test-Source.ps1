[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$cpp = Get-Content -LiteralPath (Join-Path $root 'dllmain.cpp') -Raw
$rc = Get-Content -LiteralPath (Join-Path $root 'texoverride.rc') -Raw
$changelog = Get-Content -LiteralPath (Join-Path $root 'CHANGELOG.md') -Raw

$match = [regex]::Match($cpp, '#define\s+TEXOVERRIDE_VERSION\s+"([^"]+)"')
if (-not $match.Success) { throw 'TEXOVERRIDE_VERSION was not found.' }
$version = $match.Groups[1].Value

if ($rc -notmatch [regex]::Escape('VALUE "FileVersion",      "' + $version + '"')) {
    throw "texoverride.rc FileVersion does not match $version."
}
if ($rc -notmatch [regex]::Escape('VALUE "ProductVersion",   "' + $version + '"')) {
    throw "texoverride.rc ProductVersion does not match $version."
}
if ($changelog -notmatch ('(?m)^## ' + [regex]::Escape($version) + '\s')) {
    throw "CHANGELOG.md has no section for $version."
}
if ($rc -notmatch 'FILEVERSION\s+0,7,3,1') {
    throw 'The numeric Windows file version is not 0.7.3.1.'
}

$requiredSafetyText = @(
    'CostState::InvalidHeader',
    'acquireSafeResource',
    'HANDLE lease = INVALID_HANDLE_VALUE',
    'CsGuard lock(g_cs)',
    'FILE_ATTRIBUTE_REPARSE_POINT',
    'WaitForSingleObject(g_scanDone, 15000)',
    'else if (lastPump && ++pumpMisses >= 5',
    'locateRuntimePatternsSafe()',
    'REG_IN_PROGRESS',
    'g_registerDone',
    'rejectActiveOverwrite',
    'if (g_vramTable[i + j] >= desired[j]) continue;',
    'InterlockedExchange((volatile LONG*)&e.handle'
)
foreach ($needle in $requiredSafetyText) {
    if (-not $cpp.Contains($needle)) { throw "Required safety behavior is missing: $needle" }
}

"Source checks passed for texoverride $version."
