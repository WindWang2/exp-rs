# VERIFY.ps1 - self-contained bundle verifier shipped INSIDE the offline
# classroom bundle (goal D7; schema /2 support by deployment-packaging-11/F19).
# Mirror of scripts/bundle_manifest.ps1's Test-Bundle, compacted for the
# target machine (Windows PowerShell 5.1: no [IO.Path]::GetRelativePath;
# verdict via $script:BUNDLE_VERDICT because a function return shares the
# output stream).
#
# Schemas: accepts /1 (field bundles) and /2 (default since F19); any other
# major is refused with exit 2 — a newer bundle must never half-verify.
#
# Usage: VERIFY.cmd  (wrapper passes the bundle root = this file's directory)
#        powershell -File VERIFY.ps1 [-Runtime]  (-Runtime additionally runs
#        bin\sicnu_geo_rs_cli env-doctor; its failure fails the script)
param(
  [string] $Root = (Split-Path -Parent $MyInvocation.MyCommand.Path),
  [switch] $Runtime
)

$ErrorActionPreference = "Stop"
$script:BUNDLE_VERDICT = $false
$supportedSchemas = @("sicnu.offline_bundle/1", "sicnu.offline_bundle/2")

function Get-RelPath([string] $root, [string] $fullName) {
  $r = $root.TrimEnd('\')
  if (-not $fullName.StartsWith($r, [StringComparison]::OrdinalIgnoreCase)) {
    throw "path outside bundle root: $fullName"
  }
  return $fullName.Substring($r.Length).TrimStart('\').Replace('\', '/')
}

$manifestPath = Join-Path $Root "manifest.json"
if (-not (Test-Path -LiteralPath $manifestPath)) {
  Write-Output "cannot verify: no manifest.json under $Root"; exit 2
}
$m = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($supportedSchemas -notcontains $m.schema) {
  Write-Output ("cannot verify: unsupported manifest schema: '{0}' (this reader supports {1})" -f `
    $m.schema, ($supportedSchemas -join " / "))
  exit 2
}
if ($m.schema -eq "sicnu.offline_bundle/2" -and $null -ne $m.compat -and $null -ne $m.compat.min_reader_schema) {
  if ($m.compat.min_reader_schema -gt 2) {
    Write-Output ("cannot verify: manifest requires reader schema {0}, this reader is 2" -f `
      $m.compat.min_reader_schema)
    exit 2
  }
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
if ($verdict -ne "PASS") { exit 1 }

# -Runtime: first-run environment self-check through the shipped CLI
# (F19). Integrity stays the primary gate; the runtime check reports and
# fails the script only when it cannot run healthy/degraded.
if ($Runtime) {
  $cli = Join-Path $Root "bin\sicnu_geo_rs_cli.exe"
  if (-not (Test-Path -LiteralPath $cli)) {
    Write-Output "RUNTIME CHECK: skipped (bin\sicnu_geo_rs_cli.exe not present in this bundle)"
    exit 0
  }
  & $cli env-doctor
  $runtimeExit = $LASTEXITCODE
  if ($runtimeExit -ne 0) {
    Write-Output "RUNTIME CHECK: issues found (env-doctor exit $runtimeExit; 0=healthy, 2=degraded/broken)"
    exit 1
  }
  Write-Output "RUNTIME CHECK: healthy"
}
exit 0
