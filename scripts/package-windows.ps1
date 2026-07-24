param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9_.-]+$')]
    [string]$Architecture
)

# Create a portable Windows archive from a configured multi-configuration build.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$SourceDirectory = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$ResolvedBuildDirectory = (Resolve-Path $BuildDirectory).Path
$ReleaseDirectory = Join-Path $ResolvedBuildDirectory 'Release'
$Cache = Join-Path $ResolvedBuildDirectory 'CMakeCache.txt'
$Executable = Join-Path $ReleaseDirectory 'heresy.exe'

if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "Heresy Editor executable not found: $Executable"
}
if (-not (Test-Path -LiteralPath $Cache -PathType Leaf)) {
    throw "CMake cache not found: $Cache"
}

$VersionLine = Select-String -LiteralPath $Cache -Pattern '^CMAKE_PROJECT_VERSION:STATIC=(.+)$'
if ($null -eq $VersionLine -or $VersionLine.Matches.Count -ne 1) {
    throw "Could not determine the project version from $Cache"
}
$Version = $VersionLine.Matches[0].Groups[1].Value
if ($Version -notmatch '^[0-9]+(\.[0-9]+)*$') {
    throw "Invalid project version in ${Cache}: $Version"
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$ResolvedOutputDirectory = (Resolve-Path $OutputDirectory).Path
$PackageName = "Heresy-Editor-$Version-windows-$Architecture"
$Archive = Join-Path $ResolvedOutputDirectory "$PackageName.zip"
$TemporaryDirectory = Join-Path ([IO.Path]::GetTempPath()) "heresy-package-$([guid]::NewGuid())"
$PackageRoot = Join-Path $TemporaryDirectory $PackageName

try {
    New-Item -ItemType Directory -Force -Path $PackageRoot | Out-Null
    Copy-Item -Path (Join-Path $ReleaseDirectory '*') -Destination $PackageRoot -Recurse
    Copy-Item -LiteralPath @(
        (Join-Path $SourceDirectory 'AUTHORS.md'),
        (Join-Path $SourceDirectory 'GPL.txt'),
        (Join-Path $SourceDirectory 'INSTALL.txt'),
        (Join-Path $SourceDirectory 'README.md'),
        (Join-Path $SourceDirectory 'README.txt')
    ) -Destination $PackageRoot

    if (Test-Path -LiteralPath $Archive) {
        Remove-Item -LiteralPath $Archive -Force
    }
    Compress-Archive -LiteralPath $PackageRoot -DestinationPath $Archive -CompressionLevel Optimal

    $Hash = (Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash.ToLowerInvariant()
    "$Hash  $PackageName.zip" | Set-Content -LiteralPath "$Archive.sha256" -Encoding ascii
}
finally {
    if (Test-Path -LiteralPath $TemporaryDirectory) {
        Remove-Item -LiteralPath $TemporaryDirectory -Recurse -Force
    }
}

Write-Output $Archive
