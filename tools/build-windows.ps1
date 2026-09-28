# Adapted from RocketKnightAdventuresRecomp's validated release workflow.
param(
  [string]$Version = '0.5.0',
  [string]$BuildDir = 'build-release',
  [string]$CMake = 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe',
  [string]$Python = 'python',
  [switch]$NoPackage
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $root $BuildDir }
$build = [IO.Path]::GetFullPath($build)
# PowerShell 5.1 converts redirected native stderr to ErrorRecords. Check exit
# codes so ordinary CMake warnings do not become terminating PowerShell errors.
$ErrorActionPreference = 'Continue'
& $CMake -S $root -B $build -G 'Visual Studio 17 2022' -A x64 `
  "-DGENESIS_RECOMP_ROOT=$root/segagenesisrecomp" "-DS3_BUILD_VERSION=$Version" `
  "-DPython3_EXECUTABLE=$Python" -DSONIC_REVERSE_DEBUG=OFF -DGEN_ENABLE_TRACE=OFF `
  -DGEN_DEV_TRACE=OFF -DGENESIS_BUILD_COSIM=OFF -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
if ($LASTEXITCODE) { throw 'Release configure failed' }
& $CMake --build $build --config Release --parallel 8
if ($LASTEXITCODE) { throw 'Release build failed' }
$ErrorActionPreference = 'Stop'
if (-not $NoPackage) {
  & "$PSScriptRoot/make_release.ps1" -Version $Version -BuildDir "$build/Release" -Python $Python
}
