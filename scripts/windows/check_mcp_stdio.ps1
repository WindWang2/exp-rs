# check_mcp_stdio.ps1 — one MCP discovery round-trip over stdio (goal D7/F).
# Spawns the exp-rs binary with --mcp, speaks newline-delimited JSON-RPC, and
# asserts: initialize -> serverInfo.name == "exp-rs-mcp"; tools/list -> array.
# The Linux smoke (docs/deployment/lab-offline.md) runs the same protocol.
param(
  [Parameter(Mandatory = $true)] [string] $Exe,
  [int] $TimeoutSeconds = 60
)

$ErrorActionPreference = "Stop"

$init = '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"check_mcp","version":"1.0"}}}'
$initialized = '{"jsonrpc":"2.0","method":"notifications/initialized"}'
$list = '{"jsonrpc":"2.0","id":2,"method":"tools/list"}'

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $Exe
$psi.Arguments = "--mcp"
$psi.UseShellExecute = $false
$psi.RedirectStandardInput = $true
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$psi.StandardOutputEncoding = [System.Text.Encoding]::UTF8
$psi.EnvironmentVariables["QT_QPA_PLATFORM"] = "offscreen"
$psi.EnvironmentVariables["SICNU_OFFLINE"] = "1"

$proc = [System.Diagnostics.Process]::Start($psi)
try {
  foreach ($msg in @($init, $initialized, $list)) {
    $proc.StandardInput.WriteLine($msg)
    $proc.StandardInput.Flush()
  }

  $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
  $initOk = $false; $toolsOk = $false; $toolCount = -1
  while ([DateTime]::UtcNow -lt $deadline -and (-not $initOk -or -not $toolsOk)) {
    if ($proc.HasExited) { break }
    $lineTask = $proc.StandardOutput.ReadLineAsync()
    while (-not $lineTask.IsCompleted) {
      if ([DateTime]::UtcNow -ge $deadline -or $proc.HasExited) { break }
      Start-Sleep -Milliseconds 100
    }
    if (-not $lineTask.IsCompleted) { break }
    $line = $lineTask.Result
    if ([string]::IsNullOrWhiteSpace($line)) { continue }
    try { $msg = $line | ConvertFrom-Json } catch { continue }
    if ($msg.id -eq 1 -and $msg.result) {
      if ($msg.result.serverInfo.name -eq "exp-rs-mcp") { $initOk = $true }
      else { Write-Error "unexpected serverInfo.name: $($msg.result.serverInfo.name)"; }
    }
    if ($msg.id -eq 2 -and $msg.result -and $msg.result.tools) {
      $toolsOk = $true
      $toolCount = @($msg.result.tools).Count
    }
  }

  if ($initOk -and $toolsOk) {
    Write-Output "MCP STDIO CHECK PASS $Exe (initialize ok, tools/list ok, $toolCount tools)"
    exit 0
  }
  Write-Output "MCP STDIO CHECK FAIL $Exe (initOk=$initOk toolsOk=$toolsOk exit=$($proc.HasExited))"
  exit 1
} finally {
  if (-not $proc.HasExited) {
    try { $proc.Kill(); $proc.WaitForExit(5000) | Out-Null } catch {}
  }
}
