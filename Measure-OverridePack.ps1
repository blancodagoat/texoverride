[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$Root,
    [string]$ReportCsv,
    [switch]$FailOnRejected
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$limit = [uint64](32MB)

function Get-RscSizeFromFlags {
    param([uint32]$Flags)
    $f = [uint64]$Flags
    $pages = (($f -shr 27) -band 1) +
             ((($f -shr 26) -band 1) -shl 1) +
             ((($f -shr 25) -band 1) -shl 2) +
             ((($f -shr 24) -band 1) -shl 3) +
             ((($f -shr 17) -band 0x7F) -shl 4) +
             ((($f -shr 11) -band 0x3F) -shl 5) +
             ((($f -shr 7)  -band 0x0F) -shl 6) +
             ((($f -shr 5)  -band 0x03) -shl 7) +
             ((($f -shr 4)  -band 0x01) -shl 8)
    return ([uint64](0x200) -shl [int]($f -band 0x0F)) * [uint64]$pages
}

$resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
$rows = [System.Collections.Generic.List[object]]::new()

Get-ChildItem -LiteralPath $resolvedRoot -Recurse -File |
    Where-Object { $_.Extension -in '.ydd', '.ytd' } |
    Sort-Object FullName |
    ForEach-Object {
        $state = 'Unreadable'
        [uint64]$virtual = 0
        [uint64]$physical = 0
        try {
            $share = [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete
            $stream = [IO.FileStream]::new($_.FullName, [IO.FileMode]::Open, [IO.FileAccess]::Read, $share)
            try {
                $header = [byte[]]::new(16)
                $got = $stream.Read($header, 0, $header.Length)
            }
            finally { $stream.Dispose() }

            if ($got -ne 16 -or $_.Length -le 16 -or [BitConverter]::ToUInt32($header, 0) -ne 0x37435352) {
                $state = 'InvalidHeader'
            }
            else {
                $virtual = Get-RscSizeFromFlags ([BitConverter]::ToUInt32($header, 8))
                $physical = Get-RscSizeFromFlags ([BitConverter]::ToUInt32($header, 12))
                if (($virtual -eq 0) -and ($physical -eq 0)) { $state = 'InvalidHeader' }
                elseif ($virtual -gt $limit) { $state = 'TooBigMesh' }
                elseif ($physical -gt $limit) { $state = 'TooBigTexture' }
                else { $state = 'Accepted' }
            }
        }
        catch { $state = 'Unreadable' }

        $rows.Add([pscustomobject]@{
            RelativePath = [IO.Path]::GetRelativePath($resolvedRoot, $_.FullName).Replace('\', '/')
            Type = $_.Extension.TrimStart('.').ToUpperInvariant()
            DiskBytes = [uint64]$_.Length
            VirtualBytes = $virtual
            PhysicalBytes = $physical
            TotalMemoryBytes = $virtual + $physical
            Status = $state
        })
    }

if ($ReportCsv) {
    $reportParent = Split-Path -Parent $ReportCsv
    if ($reportParent -and -not (Test-Path -LiteralPath $reportParent)) {
        New-Item -ItemType Directory -Path $reportParent -Force | Out-Null
    }
    $rows | Export-Csv -LiteralPath $ReportCsv -NoTypeInformation -Encoding utf8
}

$accepted = @($rows | Where-Object Status -eq 'Accepted')
$rejected = @($rows | Where-Object Status -ne 'Accepted')
[pscustomobject]@{
    Root = $resolvedRoot
    Resources = $rows.Count
    Accepted = $accepted.Count
    Rejected = $rejected.Count
    AcceptedVirtualGiB = [math]::Round((($accepted | Measure-Object VirtualBytes -Sum).Sum / 1GB), 3)
    AcceptedPhysicalGiB = [math]::Round((($accepted | Measure-Object PhysicalBytes -Sum).Sum / 1GB), 3)
    ReportCsv = $ReportCsv
}

if ($rejected.Count) {
    $rejected | Select-Object RelativePath, Status, VirtualBytes, PhysicalBytes | Format-Table -AutoSize | Out-Host
    if ($FailOnRejected) { exit 2 }
}
