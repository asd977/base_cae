<#
.SYNOPSIS
Creates an application update ZIP containing only the EXE, core.dll and plugin DLLs.
.EXAMPLE
./scripts/package-app.ps1
.EXAMPLE
./scripts/package-app.ps1 -SourceDir ./build/Desktop_Qt_6_7_3_MSVC2022_64bit_Release -OutputDir ./dist
#>
[CmdletBinding()]
param(
    [string]$SourceDir,
    [string]$OutputDir
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($SourceDir)) {
    $SourceDir = Join-Path $scriptDirectory '..\build\release'
}
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $scriptDirectory '..\dist'
}

$sourcePath = (Resolve-Path -LiteralPath $SourceDir).ProviderPath
$pluginPath = Join-Path $sourcePath 'plugins'
$exeName = 'POWERCHINA-GEOCAE.exe'
$files = @()

foreach ($name in @($exeName, 'core.dll')) {
    $path = Join-Path $sourcePath $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required file is missing: $path"
    }
    $files += [pscustomobject]@{ Path = $path; Entry = $name }
}

if (-not (Test-Path -LiteralPath $pluginPath -PathType Container)) {
    throw "Plugin directory is missing: $pluginPath"
}
$plugins = @(Get-ChildItem -LiteralPath $pluginPath -Filter '*.dll' -File -Recurse | Sort-Object FullName)
if ($plugins.Count -eq 0) {
    throw "No plugin DLLs found in: $pluginPath"
}
foreach ($plugin in $plugins) {
    $relative = $plugin.FullName.Substring($pluginPath.Length).TrimStart([char]'\', [char]'/')
    $files += [pscustomobject]@{
        Path = $plugin.FullName
        Entry = 'plugins/' + $relative.Replace([char]'\', [char]'/')
    }
}

[void][System.IO.Directory]::CreateDirectory($OutputDir)
$outputPath = (Resolve-Path -LiteralPath $OutputDir).ProviderPath
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss_fff'
$archivePath = Join-Path $outputPath "POWERCHINA-GEOCAE_update_$stamp.zip"

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$stream = $null
$archive = $null
$created = $false
try {
    # CreateNew preserves existing packages, including a same-name archive.
    $stream = [System.IO.File]::Open($archivePath, [System.IO.FileMode]::CreateNew)
    $created = $true
    $archive = [System.IO.Compression.ZipArchive]::new($stream, [System.IO.Compression.ZipArchiveMode]::Create)
    foreach ($file in $files) {
        [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $archive, $file.Path, $file.Entry, [System.IO.Compression.CompressionLevel]::Optimal)
    }
    $archive.Dispose()
    $archive = $null
    $stream.Dispose()
    $stream = $null
}
catch {
    if ($null -ne $archive) { $archive.Dispose(); $archive = $null }
    if ($null -ne $stream) { $stream.Dispose(); $stream = $null }
    if ($created) { Remove-Item -LiteralPath $archivePath -Force }
    throw
}
finally {
    if ($null -ne $archive) { $archive.Dispose() }
    if ($null -ne $stream) { $stream.Dispose() }
}

$sizeMB = (Get-Item -LiteralPath $archivePath).Length / 1MB
Write-Host ("Packaged {0} files ({1} plugins), {2:N2} MB" -f $files.Count, $plugins.Count, $sizeMB)
Write-Host "Package: $archivePath"
Write-Host 'Extract into an existing full installation to update the application.'
