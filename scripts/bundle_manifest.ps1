# bundle_manifest.ps1 — manifest writer/verifier for the offline classroom
# bundle (goal D7). Contract: packaging/OFFLINE_BUNDLE.md. Twin of the Python
# block inside scripts/build_offline_bundle.sh.
param(
  [Parameter(Mandatory = $false)] [string] $Bundle,
  [Parameter(Mandatory = $false)] [string] $Version,
  [Parameter(Mandatory = $false)] [int] $MaxMb = 250,
  [Parameter(Mandatory = $false)] [string] $Verify
)

$ErrorActionPreference = "Stop"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::SystemDefault

function Get-ManifestFiles([string] $root) {
  $files = @()
  Get-ChildItem -LiteralPath $root -Recurse -File | ForEach-Object {
    $rel = [IO.Path]::GetRelativePath($root, $_.FullName).Replace("\", "/")
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
  $manifestPath = Join-Path $root "manifest.json"
  if (-not (Test-Path -LiteralPath $manifestPath)) {
    Write-Output "BUNDLE VERIFY FAIL $root (no manifest.json)"; return $false
  }
  $m = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
  if ($m.schema -ne "sicnu.offline_bundle/1") {
    Write-Output "BUNDLE VERIFY FAIL $root (bad manifest schema: $($m.schema))"; return $false
  }
  $bad = @(); $total = [long] 0; $count = 0
  $byPath = @{}
  foreach ($f in $m.files) { $byPath[$f.path] = $f }
  foreach ($f in $m.files) {
    $count++
    $p = Join-Path $root ($f.path -replace "/", "\")
    if (-not (Test-Path -LiteralPath $p)) { $bad += "missing $($f.path)"; continue }
    $len = (Get-Item -LiteralPath $p).Length; $total += $len
    if ($len -ne $f.bytes) { $bad += "size mismatch $($f.path): $len != $($f.bytes)"; continue }
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
      $stream = [IO.File]::OpenRead($p)
      try {
        $hash = ($sha.ComputeHash($stream) | ForEach-Object { $_.ToString("x2") }) -join ""
        if ($hash -ne $f.sha256) { $bad += "sha256 mismatch $($f.path)" }
      } finally { $stream.Dispose() }
    } finally { $sha.Dispose() }
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
  return ($verdict -eq "PASS")
}

if ($Verify -ne "") { if (Test-Bundle $Verify) { exit 0 } else { exit 1 } }

if ($Bundle -eq "" -or $Version -eq "") {
  Write-Error "bundle_manifest.ps1: -Bundle/-Version (or -Verify) required"; exit 2
}

$required = @("bin/", "data/samples/", "data/labs/grading/", "data/fonts/", "labs/lab1/",
              "RUN.cmd", "GENERATE_SAMPLES.cmd", "GRADE_ALL.cmd", "README-zh.md", "manifest.json")
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
