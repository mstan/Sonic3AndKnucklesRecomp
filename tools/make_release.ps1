# Stage allowlisted native binaries and dependency closures; never zip builds.
param(
  [Parameter(Mandatory = $true)][string]$Version,
  [string]$BuildDir = 'build-release/Release',
  [string]$Python = 'python',
  [string[]]$RuntimeBinDir = @(),
  [string]$EngineRoot = ''
)
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^\d+\.\d+\.\d+(-[A-Za-z0-9.]+)?$') { throw 'Invalid version' }
$root = Split-Path -Parent $PSScriptRoot
$build = if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $root $BuildDir }
$out = Join-Path $root 'release-stage'
$modes = Get-Content -LiteralPath "$PSScriptRoot/release/modes.json" -Raw | ConvertFrom-Json
$builderEngine = if ($EngineRoot) { $EngineRoot } else { Join-Path $root 'segagenesisrecomp' }
. "$builderEngine/tools/release/GenesisRelease.ps1"
$ErrorActionPreference = 'Continue'
& $Python "$PSScriptRoot/make_campaign_builder.py" --out $build `
  --work (Join-Path (Split-Path -Parent $build) 'campaign-builder') --engine-root $builderEngine
$builderExit=$LASTEXITCODE
$ErrorActionPreference='Stop'
if ($builderExit) { throw 'Bundled owner-media converter build failed' }
foreach ($mode in @('sonic3','sonic3k','sandk')) {
  $target = $modes.$mode.target
  $name = "$target-windows-x64-v$Version"
  $stage = Join-Path $out "$name-$([Guid]::NewGuid().ToString('N'))"
  $zip = Join-Path $out "$name.zip"
  New-Item -ItemType Directory -Path $stage -Force | Out-Null
  Copy-Item -LiteralPath (Join-Path $build "$target.exe") -Destination $stage
  Copy-Item -LiteralPath (Join-Path $build 'SDL2.dll') -Destination $stage
  $ErrorActionPreference = 'Continue'
  $payloadArgs = @('--build', $build, '--dest', $stage, '--version', $Version, '--mode', $mode)
  if ($EngineRoot) { $payloadArgs += @('--engine-root', $EngineRoot) }
  & $Python "$PSScriptRoot/release/stage_payload.py" @payloadArgs
  $payloadExit = $LASTEXITCODE
  $ErrorActionPreference = 'Stop'
  if ($payloadExit) { throw 'Release payload validation failed' }
  Complete-GenesisReleaseStage -StageDir $stage -Archive $zip -SearchDirs (@($build) + $RuntimeBinDir)
}
