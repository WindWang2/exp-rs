# bundle_manifest.ps1 - manifest writer/verifier for the offline classroom
# bundle (goal D7; schema /2 added by deployment-packaging-11/F19). Contract:
# packaging/OFFLINE_BUNDLE.md. Twin of scripts/verify_bundle_manifest.py (the
# canonical POSIX/tests verifier) and of the writer block inside
# scripts/build_offline_bundle.sh.
#
# Windows PowerShell 5.1 constraints honored here (no pwsh dependency):
#   * System.IO.Path.GetRelativePath does not exist on .NET Framework -
#     relative paths are computed by substring;
#   * a function's return value shares the output stream with Write-Output,
#     so Test-Bundle reports its verdict through $script:BUNDLE_VERDICT
#     instead of `return` (a multi-element return is always truthy).
#
# Schemas: writes /2 by default (-Schema 1 for the legacy shape). Verifying
# accepts both /1 and /2 and refuses any other major with exit 2 (a newer
# bundle must never half-verify). The POSIX verifier's escaping-symlink rule
# is intentionally not mirrored: Windows builders ship dereferenced files
# (robocopy default), so a bundle-side symlink is a POSIX-shaped anomaly.
param(
  [Parameter(Mandatory = $false)] [string] $Bundle,
  [Parameter(Mandatory = $false)] [string] $Version,
  [Parameter(Mandatory = $false)] [int] $MaxMb = 250,
  [Parameter(Mandatory = $false)] [ValidateSet("1", "2")] [string] $Schema = "2",
  [Parameter(Mandatory = $false)] [hashtable] $Components,
  [Parameter(Mandatory = $false)] [hashtable] $BuildOptions,
  [Parameter(Mandatory = $false)] [string] $ComponentsFromBin,
  [Parameter(Mandatory = $false)] [string] $Verify
)

$ErrorActionPreference = "Stop"
$script:BUNDLE_VERDICT = $false
$script:VERIFY_EXIT = 1

$script:SUPPORTED_SCHEMAS = @("sicnu.offline_bundle/1", "sicnu.offline_bundle/2")

# Substring-based relative path (PS 5.1 has no [IO.Path]::GetRelativePath).
function Get-RelPath([string] $root, [string] $fullName) {
  $r = $root.TrimEnd('\')
  if (-not $fullName.StartsWith($r, [StringComparison]::OrdinalIgnoreCase)) {
    throw "path outside bundle root: $fullName"
  }
  return $fullName.Substring($r.Length).TrimStart('\').Replace('\', '/')
}

function Get-ManifestFiles([string] $root) {
  $files = @()
  Get-ChildItem -LiteralPath $root -Recurse -File | ForEach-Object {
    $rel = Get-RelPath $root $_.FullName
    if ($rel -eq "manifest.json") { return }
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
      $stream = [IO.File]::OpenRead($_.FullName)
      try {
        $hash = $sha.ComputeHash($stream) | ForEach-Object { $_.ToString("x2") }
        $files += [ordered] @{ path = $rel; bytes = $_.Length; sha256 = ($hash -join "") }
      } finally { $stream.Dispose() }
    } finally { $sha.Dispose() }
  }
  return ($files | Sort-Object { $_.path })
}

function Test-Bundle([string] $root) {
  $script:BUNDLE_VERDICT = $false
  $root = (Get-Item -LiteralPath $root).FullName
  $manifestPath = Join-Path $root "manifest.json"
  if (-not (Test-Path -LiteralPath $manifestPath)) {
    Write-Output "cannot verify: no manifest.json under $root"
    $script:VERIFY_EXIT = 2
    return
  }
  $m = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
  if ($script:SUPPORTED_SCHEMAS -notcontains $m.schema) {
    Write-Output ("cannot verify: unsupported manifest schema: '{0}' (this reader supports {1})" -f `
      $m.schema, ($script:SUPPORTED_SCHEMAS -join " / "))
    $script:VERIFY_EXIT = 2
    return
  }
  $script:VERIFY_EXIT = 1
  $bad = @(); $total = [long] 0; $count = 0
  $listed = @{}
  foreach ($f in $m.files) { $listed[$f.path] = $true }
  foreach ($f in $m.files) {
    $count++
    $rel = $f.path
    # Completeness, not authenticity: reject entries that escape the bundle.
    if ([IO.Path]::IsPathRooted($rel) -or $rel.StartsWith("..") -or $rel -eq "manifest.json") {
      $bad += "unsafe manifest path: $rel"; continue
    }
    $p = Join-Path $root ($rel -replace "/", "\")
    if (-not (Test-Path -LiteralPath $p)) { $bad += "missing $rel"; continue }
    $len = (Get-Item -LiteralPath $p).Length; $total += $len
    if ($len -ne $f.bytes) { $bad += "size mismatch ${rel}: $len != $($f.bytes)"; continue }
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
      $stream = [IO.File]::OpenRead($p)
      try {
        $hash = ($sha.ComputeHash($stream) | ForEach-Object { $_.ToString("x2") }) -join ""
        if ($hash -ne $f.sha256) { $bad += "sha256 mismatch $rel" }
      } finally { $stream.Dispose() }
    } finally { $sha.Dispose() }
  }
  # files[] must cover EVERY regular file: re-walk and flag unlisted ones.
  Get-ChildItem -LiteralPath $root -Recurse -File | ForEach-Object {
    $rel = Get-RelPath $root $_.FullName
    if ($rel -ne "manifest.json" -and -not $listed.ContainsKey($rel)) {
      $bad += "unlisted file: $rel"
    }
  }
  foreach ($req in $m.required) {
    if ($req.EndsWith("/")) {
      $hit = $false
      foreach ($f in $m.files) { if ($f.path.StartsWith($req)) { $hit = $true; break } }
      if (-not $hit) { $bad += "required prefix empty: $req" }
    } elseif (-not (Test-Path -LiteralPath (Join-Path $root ($req -replace "/", "\")))) {
      $bad += "required missing: $req"
    }
  }
  # /2 declared-provenance sections: shape-checked only (structure, never
  # content) — the values are builder declarations, not derived facts.
  if ($m.schema -eq "sicnu.offline_bundle/2") {
    foreach ($key in @("components", "build_options", "compat")) {
      $section = $m.$key
      if ($null -ne $section -and $section.GetType().Name -ne "PSCustomObject") {
        $bad += "$key must be an object"
      }
    }
    if ($null -ne $m.compat -and $null -ne $m.compat.min_reader_schema) {
      if ($m.compat.min_reader_schema -gt 2) {
        $bad += ("compat.min_reader_schema {0} exceeds this reader's newest schema (2)" -f `
          $m.compat.min_reader_schema)
      }
    }
  }
  $mb = [math]::Round($total / 1MB, 1)
  $verdict = "PASS"; if ($bad.Count -gt 0 -or $mb -gt $m.size_ceiling_mb) { $verdict = "FAIL" }
  Write-Output "BUNDLE VERIFY $verdict $root ($count files, $mb MB / ceiling $($m.size_ceiling_mb) MB)"
  foreach ($b in $bad) { Write-Output "  $b" }
  if ($mb -gt $m.size_ceiling_mb) { Write-Output "  size $mb MB exceeds ceiling $($m.size_ceiling_mb) MB" }
  $script:BUNDLE_VERDICT = ($verdict -eq "PASS")
}

