# VERIFY.ps1 - self-contained bundle verifier shipped INSIDE the offline
# classroom bundle (goal D7). Mirror of scripts/bundle_manifest.ps1's
# Test-Bundle, compacted for the target machine (Windows PowerShell 5.1:
# no [IO.Path]::GetRelativePath; verdict via $script:BUNDLE_VERDICT because
# a function return shares the output stream).
#
# Usage: VERIFY.cmd  (wrapper passes the bundle root = this file's directory)
param([string] $Root = (Split-Path -Parent $MyInvocation.MyCommand.Path))

$ErrorActionPreference = "Stop"
$script:BUNDLE_VERDICT = $false

function Get-RelPath([string] $root, [string] $fullName) {
  $r = $root.TrimEnd('\')
  if (-not $fullName.StartsWith($r, [StringComparison]::OrdinalIgnoreCase)) {
    throw "path outside bundle root: $fullName"
  }
  return $fullName.Substring($r.Length).TrimStart('\').Replace('\', '/')
}

$manifestPath = Join-Path $Root "manifest.json"
if (-not (Test-Path -LiteralPath $manifestPath)) {
  Write-Output "BUNDLE VERIFY FAIL $Root (no manifest.json)"; exit 1
}
$m = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($m.schema -ne "sicnu.offline_bundle/1") {
  Write-Output "BUNDLE VERIFY FAIL $Root (bad manifest schema)"; exit 1
}

$bad = @(); $total = [long] 0; $count = 0
$listed = @{}
foreach ($f in $m.files) { $listed[$f.path] = $true }

foreach ($f in $m.files) {
  $count++
  $rel = $f.path
  if ([IO.Path]::IsPathRooted($rel) -or $rel.StartsWith("..") -or $rel -eq "manifest.json") {
    $bad += "unsafe manifest path: $rel"; continue
  }
  $p = Join-Path $Root ($rel -replace "/", "\")
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

Get-ChildItem -LiteralPath $Root -Recurse -File | ForEach-Object {
  $rel = Get-RelPath $Root $_.FullName
  if ($rel -ne "manifest.json" -and -not $listed.ContainsKey($rel)) {
    $bad += "unlisted file: $rel"
  }
}

foreach ($req in $m.required) {
  if ($req.EndsWith("/")) {
    $hit = $false
    foreach ($f in $m.files) { if ($f.path.StartsWith($req)) { $hit = $true; break } }
    if (-not $hit) { $bad += "required prefix empty: $req" }
  } elseif (-not (Test-Path -LiteralPath (Join-Path $Root ($req -replace "/", "\")))) {
    $bad += "required missing: $req"
  }
}

$mb = [math]::Round($total / 1MB, 1)
$verdict = "PASS"; if ($bad.Count -gt 0 -or $mb -gt $m.size_ceiling_mb) { $verdict = "FAIL" }
Write-Output "BUNDLE VERIFY $verdict $Root ($count files, $mb MB / ceiling $($m.size_ceiling_mb) MB)"
foreach ($b in $bad) { Write-Output "  $b" }
if ($verdict -eq "PASS") { exit 0 } else { exit 1 }
