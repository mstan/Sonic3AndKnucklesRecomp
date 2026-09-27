# Stage allowlisted native binaries and dependency closures; never zip builds.
param(
  [Parameter(Mandatory = $true)][string]$Version,
  [string]$BuildDir = 'build-release/Release',
  [string]$Python = 'python',
  [string[]]$RuntimeBinDir = @()
)
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^\d+\.\d+\.\d+(-[A-Za-z0-9.]+)?$') { throw 'Invalid version' }
$root = Split-Path -Parent $PSScriptRoot
$build = if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $root $BuildDir }
$out = Join-Path $root 'release-stage'
. "$PSScriptRoot/release/RuntimeDllClosure.ps1"
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$modes = Get-Content -LiteralPath "$PSScriptRoot/release/modes.json" -Raw | ConvertFrom-Json
foreach ($mode in @('sonic3','sonic3k','sandk')) {
  $target = $modes.$mode.target
  $name = "$target-windows-x64-v$Version"
  $stage = Join-Path $out "$name-$([Guid]::NewGuid().ToString('N'))"
  $zip = Join-Path $out "$name.zip"
  New-Item -ItemType Directory -Path $stage -Force | Out-Null
  Copy-Item -LiteralPath (Join-Path $build "$target.exe") -Destination $stage
  Copy-Item -LiteralPath (Join-Path $build 'SDL2.dll') -Destination $stage
  & $Python "$PSScriptRoot/release/stage_payload.py" --build $build --dest $stage --version $Version --mode $mode
  if ($LASTEXITCODE) { throw 'Release payload validation failed' }
  $added = @(Copy-RuntimeDllClosure -StageDir $stage -SearchDirs (@($build) + $RuntimeBinDir))
  Assert-RuntimeDllClosure -StageDir $stage | Out-Null
  Write-Host "Staged $target dependency closure: $($added -join ', ')"
  $files = @(Get-ChildItem -LiteralPath $stage -File -Recurse | Sort-Object FullName)
  foreach ($file in $files) {
    $rel = $file.FullName.Substring($stage.Length + 1).Replace('\', '/')
    if ($rel -match '\.(bin|gen|smd|srm|ram|vram|log|map|pdb)$' -or
        $rel -match '(?i)(settings\.ini|rom.*\.cfg|mods\.ini|savestate|ramdump)') {
      throw "Forbidden release file: $rel"
    }
  }
  if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip }
  $archive = [IO.Compression.ZipFile]::Open($zip, [IO.Compression.ZipArchiveMode]::Create)
  try {
    foreach ($file in $files) {
      $entry = $file.FullName.Substring($stage.Length + 1).Replace('\', '/')
      [IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
        $archive, $file.FullName, $entry, [IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
  } finally { $archive.Dispose() }
  $archive = [IO.Compression.ZipFile]::OpenRead($zip)
  try {
    if ($archive.Entries.Count -ne $files.Count) { throw 'ZIP entry count mismatch' }
    foreach ($entry in $archive.Entries) {
      if ($entry.FullName.Contains('\') -or $entry.FullName.StartsWith('/') -or
          $entry.FullName -match '(^|/)\.\.(/|$)') { throw "Unsafe ZIP path: $($entry.FullName)" }
    }
  } finally { $archive.Dispose() }
  $hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
  [IO.File]::WriteAllText("$zip.sha256", "$hash  $name.zip`n", (New-Object Text.UTF8Encoding($false)))
  Write-Host "Built $zip ($($files.Count) files); SHA256 $hash"
}
