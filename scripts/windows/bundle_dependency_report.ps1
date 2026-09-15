# bundle_dependency_report.ps1 - Windows twin of
# scripts/report_bundle_dependencies.py (F19 dependency inventory).
#
# Writes <bundle>\dependencies.json (schema "exp.bundle.deps.v1") classifying
# every direct import of the PE images in the bundle's bin directory as:
#   shipped    - provided by a file inside the bundle
#   system     - a Windows system/known DLL (kernel32.dll, user32.dll, ...)
#   unresolved - neither (this is the actionable class: the deployment
#                machine will fail to load the binary)
#
# Import tables are read with dumpbin when available (VS toolchain, discovered
# like scripts/windows/_env.cmd does); without dumpbin the script degrades to
# a typed skip entry per file - it never guesses imports from filenames.
# The report lands before the manifest is written, so the manifest hashes it.
#
# PowerShell 5.1 constraints honored (no pwsh dependency).
# Execution on Windows hosts only; the F19 Linux track validated this script
# statically (structure, quoting, PS 5.1 constructs) - see EVIDENCE.
param(
  [Parameter(Mandatory = $true)] [string] $Bundle,
  [Parameter(Mandatory = $false)] [string] $Out = ""
)

$ErrorActionPreference = "Stop"

$systemDlls = @(
  "advapi32.dll", "kernel32.dll", "msvcrt.dll", "ntdll.dll", "user32.dll",
  "ws2_32.dll", "shell32.dll", "ole32.dll", "oleaut32.dll", "gdi32.dll",
  "gdiplus.dll", "shlwapi.dll", "comctl32.dll", "comdlg32.dll", "bcrypt.dll",
  "ncrypt.dll", "crypt32.dll", "secur32.dll", "userenv.dll", "winmm.dll",
  "ws2help.dll", "wtsapi32.dll", "dbghelp.dll", "psapi.dll", "setupapi.dll",
  "version.dll", "wintrust.dll", "dwmapi.dll", "uxtheme.dll", "d3d11.dll",
  "dxgi.dll", "opengl32.dll", "glu32.dll", "iphlpapi.dll", "dnsapi.dll",
  "netapi32.dll", "powrprof.dll", "propsys.dll", "winspool.drv"
)

function Resolve-Dumpbin {
  $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
  if (Test-Path -LiteralPath $vswhere) {
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
    if ($vsPath) {
      $candidate = Get-ChildItem -Path (Join-Path $vsPath "VC\Tools\MSVC") -Recurse -Filter dumpbin.exe -ErrorAction SilentlyContinue |
        Select-Object -First 1
      if ($candidate) { return $candidate.FullName }
    }
  }
  $onPath = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
  if ($onPath) { return $onPath.Source }
  return ""
}

function Get-Imports([string] $dumpbin, [string] $imagePath) {
  if ($dumpbin -eq "") {
    return @{ imports = $null; reason = "dumpbin unavailable" }
  }
  $out = & $dumpbin /DEPENDENTS /nologo $imagePath 2>$null
  if ($LASTEXITCODE -ne 0 -or -not $out) {
    return @{ imports = $null; reason = "dumpbin failed" }
  }
  $imports = @()
  $inSection = $false
  foreach ($line in $out) {
    if ($line -match "Image has the following dependencies") { $inSection = $true; continue }
    if ($inSection) {
      if ($line -match "Image has the following delay load" -or $line -match "Summary") { break }
      $name = $line.Trim()
      if ($name -match '\.dll$') { $imports += $name.ToLowerInvariant() }
    }
  }
  return @{ imports = $imports; reason = "" }
}

$binDir = Join-Path $Bundle "bin"
if (-not (Test-Path -LiteralPath $binDir)) {
  Write-Error "bundle_dependency_report: no bin directory under $Bundle"
  exit 2
}

$dumpbin = Resolve-Dumpbin
$shipped = @{}
Get-ChildItem -LiteralPath $binDir -File | ForEach-Object { $shipped[$_.Name.ToLowerInvariant()] = $true }

$entries = @()
$skipped = 0
Get-ChildItem -LiteralPath $binDir -File | Where-Object { $_.Extension -match '^\.(exe|dll)$' } |
ForEach-Object {
  $rel = "bin/" + $_.Name
  $result = Get-Imports $dumpbin $_.FullName
  if ($null -eq $result.imports) {
    $entries += [ordered] @{ file = $rel; status = "skipped"; reason = $result.reason }
    $skipped++
    return
  }
  foreach ($dep in $result.imports) {
    $status = "unresolved"
    if ($shipped.ContainsKey($dep)) { $status = "shipped" }
    elseif ($systemDlls -contains $dep) { $status = "system" }
    $entries += [ordered] @{ file = $rel; dependency = $dep; status = $status }
  }
}

$counts = @{ shipped = 0; system = 0; unresolved = 0; skipped = 0 }
foreach ($e in $entries) { $counts[$e.status]++ }

$report = [ordered] @{
  schema = "exp.bundle.deps.v1"
  bundle = (Split-Path -Leaf $Bundle)
  tool = if ($dumpbin -ne "") { "dumpbin" } else { "none" }
  note = "shipped=provided by this bundle; system=Windows system DLL; " +
         "unresolved=not provided and not a system DLL (deployment machines " +
         "will fail to load); skipped=import table unreadable (no dumpbin)"
  counts = $counts
  dependencies = $entries
}
$outPath = if ($Out -ne "") { $Out } else { Join-Path $Bundle "dependencies.json" }
$json = $report | ConvertTo-Json -Depth 4
[System.IO.File]::WriteAllText($outPath, $json, (New-Object System.Text.UTF8Encoding($false)))
Write-Output ("dependencies: {0} shipped, {1} system, {2} unresolved, {3} skipped -> {4}" -f `
  $counts.shipped, $counts.system, $counts.unresolved, $counts.skipped, $outPath)
exit 0
