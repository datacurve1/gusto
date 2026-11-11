#Requires -Version 5.1
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$GameDirectory)
$ErrorActionPreference = 'Stop'
$folder = (Resolve-Path -LiteralPath $GameDirectory).Path
if (Get-Process -Name eqgame -ErrorAction SilentlyContinue) {
    throw 'Close EverQuest before installing Gusto.'
}
if (!(Test-Path -LiteralPath (Join-Path $folder 'eqgame.exe') -PathType Leaf)) {
    throw 'Missing eqgame.exe.'
}
$source = Join-Path $PSScriptRoot 'GUSTO.asi'
$checksum = Join-Path $PSScriptRoot 'GUSTO.asi.sha256'
$expected = ((Get-Content -LiteralPath $checksum -Raw).Trim() -split '\s+')[0].ToLowerInvariant()
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $source).Hash.ToLowerInvariant() -ne $expected) {
    throw 'Package checksum mismatch. Extract a fresh copy of the release.'
}
$target = Join-Path $folder 'GUSTO.asi'
if (Test-Path -LiteralPath $target) {
    $backup = Join-Path $folder ('GUSTO.before-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '.asi.bak')
    Copy-Item -LiteralPath $target -Destination $backup
    Write-Host "Backup: $backup"
}
Copy-Item -LiteralPath $source -Destination $target -Force
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $target).Hash.ToLowerInvariant() -ne $expected) {
    throw 'Installed checksum mismatch.'
}
Write-Host "Installed: $target"
Write-Host 'Keep your existing 32-bit ASI loader. See WINDOWS-INSTALL.md if this is a fresh install.'
Write-Host 'Start EverQuest, open /gusto, and assign controller keys to match EverQuest Options / Keys.'
