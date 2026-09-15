[CmdletBinding()]
param(
    [string]$Configuration = "Release",
    [string]$QtRoot = "C:\Qt\6.8.3\msvc2022_64",
    [string]$FfmpegBin = "",
    [string]$OutputDirectory = "out"
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\")).Path
$binary = Join-Path $repoRoot "build\windows\$Configuration\LastFrame.exe"
$outputRoot = Join-Path $repoRoot $OutputDirectory
$package = Join-Path $outputRoot "LastFrameRelease"
$archive = Join-Path $outputRoot "LastFrame-windows-x64-portable.zip"
$windeployqt = Join-Path $QtRoot "bin\windeployqt.exe"

if (-not (Test-Path -LiteralPath $binary)) {
    throw "Build output not found: $binary"
}
if (-not (Test-Path -LiteralPath $windeployqt)) {
    throw "windeployqt not found: $windeployqt"
}
if ([string]::IsNullOrWhiteSpace($FfmpegBin)) {
    $FfmpegBin = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Packages\Gyan.FFmpeg.Shared_Microsoft.Winget.Source_8wekyb3d8bbwe\ffmpeg-9.0.1-full_build-shared\bin"
}
if (-not (Test-Path -LiteralPath (Join-Path $FfmpegBin "ffmpeg.exe"))) {
    throw "FFmpeg binary directory not found: $FfmpegBin"
}

New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
if (Test-Path -LiteralPath $package) {
    Remove-Item -LiteralPath $package -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $package | Out-Null

Copy-Item -LiteralPath $binary -Destination $package
& $windeployqt --release --no-translations --no-system-d3d-compiler --no-opengl-sw --compiler-runtime --network $binary --dir $package
Copy-Item -LiteralPath (Join-Path $FfmpegBin "ffmpeg.exe") -Destination $package -Force
Copy-Item -LiteralPath (Join-Path $FfmpegBin "ffprobe.exe") -Destination $package -Force
Get-ChildItem -LiteralPath $FfmpegBin -Filter "*.dll" -File | Copy-Item -Destination $package -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "LICENSE") -Destination $package -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "THIRD_PARTY_NOTICES.md") -Destination $package -Force

Compress-Archive -Path (Join-Path $package "*") -DestinationPath $archive -Force
Get-FileHash -LiteralPath $archive -Algorithm SHA256
