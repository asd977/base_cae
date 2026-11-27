<# Installs the pinned GMDS sources and header dependencies under ignored libs/. #>
[CmdletBinding()]
param([string]$LibraryRoot = (Join-Path $PSScriptRoot '..\libs'))
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$revision = '6c8090df3b3dab9c087053661d83b12831d766b3'
[void][IO.Directory]::CreateDirectory($LibraryRoot)
$LibraryRoot = (Resolve-Path -LiteralPath $LibraryRoot).ProviderPath
$source = Join-Path $LibraryRoot 'gmds-src'
if (-not (Test-Path -LiteralPath $source)) {
    & git clone --no-checkout --filter=blob:none --depth 1 'https://github.com/LIHPC-Computational-Geometry/gmds.git' $source
    if ($LASTEXITCODE -ne 0) { throw 'GMDS clone failed' }
    & git -C $source fetch --depth 1 origin $revision
    if ($LASTEXITCODE -ne 0) { throw 'GMDS fetch failed' }
    & git -C $source checkout --detach $revision
    if ($LASTEXITCODE -ne 0) { throw 'GMDS checkout failed' }
}
$actual = (& git -C $source rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actual -ne $revision) {
    throw "GMDS source version differs from $revision. Choose a new -LibraryRoot; existing sources were preserved."
}
$downloads = Join-Path $LibraryRoot 'gmds-downloads'
[void][IO.Directory]::CreateDirectory($downloads)
function Get-VerifiedZip([string]$Name, [string]$Url, [string]$Sha256) {
    $archive = Join-Path $downloads $Name
    if (-not (Test-Path -LiteralPath $archive)) { Invoke-WebRequest -Uri $Url -OutFile $archive }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $Sha256) {
        throw "Archive checksum mismatch: $archive"
    }
    $unpacked = Join-Path $downloads ([IO.Path]::GetFileNameWithoutExtension($Name) + '-unpacked')
    if (-not (Test-Path -LiteralPath $unpacked)) { Expand-Archive -LiteralPath $archive -DestinationPath $unpacked }
    return $unpacked
}
$eigen = Join-Path $LibraryRoot 'eigen3'
if (-not (Test-Path -LiteralPath (Join-Path $eigen 'include\eigen3\Eigen\Core'))) {
    $unpacked = Get-VerifiedZip 'eigen-3.4.0.zip' 'https://gitlab.com/libeigen/eigen/-/archive/3.4.0/eigen-3.4.0.zip' 'EBA3F3D414D2F8CBA2919C78EC6DAAB08FC71BA2BA4AE502B7E5D4D99FC02CDA'
    $root = Join-Path $unpacked 'eigen-3.4.0'
    $include = Join-Path $eigen 'include\eigen3'
    [void][IO.Directory]::CreateDirectory($include)
    Copy-Item -LiteralPath (Join-Path $root 'Eigen') -Destination $include -Recurse
    Get-ChildItem -LiteralPath $root -Filter 'COPYING*' -File | Copy-Item -Destination $eigen
}
$cgal = Join-Path $LibraryRoot 'cgal'
if (-not (Test-Path -LiteralPath (Join-Path $cgal 'include\CGAL\Generalized_map.h'))) {
    $unpacked = Get-VerifiedZip 'CGAL-5.6.2.zip' 'https://github.com/CGAL/cgal/releases/download/v5.6.2/CGAL-5.6.2.zip' 'EC522D4E55DBA7E2E117E028239D392667338E32D123B659E4E135F20374FEC4'
    $root = Join-Path $unpacked 'CGAL-5.6.2'
    [void][IO.Directory]::CreateDirectory($cgal)
    Copy-Item -LiteralPath (Join-Path $root 'include') -Destination $cgal -Recurse
    foreach ($name in @('LICENSE', 'LICENSE.GPL', 'LICENSE.LGPL', 'LICENSE.BSL')) {
        $path = Join-Path $root $name
        if (Test-Path -LiteralPath $path) { Copy-Item -LiteralPath $path -Destination $cgal }
    }
}
Write-Host "GMDS $revision, Eigen 3.4.0 and CGAL 5.6.2 are ready in $LibraryRoot"
Write-Host 'Configure/build the application normally. Existing SMESH and Boost packages are reused.'
