# Builds texoverride.zip: the plugin plus a ready-made tex_overrides folder with one empty
# folder per collection, so nobody has to guess or spell a collection name.
#
# The folder list comes out of COLLECTIONS.md, which is the documented list players read, so the
# two can never disagree. Run from the repo root, after build.bat.
param([string]$Out = "texoverride.zip")
$ErrorActionPreference = "Stop"

if (-not (Test-Path texoverride.asi)) { throw "texoverride.asi not found. Run build.bat first." }

$names = Select-String -Path COLLECTIONS.md -Pattern '^- `([a-z0-9_]+)`$' |
         ForEach-Object { $_.Matches[0].Groups[1].Value }
if ($names.Count -lt 150) { throw "only $($names.Count) collections parsed out of COLLECTIONS.md" }

$stage = Join-Path ([System.IO.Path]::GetTempPath()) "texoverride-zip"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
$tex = Join-Path $stage "tex_overrides"
New-Item -ItemType Directory -Path $tex -Force | Out-Null
foreach ($n in $names) { New-Item -ItemType Directory -Path (Join-Path $tex $n) -Force | Out-Null }
Copy-Item texoverride.asi $stage
Copy-Item tools\tex_overrides_README.txt (Join-Path $tex "README.txt")

if (Test-Path $Out) { Remove-Item $Out -Force }
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $Out -CompressionLevel Optimal

$n = [IO.Compression.ZipFile]::OpenRead((Resolve-Path $Out)).Entries.Count
"packed $Out with $($names.Count) collection folders, $n zip entries"
