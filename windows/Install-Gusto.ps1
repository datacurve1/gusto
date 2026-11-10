#Requires -Version 5.1
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$GameDirectory)
$ErrorActionPreference = 'Stop'
$folder = (Resolve-Path -LiteralPath $GameDirectory).Path
$expectedGame = '2a8702ad9f722704f01355c0750be7d6f164a8b9c9128ba0cf286ea32b405b0e'
$expectedGraphics = 'ade556b68e98f6f39c5578f70431e11c0b9babcc15b8571eb22cf1e2b654417e'
if (Get-Process -Name eqgame -ErrorAction SilentlyContinue) {
    throw 'Close EverQuest before installing Gusto.'
}
$files = @{ 'eqgame.exe' = $expectedGame; 'EQGraphicsDX9.dll' = $expectedGraphics }
foreach ($name in $files.Keys) {
    $path = Join-Path $folder $name
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing $name." }
    if ((Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant() -ne $files[$name]) {
        throw "Unsupported $name. This build requires the May 10, 2013 RoF2 client."
    }
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