if ($Verify -ne "") {
  Test-Bundle $Verify
  if (-not $script:BUNDLE_VERDICT) { exit $script:VERIFY_EXIT }
  exit 0
}

if ($Bundle -eq "" -or $Version -eq "") {
  Write-Error "bundle_manifest.ps1: -Bundle/-Version (or -Verify) required"; exit 2
}

$required = @("bin/", "data/samples/", "data/labs/grading/", "data/fonts/",
              "data/runtime/proj/", "labs/lab1/",
              "RUN.cmd", "GENERATE_SAMPLES.cmd", "GRADE_ALL.cmd", "VERIFY.cmd",
              "VERIFY.ps1", "README-zh.md", "manifest.json")
$schemaString = "sicnu.offline_bundle/$Schema"
if ($Schema -eq "2") {
  # /2 ships the in-bundle Linux verifier and its canonical engine.
  $required = $required + @("VERIFY.sh", "tools/verify_bundle_manifest.py")
}
# F19: collect runtime component versions from the shipped DLLs' version
# resources (best-effort — a missing DLL simply omits its entry; the version
# strings are the files' own declarations, never derived facts).
function Get-ComponentsFromBin([string] $binDir) {
  $known = @{ "qt6core.dll" = "qt"; "qgis_core.dll" = "qgis"; "gdal.dll" = "gdal";
              "proj.dll" = "proj"; "geos_c.dll" = "geos"; "sqlite3.dll" = "sqlite" }
  $components = [ordered] @{}
  foreach ($dll in (Get-ChildItem -LiteralPath $binDir -Filter *.dll -ErrorAction SilentlyContinue)) {
    $base = $dll.Name.ToLowerInvariant()
    if ($known.ContainsKey($base) -and -not $components.Contains($known[$base])) {
      $version = $dll.VersionInfo.FileVersion
      if ($version) {
        $components[$known[$base]] = [ordered] @{ version = $version; source = "dll_version_resource" }
      }
    }
  }
  return $components
}

if ($ComponentsFromBin -ne "") {
  $fromBin = Get-ComponentsFromBin $ComponentsFromBin
  if ($Components) {
    foreach ($k in $Components.Keys) { $fromBin[$k] = $Components[$k] }
  }
  $Components = $fromBin
}

$files = Get-ManifestFiles $Bundle
$manifest = [ordered] @{
  schema = $schemaString
  bundle_version = $Version
  created_utc = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:sszzz")
  size_ceiling_mb = $MaxMb
  required = $required
  files = $files
}
if ($Schema -eq "2") {
  if ($Components) { $manifest.components = $Components }
  if ($BuildOptions) { $manifest.build_options = $BuildOptions }
  $manifest.compat = [ordered] @{ min_reader_schema = 1; bundle_kind = "lab-cli" }
}
$path = Join-Path $Bundle "manifest.json"
$json = $manifest | ConvertTo-Json -Depth 4
[System.IO.File]::WriteAllText($path, $json, (New-Object System.Text.UTF8Encoding($false)))
Write-Output "manifest: $($files.Count) files (schema $schemaString)"
exit 0
