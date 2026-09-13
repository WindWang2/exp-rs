# bundle_manifest.ps1 - manifest writer/verifier for the offline classroom
# bundle (goal D7). Contract: packaging/OFFLINE_BUNDLE.md. Twin of the Python
# block inside scripts/build_offline_bundle.sh.
#
# Windows PowerShell 5.1 constraints honored here (no pwsh dependency):
#   * System.IO.Path.GetRelativePath does not exist on .NET Framework -
#     relative paths are computed by substring;
#   * a function's return value shares the output stream with Write-Output,
#     so Test-Bundle reports its verdict through $script:BUNDLE_VERDICT
#     instead of `return` (a multi-element return is always truthy).
param(
  [Parameter(Mandatory = $false)] [string] $Bundle,
  [Parameter(Mandatory = $false)] [string] $Version,
  [Parameter(Mandatory = $false)] [int] $MaxMb = 250,
  [Parameter(Mandatory = $false)] [string] $Verify
)

$ErrorActionPreference = "Stop"
$script:BUNDLE_VERDICT = $false

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
    Write-Output "BUNDLE VERIFY FAIL $root (no manifest.json)"; return
  }
  $m = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
  if ($m.schema -ne "sicnu.offline_bundle/1") {
    Write-Output "BUNDLE VERIFY FAIL $root (bad manifest schema: $($m.schema))"; return
  }
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
  $mb = [math]::Round($total / 1MB, 1)
  $verdict = "PASS"; if ($bad.Count -gt 0 -or $mb -gt $m.size_ceiling_mb) { $verdict = "FAIL" }
  Write-Output "BUNDLE VERIFY $verdict $root ($count files, $mb MB / ceiling $($m.size_ceiling_mb) MB)"
  foreach ($b in $bad) { Write-Output "  $b" }
  if ($mb -gt $m.size_ceiling_mb) { Write-Output "  size $mb MB exceeds ceiling $($m.size_ceiling_mb) MB" }
  $script:BUNDLE_VERDICT = ($verdict -eq "PASS")
}

if ($Verify -ne "") {
  Test-Bundle $Verify
  if ($script:BUNDLE_VERDICT) { exit 0 } else { exit 1 }
}

if ($Bundle -eq "" -or $Version -eq "") {
  Write-Error "bundle_manifest.ps1: -Bundle/-Version (or -Verify) required"; exit 2
}

$required = @("bin/", "data/samples/", "data/labs/grading/", "data/fonts/",
              "data/runtime/proj/", "labs/lab1/",
              "RUN.cmd", "GENERATE_SAMPLES.cmd", "GRADE_ALL.cmd", "VERIFY.cmd",
              "VERIFY.ps1", "README-zh.md", "manifest.json")
$files = Get-ManifestFiles $Bundle
$manifest = [ordered] @{
  schema = "sicnu.offline_bundle/1"
  bundle_version = $Version
  created_utc = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:sszzz")
  size_ceiling_mb = $MaxMb
  required = $required
  files = $files
}
$path = Join-Path $Bundle "manifest.json"
$json = $manifest | ConvertTo-Json -Depth 4
[System.IO.File]::WriteAllText($path, $json, (New-Object System.Text.UTF8Encoding($false)))
Write-Output "manifest: $($files.Count) files"
exit 0
