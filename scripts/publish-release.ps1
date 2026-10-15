# Upload all validated platform/IDA-version ZIPs to a draft GitHub Release.
# Run scripts/build-all.py on each native OS and collect artifacts first.
param(
    [string]$StageDir = "",
    [string]$IdaVersion = "",
    [switch]$Publish,
    [string]$DistDir = "dist"
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not [System.IO.Path]::IsPathRooted($DistDir)) {
    $DistDir = Join-Path $root $DistDir
}
$manifest = Get-Content -LiteralPath (Join-Path $root 'ida-plugin.json') -Raw | ConvertFrom-Json
$tag = 'v' + $manifest.plugin.version
if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
    throw 'GitHub CLI (gh) is required; install it and run gh auth login.'
}
& python (Join-Path $PSScriptRoot 'package-plugin.py') --check-manifest --tag $tag
if ($LASTEXITCODE -ne 0) { throw 'Release metadata validation failed.' }

if (-not [string]::IsNullOrWhiteSpace($StageDir)) {
    if ([string]::IsNullOrWhiteSpace($IdaVersion)) {
        throw 'Pass -IdaVersion (9.2, 9.3, or 9.4) when using -StageDir.'
    }
    if (-not [System.IO.Path]::IsPathRooted($StageDir)) {
        $StageDir = Join-Path $root $StageDir
    }
    & python (Join-Path $PSScriptRoot 'package-plugin.py') --stage $StageDir `
        --ida-version $IdaVersion --output $DistDir --tag $tag
    if ($LASTEXITCODE -ne 0) { throw 'Native ZIP packaging failed.' }
}

$assets = @(Get-ChildItem -LiteralPath $DistDir -Filter 'cantordust-ida-v*-ida*-*.zip' -File)
if ($assets.Count -eq 0) { throw "No native plugin release assets in $DistDir" }
# Validate each archive (entry point, scoped metadata, and version) before upload.
& python (Join-Path $PSScriptRoot 'build-all.py') --collect $DistDir --output $DistDir
if ($LASTEXITCODE -ne 0) { throw 'Native release asset validation failed.' }

& gh release view $tag --json tagName *> $null
if ($LASTEXITCODE -ne 0) {
    & gh release create $tag --draft --verify-tag --title "Cantor Dust $tag" --generate-notes
    if ($LASTEXITCODE -ne 0) { throw "Could not create draft release $tag" }
}
foreach ($asset in $assets) {
    & gh release upload $tag $asset.FullName --clobber
    if ($LASTEXITCODE -ne 0) { throw "Upload failed: $($asset.FullName)" }
}
if ($Publish) {
    & gh release edit $tag --draft=false
    if ($LASTEXITCODE -ne 0) { throw 'Could not publish release.' }
    Write-Host "Published $tag with $($assets.Count) archive(s)."
} else {
    Write-Host "Uploaded $($assets.Count) archive(s) to draft release $tag."
    Write-Host 'Review binaries and HCLI lint results, then rerun with -Publish.'
}
