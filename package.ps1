param([string]$BuildDirectory = (Join-Path $PSScriptRoot 'build'))
$ErrorActionPreference = 'Stop'
$destination = Join-Path $PSScriptRoot 'dist'
New-Item -ItemType Directory -Force $destination | Out-Null
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'HomeKeys_artefacts/Release/HomeKeys.exe') -Destination $destination -Force
foreach ($name in @('lessons','licenses')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $destination -Recurse -Force
}
foreach ($name in @('LICENSE','README.md','THIRD_PARTY_NOTICES.md','双层钢琴键位对照.md','五八度键位对照.md')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $destination -Force
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'docs') -Destination $destination -Recurse -Force
$exe = Join-Path $destination 'HomeKeys.exe'
[pscustomobject]@{file='HomeKeys.exe'; sha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash; version=(Get-Item -LiteralPath $exe).VersionInfo.ProductVersion; signatureStatus=[string](Get-AuthenticodeSignature -LiteralPath $exe).Status} | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $destination 'release-manifest.json')
Write-Output "Build output: $destination"
