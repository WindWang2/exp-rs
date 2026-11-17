param(
  [Parameter(Mandatory=$true)][string]$BuildDir,
  [Parameter(Mandatory=$true)][string]$RuntimeStage,
  [Parameter(Mandatory=$true)][string]$PackageRoot,
  [string]$QtDir='',
  [string]$QcaDir='',
  [string]$KeychainDir='',
  [string]$Cmake='cmake'
)
$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
$env:PYTHONDONTWRITEBYTECODE='1'
Remove-Item Env:PYTHONHOME,Env:PYTHONPATH -ErrorAction SilentlyContinue
$RepoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\','/')
$BuildDir=(Resolve-Path -LiteralPath $BuildDir).Path
$RuntimeStage=(Resolve-Path -LiteralPath $RuntimeStage).Path
$PackageRoot=[IO.Path]::GetFullPath($PackageRoot).TrimEnd('\','/')
$cache=Join-Path $BuildDir 'CMakeCache.txt'

function Cache-Value([string]$Name) {
  $match=Select-String -LiteralPath $cache -Pattern ('^'+[Regex]::Escape($Name)+':[^=]*=(.*)$') | Select-Object -First 1
  if($match){return $match.Matches[0].Groups[1].Value}
  return ''
}
function Write-Utf8([string]$Path,[string]$Text) {
  [IO.File]::WriteAllText($Path,$Text,(New-Object Text.UTF8Encoding($false)))
}
function Check-Exit([string]$What) {if($LASTEXITCODE -ne 0){throw "$What failed (exit $LASTEXITCODE)"}}
function Assert-NoReparseTree([string]$Path) {
  $pending=New-Object 'System.Collections.Generic.Stack[string]'
  $pending.Push($Path)
  while($pending.Count){
    $current=Get-Item -LiteralPath $pending.Pop() -Force
    if($current.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Refusing reparse point: $($current.FullName)"}
    if($current.PSIsContainer){Get-ChildItem -LiteralPath $current.FullName -Force | ForEach-Object {$pending.Push($_.FullName)}}
  }
}
function Assert-Owned([string]$Path) {
  $full=[IO.Path]::GetFullPath($Path)
  if(-not $full.StartsWith($PackageRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){
    throw "Refusing non-package path: $full"
  }
  # Copy destinations may not exist yet. Check every existing ancestor before
  # creating them; only recurse into a tree when it is already present.
  $ancestor=$full
  while($ancestor.Length -ge $PackageRoot.Length){
    $item=Get-Item -LiteralPath $ancestor -Force -ErrorAction Ignore
    if($item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw "Refusing package ancestor reparse point: $ancestor"}
    if($ancestor.Equals($PackageRoot,[StringComparison]::OrdinalIgnoreCase)){break}
    $ancestor=Split-Path -Parent $ancestor
  }
  if(Test-Path -LiteralPath $full){Assert-NoReparseTree $full}
}
function Copy-Tree([string]$Source,[string]$Destination) {
  if(-not(Test-Path -LiteralPath $Source -PathType Container)){return}
  Assert-NoReparseTree $Source
  [IO.Directory]::CreateDirectory($Destination) | Out-Null
  Get-ChildItem -LiteralPath $Source -Force | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination $Destination -Recurse -Force}
}
if(-not $QtDir){
  $qtPackage=Cache-Value 'Qt6_DIR'
  if(-not $qtPackage){throw 'QtDir or a configured Qt6_DIR is required.'}
  $QtDir=[IO.Path]::GetFullPath((Join-Path $qtPackage '../../..'))
}
if(-not $QcaDir){
  $qcaLibrary=Cache-Value 'QCA_LIBRARY'
  if(-not $qcaLibrary){throw 'QcaDir or a configured QCA_LIBRARY is required.'}
  $QcaDir=Split-Path -Parent (Split-Path -Parent $qcaLibrary)
}
if(-not $KeychainDir){
  $keychainPackage=Cache-Value 'Qt6Keychain_DIR'
  if(-not $keychainPackage){throw 'KeychainDir or a configured Qt6Keychain_DIR is required.'}
  $KeychainDir=[IO.Path]::GetFullPath((Join-Path $keychainPackage '../../..'))
}
if((Cache-Value 'CMAKE_BUILD_TYPE') -ne 'Release'){throw 'A Release build is required; Debug CRT packages are refused.'}
if((Cache-Value 'SICNU_EMBED_PYTHON') -ne 'OFF'){throw 'Embedded host Python must be disabled.'}
if((Cache-Value 'SICNU_LAB_SKIP_PYTHON_BINDINGS') -notin @('ON','TRUE','1')){throw 'Disable optional host-Python pybind bindings for this standalone package.'}
if((Cache-Value 'SICNU_BUNDLE_PYTHON') -ne 'ON'){throw 'SICNU_BUNDLE_PYTHON must be ON.'}
$configuredStage=[IO.Path]::GetFullPath((Cache-Value 'SICNU_PYTHON_RUNTIME_STAGE'))
if(-not $configuredStage.Equals($RuntimeStage,[StringComparison]::OrdinalIgnoreCase)){throw 'RuntimeStage differs from configured install component.'}
$allowed=@((Join-Path $RepoRoot 'dist'),(Join-Path $RepoRoot 'build-logs'))
if(-not($allowed | Where-Object {$PackageRoot.StartsWith($_+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)})){
  throw 'PackageRoot must be a fresh child of workspace dist/ or build-logs/.'
}
$ancestor=$PackageRoot
while($ancestor -and $ancestor.Length -gt $RepoRoot.Length){
  if(Test-Path -LiteralPath $ancestor){if((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Refusing output through reparse point: $ancestor"}}
  $ancestor=Split-Path -Parent $ancestor
}
if(Test-Path -LiteralPath $PackageRoot){if(@(Get-ChildItem -LiteralPath $PackageRoot -Force).Count){throw 'PackageRoot must be empty; choose a fresh directory.'}}
[IO.Directory]::CreateDirectory($PackageRoot) | Out-Null
$bin=Join-Path $PackageRoot 'bin'
$qtPlugins=Join-Path $bin 'plugins'
$pythonRoot=Join-Path $PackageRoot 'libexec\sicnu-geo-rs\python'
$sourcePython=Join-Path $RuntimeStage 'libexec\sicnu-geo-rs\python\python.exe'
$prepare=Join-Path $RepoRoot 'scripts\prepare_python_runtime.py'
$piEnabled=(Cache-Value 'SICNU_BUNDLE_PI') -in @('ON','TRUE','1')
$piPrepare=Join-Path $RepoRoot 'scripts\prepare_pi_product.py'
& $sourcePython -I -B $prepare --verify-stage $RuntimeStage
Check-Exit 'Pinned Python stage verification'
& $Cmake --install $BuildDir --config Release --prefix $PackageRoot
Check-Exit 'CMake application install'
# #2109: the plugin host worker now installs through install(TARGETS) in
# src/plugins/host/CMakeLists.txt. The old direct copy from the build tree
# silently skipped multi-config builds, where the worker lands in a
# per-config subdirectory — a missing worker is a hard packaging failure.
foreach($name in @('sicnu_geo_rs.exe','sicnu_geo_rs_cli.exe','sicnu_worker.exe','exprs_plugin_host_worker.exe')){
  if(-not(Test-Path -LiteralPath (Join-Path $bin $name))){throw "Missing installed application executable: $name"}
}

# Reconstruct only tracked data, excluding user-local/untracked sample files.
$data=Join-Path $PackageRoot 'share\sicnu_geo_rs\data'
if(Test-Path -LiteralPath $data){Assert-Owned $data; Remove-Item -LiteralPath $data -Recurse -Force}
[IO.Directory]::CreateDirectory($data) | Out-Null
$tracked=& git -C $RepoRoot -c core.quotepath=false ls-files -- data
Check-Exit 'Tracked data inventory'
foreach($relative in $tracked){
  $source=Join-Path $RepoRoot $relative
  if(-not(Test-Path -LiteralPath $source -PathType Leaf)){throw "Tracked package input is absent: $relative"}
  $target=Join-Path (Join-Path $PackageRoot 'share\sicnu_geo_rs') $relative
  Assert-Owned $target
  [IO.Directory]::CreateDirectory((Split-Path -Parent $target)) | Out-Null
  Copy-Item -LiteralPath $source -Destination $target -Force
}

# Native headers/import archives are not application runtime. The complete
# owned Python distribution, including its include/libs, is left unchanged.
foreach($name in @('include','lib')){
  $development=Join-Path $PackageRoot $name
  if(Test-Path -LiteralPath $development){
    if(@(Get-ChildItem -LiteralPath $development -Recurse -Force -File | Where-Object {$_.Extension -in @('.exe','.dll','.pyd','.node')}).Count){throw "Unexpected runtime image under development folder: $development"}
    Assert-Owned $development
    Remove-Item -LiteralPath $development -Recurse -Force
  }
}
$windeploy=Join-Path $QtDir 'bin\windeployqt.exe'
$qtpaths=Join-Path $QtDir 'bin\qtpaths.exe'
$executables=@('sicnu_geo_rs.exe','sicnu_geo_rs_cli.exe','sicnu_worker.exe') | ForEach-Object {Join-Path $bin $_}
# App-local release CRT DLLs are copied explicitly below; omit Qt's installer.
& $windeploy --release --no-compiler-runtime --include-soft-plugins --translations zh_CN --qtpaths $qtpaths --dir $bin --plugindir $qtPlugins @executables
Check-Exit 'Qt runtime deployment'
Write-Utf8 (Join-Path $bin 'qt.conf') "[Paths]`nPrefix=.`nPlugins=plugins`nTranslations=translations`n"

# These are discovered through Qt plugin metadata, not PE imports. Keep the
# essential GUI/MCP, SQLite-auth, image/icon and Windows TLS backends explicit.
$requiredQtPlugins=@(
  'platforms/qwindows.dll','platforms/qoffscreen.dll','sqldrivers/qsqlite.dll',
  'imageformats/qgif.dll','imageformats/qico.dll','imageformats/qjpeg.dll','imageformats/qsvg.dll',
  'iconengines/qsvgicon.dll','tls/qschannelbackend.dll','tls/qcertonlybackend.dll',
  'networkinformation/qnetworklistmanager.dll','styles/qmodernwindowsstyle.dll'
)
foreach($relative in $requiredQtPlugins){
  $source=Join-Path (Join-Path $QtDir 'plugins') $relative
  if(-not(Test-Path -LiteralPath $source -PathType Leaf)){throw "Required release Qt plugin is absent: $source"}
  $destination=Join-Path $qtPlugins $relative
  [IO.Directory]::CreateDirectory((Split-Path -Parent $destination)) | Out-Null
  Copy-Item -LiteralPath $source -Destination $destination -Force
}
# CMake installs the reference XML for AppPaths' fallback. QGIS itself expects
# its default-style XML directly below pkgDataPath()/resources, including when
# it upgrades an existing user's style database.
$qgisXml=Join-Path $PackageRoot 'share\sicnu_geo_rs\qgis_ref\resources\symbology-style.xml'
if(-not(Test-Path -LiteralPath $qgisXml -PathType Leaf)){throw 'Installed QGIS default symbology XML is absent.'}
$qgisResources=Join-Path $PackageRoot 'share\resources'
[IO.Directory]::CreateDirectory($qgisResources) | Out-Null
Copy-Item -LiteralPath $qgisXml -Destination $qgisResources -Force
foreach($relative in @(
  'share/sicnu_geo_rs/resources/fonts/IBMPlexSans.ttf',
  'share/sicnu_geo_rs/resources/fonts/IBMPlexMono-Regular.ttf',
  'share/sicnu_geo_rs/resources/styles.qss','share/sicnu_geo_rs/resources/styles-dark.qss',
  # R31-T03 WP18: both locale manuals must ship; HelpViewerDialog resolves
  # docs/USER_GUIDE*.md from this install prefix. Fail staging if either is
  # absent instead of silently serving the Chinese manual to an English UI.
  'share/sicnu_geo_rs/docs/USER_GUIDE.md',
  'share/sicnu_geo_rs/docs/USER_GUIDE.en.md',
  'plugins/layer_tree_plugin.dll','plugins/processing_plugin.dll'
)){
  if(-not(Test-Path -LiteralPath (Join-Path $PackageRoot $relative) -PathType Leaf)){throw "Required application resource/plugin is absent: $relative"}
}

$vcpkg=(Cache-Value 'VCPKG_INSTALLED_DIR')
if(-not $vcpkg){$vcpkg=Join-Path $BuildDir 'vcpkg_installed'}
$triplet=(Cache-Value 'VCPKG_TARGET_TRIPLET')
if(-not $triplet){$triplet='x64-windows'}
$vcpkg=Join-Path $vcpkg $triplet
foreach($name in @('proj','gdal')){
  $source=Join-Path $vcpkg ('share\'+$name)
  if(-not(Test-Path -LiteralPath $source -PathType Container)){throw "Missing $name runtime data: $source"}
  Copy-Tree $source (Join-Path $PackageRoot ('share\'+$name))
}
if(-not(Test-Path -LiteralPath (Join-Path $PackageRoot 'share\proj\proj.db'))){throw 'proj.db was not deployed.'}
# The current vcpkg GDAL build has built-in GTiff/COG drivers and only a
# drivers.ini here. Preserve any dynamically built drivers and explicitly
# direct GDAL to this package instead of a compiled-in development directory.
$gdalPlugins=Join-Path $bin 'gdalplugins'
[IO.Directory]::CreateDirectory($gdalPlugins) | Out-Null
Copy-Tree (Join-Path $vcpkg 'lib\gdalplugins') $gdalPlugins

# QCA providers are dynamic; deploy available installed crypto plugins beside
# the Qt plugins. A build without a provider is recorded rather than fabricated.
$crypto=Join-Path $qtPlugins 'crypto'
$qcaProviders=@(Get-ChildItem -LiteralPath $QcaDir -Recurse -File -Filter 'qca-*.dll' | Where-Object {$_.Name -ne 'qca-qt6.dll' -and $_.Name -notmatch 'd\.dll$'})
if($qcaProviders.Count){[IO.Directory]::CreateDirectory($crypto)|Out-Null; $qcaProviders | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination $crypto -Force}}

$compiler=Cache-Value 'CMAKE_CXX_COMPILER'
$dumpbin=Join-Path (Split-Path -Parent $compiler) 'dumpbin.exe'
if(-not(Test-Path -LiteralPath $dumpbin)){throw "dumpbin is required to verify the complete PE closure: $dumpbin"}
$vsMatch=[Regex]::Match($compiler,'^(.*?)[\\/]VC[\\/]Tools[\\/]MSVC[\\/]')
if(-not $vsMatch.Success){throw "Cannot locate Visual Studio redistributables from compiler: $compiler"}
$vsRoot=$vsMatch.Groups[1].Value
$redistHome=Join-Path $vsRoot 'VC\Redist\MSVC'
$crtRoots=@()
if(Test-Path -LiteralPath $redistHome){$crtRoots=@(Get-ChildItem -LiteralPath $redistHome -Directory | Sort-Object Name -Descending | ForEach-Object {Join-Path $_.FullName 'x64\Microsoft.VC143.CRT'} | Where-Object {Test-Path -LiteralPath $_})}
if(-not $crtRoots.Count){throw "Release VC++ redistributable directory is unavailable under $redistHome"}
foreach($crt in @(Get-ChildItem -LiteralPath $crtRoots[0] -File -Filter '*.dll')){Copy-Item -LiteralPath $crt.FullName -Destination $bin -Force}
$providers=@($BuildDir,(Join-Path $vcpkg 'bin'),(Join-Path $QtDir 'bin'),(Join-Path $QcaDir 'bin'),(Join-Path $KeychainDir 'bin'))+$crtRoots
$systemDir=[Environment]::SystemDirectory
$osNames=@('advapi32.dll','avicap32.dll','avifil32.dll','bcrypt.dll','bcryptprimitives.dll','combase.dll','comctl32.dll','comdlg32.dll','crypt32.dll','cryptbase.dll','cryptnet.dll','cryptui.dll','d2d1.dll','d3d9.dll','d3d11.dll','d3d12.dll','d3dcompiler_47.dll','dcomp.dll','ddraw.dll','dbgcore.dll','dbghelp.dll','dhcpcsvc.dll','dnsapi.dll','dsound.dll','dwmapi.dll','dxgi.dll','dxguid.dll','dwrite.dll','gdi32.dll','gdiplus.dll','glu32.dll','imm32.dll','iphlpapi.dll','kernel32.dll','kernelbase.dll','mf.dll','mfplat.dll','mfreadwrite.dll','mfuuid.dll','mmdevapi.dll','mpr.dll','msacm32.dll','msasn1.dll','msimg32.dll','msvcp_win.dll','msvcrt.dll','ncrypt.dll','netapi32.dll','netutils.dll','normaliz.dll','ntdll.dll','ole32.dll','oleaut32.dll','opengl32.dll','pdh.dll','propsys.dll','psapi.dll','quartz.dll','rpcrt4.dll','scarddlg.dll','secur32.dll','setupapi.dll','shcore.dll','shell32.dll','shlwapi.dll','sspicli.dll','user32.dll','userenv.dll','usp10.dll','uxtheme.dll','version.dll','win32u.dll','winhttp.dll','wininet.dll','winmm.dll','winnsi.dll','winspool.drv','wintrust.dll','wlanapi.dll','wldap32.dll','ws2_32.dll','wsock32.dll','wtsapi32.dll','ucrtbase.dll')
# CPython's complete Windows distribution includes _msi.pyd, which imports
# the OS's Windows Installer API (its ProductName is not Windows OS).
$osNames += 'msi.dll'
$systemNames=New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
foreach($name in $osNames){[void]$systemNames.Add($name)}
$known=Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\KnownDLLs'
foreach($property in $known.PSObject.Properties){if($property.Value -is [string] -and $property.Value -match '\.dll$'){[void]$systemNames.Add($property.Value)}}
function Get-Imports([string]$Image) {
  $lines=& $dumpbin /DEPENDENTS /nologo $Image 2>&1
  Check-Exit "PE import inspection: $Image"
  return @($lines | ForEach-Object {if($_ -match '^\s+([A-Za-z0-9_.+-]+\.(?:dll|drv))\s*$'){$Matches[1]}} | Select-Object -Unique)
}
$debugQtPluginNames=New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
Get-ChildItem -LiteralPath (Join-Path $QtDir 'plugins') -Recurse -File -Filter '*.dll' | ForEach-Object {
  # A release TLS plugin ends in "backend.dll"; only the additional trailing
  # d counts as debug, verified by the adjacent release counterpart.
  if($_.Name -cmatch 'd\.dll$'){
    $releaseName=$_.Name.Substring(0,$_.Name.Length-5)+'.dll'
    if(Test-Path -LiteralPath (Join-Path $_.DirectoryName $releaseName) -PathType Leaf){[void]$debugQtPluginNames.Add($_.Name)}
  }
}
function Is-DebugImage([string]$Name) {
  # Case matters: Qt6Quick3D.dll is a release library, Qt6Quick3Dd.dll is debug.
  return ($Name -match '^(msvcp\d+(?:_\d+)?d|vcruntime\d+(?:_\d+)?d|ucrtbased|gdald|libcurl-d|opencv_.*\d+d)\.dll$' -or $Name -cmatch '^Qt6.*d\.dll$' -or $debugQtPluginNames.Contains($Name))
}
$queue=New-Object 'System.Collections.Generic.Queue[string]'
$visited=New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
$edges=New-Object 'System.Collections.Generic.List[object]'
# The SDK retains native resources for several upstream platforms. Only its
# Windows x64 prebuild is loadable here; it must be a real PE AMD64 image.
function Get-PeMachine([string]$Image) {
  $stream=[IO.File]::OpenRead($Image)
  $reader=New-Object IO.BinaryReader($stream)
  try {
    if($stream.Length -lt 64 -or $reader.ReadUInt16() -ne 0x5a4d){return 0}
    $stream.Position=0x3c
    $offset=$reader.ReadUInt32()
    if($offset -gt ($stream.Length-6)){return 0}
    $stream.Position=$offset
    if($reader.ReadUInt32() -ne 0x00004550){return 0}
    return $reader.ReadUInt16()
  } finally {$reader.Dispose()}
}
Get-ChildItem -LiteralPath $PackageRoot -Recurse -Force -File | ForEach-Object {
  if($_.Extension -in @('.exe','.dll','.pyd')){$queue.Enqueue($_.FullName)}
  elseif($_.Extension -eq '.node'){
    $relative=$_.FullName.Substring($PackageRoot.Length+1).Replace('\','/')
    $machine=Get-PeMachine $_.FullName
    if($relative -match '/prebuilds/win32-x64/'){
      if($machine -ne 0x8664){throw "Expected Windows AMD64 native Node module: $relative"}
      $queue.Enqueue($_.FullName)
    } elseif($machine -eq 0x8664){throw "Unclassified Windows x64 Node module: $relative"}
  }
}
while($queue.Count){
  $image=$queue.Dequeue()
  if(-not $visited.Add($image)){continue}
  $leaf=Split-Path -Leaf $image
  if(Is-DebugImage $leaf){throw "Debug image found in release package: $image"}
  $pythonOwned=$image.StartsWith($pythonRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)
  foreach($dep in @(Get-Imports $image)){
    if(Is-DebugImage $dep){throw "Debug CRT/Qt import in release package: $image -> $dep"}
    if(-not $pythonOwned -and $dep -match '^python\d*\.dll$'){throw "Native application unexpectedly imports a host Python DLL: $image -> $dep"}
    $record=[ordered]@{image=$image.Substring($PackageRoot.Length+1).Replace('\','/');dependency=$dep;classification='';resolved=''}
    if($dep -match '^(api-ms-win-|ext-ms-win-)'){$record.classification='windows-api-set';$edges.Add([pscustomobject]$record);continue}
    $osPath=Join-Path $systemDir $dep
    $verifiedWindowsFile=$false
    if(-not $systemNames.Contains($dep) -and (Test-Path -LiteralPath $osPath -PathType Leaf)){
      $version=(Get-Item -LiteralPath $osPath).VersionInfo
      $verifiedWindowsFile=$version.CompanyName -like 'Microsoft*' -and $version.ProductName -like '*Windows*Operating System*'
    }
    if($systemNames.Contains($dep) -or $verifiedWindowsFile){
      if(-not(Test-Path -LiteralPath $osPath)){throw "Required Windows DLL is absent: $dep"}
      $record.classification='windows-system';$record.resolved=$osPath;$edges.Add([pscustomobject]$record);continue
    }
    $localSearch=@((Split-Path -Parent $image),$bin)
    if($pythonOwned){$localSearch+=@($pythonRoot,(Join-Path $pythonRoot 'DLLs'))}
    $found=$null
    foreach($dir in $localSearch){$candidate=Join-Path $dir $dep;if(Test-Path -LiteralPath $candidate -PathType Leaf){$found=(Get-Item -LiteralPath $candidate).FullName;break}}
    if(-not $found){
      $source=$null
      foreach($dir in $providers){$candidate=Join-Path $dir $dep;if(Test-Path -LiteralPath $candidate -PathType Leaf){$source=$candidate;break}}
      if(-not $source){throw "Unresolved non-system dependency: $image -> $dep"}
      if($dep -match '^python\d*\.dll$'){throw "Refusing system-Python fallback: $source"}
      $found=Join-Path $bin $dep
      Copy-Item -LiteralPath $source -Destination $found -Force
      $record.source=$source
      $queue.Enqueue($found)
    }
    $record.classification='packaged';$record.resolved=$found.Substring($PackageRoot.Length+1).Replace('\','/');$edges.Add([pscustomobject]$record)
  }
}
Write-Utf8 (Join-Path $PackageRoot 'pe-dependencies.json') (([ordered]@{schema='sicnu.windows-pe-closure/1';images=$visited.Count;dependencies=$edges;unresolved=0}) | ConvertTo-Json -Depth 7)

$licenses=Join-Path $PackageRoot 'licenses'
[IO.Directory]::CreateDirectory($licenses)|Out-Null
foreach($name in @('COPYING','LICENSE','LICENSE.md','LICENSE.txt','README.md')){if(Test-Path -LiteralPath (Join-Path $RepoRoot $name)){Copy-Item -LiteralPath (Join-Path $RepoRoot $name) -Destination $licenses -Force}}
$sourceNotices=& git -C $RepoRoot -c core.quotepath=false ls-files -- external resources images
Check-Exit 'Source license inventory'
foreach($relative in $sourceNotices){
  if($relative -match '(^|/)[^/]*(LICENSE|LICENCE|COPYING|OFL)[^/]*$'){
    $destination=Join-Path (Join-Path $licenses 'source') $relative
    [IO.Directory]::CreateDirectory((Split-Path -Parent $destination))|Out-Null
    Copy-Item -LiteralPath (Join-Path $RepoRoot $relative) -Destination $destination -Force
  }
}
$portShare=Join-Path $vcpkg 'share'
Get-ChildItem -LiteralPath $portShare -Directory | ForEach-Object {
  $port=$_
  foreach($name in @('copyright','vcpkg.spdx.json')){
    $source=Join-Path $port.FullName $name
    if(Test-Path -LiteralPath $source){$destination=Join-Path $licenses ('vcpkg\'+$port.Name);[IO.Directory]::CreateDirectory($destination)|Out-Null;Copy-Item -LiteralPath $source -Destination $destination -Force}
  }
}
Copy-Tree (Join-Path $QtDir 'sbom') (Join-Path $licenses 'qt-sbom')
foreach($candidate in @((Join-Path $QtDir 'Licenses'),(Join-Path $QtDir 'licenses'),(Join-Path $QtDir '..\..\Licenses'))){if(Test-Path -LiteralPath $candidate){Copy-Tree $candidate (Join-Path $licenses 'qt');break}}
foreach($entry in @(@{root=$QcaDir;name='qca'},@{root=$KeychainDir;name='qtkeychain'})){
  foreach($candidate in @((Join-Path $entry.root 'share'),(Join-Path $entry.root 'doc'))){
    if(Test-Path -LiteralPath $candidate){Get-ChildItem -LiteralPath $candidate -Recurse -File | Where-Object {$_.Name -match 'license|copying|copyright'} | ForEach-Object {$dest=Join-Path $licenses $entry.name;[IO.Directory]::CreateDirectory($dest)|Out-Null;Copy-Item -LiteralPath $_.FullName -Destination $dest -Force}}
  }
}
$launcher=@'
@echo off
setlocal
set "RS_ROOT=%~dp0"
set "PATH=%RS_ROOT%bin;%SystemRoot%\System32;%SystemRoot%"
set "QT_PLUGIN_PATH=%RS_ROOT%bin\plugins"
set "QCA_PLUGIN_PATH=%RS_ROOT%bin\plugins"
set "PROJ_DATA=%RS_ROOT%share\proj"
set "GDAL_DATA=%RS_ROOT%share\gdal"
set "GDAL_DRIVER_PATH=%RS_ROOT%bin\gdalplugins"
set "SICNU_DATA_DIR=%RS_ROOT%share\sicnu_geo_rs"
set "PYTHONHOME="
set "PYTHONPATH="
set "PYTHONUSERBASE="
set "SICNU_PYTHON_EXECUTABLE="
set "SICNU_PYTHON_EXECUTION=trusted_local"
"%RS_ROOT%bin\sicnu_geo_rs.exe" %*
exit /b %errorlevel%
'@
Write-Utf8 (Join-Path $PackageRoot 'RUN_RS_STUDIO.cmd') $launcher
$head=(& git -C $RepoRoot rev-parse HEAD).Trim(); Check-Exit 'Source commit identity'
$dirty=[bool](& git -C $RepoRoot status --porcelain --untracked-files=no)
$metadata=[ordered]@{schema='sicnu.windows-application/1';source_commit=$head;build_type='Release';architecture='x64';python='3.12.16';source_tracked_dirty=$dirty;created_utc=[DateTime]::UtcNow.ToString('o');qca_crypto_plugins=@($qcaProviders.Name);required_qt_plugins=$requiredQtPlugins;pe_image_count=$visited.Count;non_system_imports_verified=$true}
$metadata.pi_bundled=$piEnabled
if($piEnabled){
  $piReceipt=Get-Content -LiteralPath (Join-Path $PackageRoot 'share/sicnu-geo-rs/pi-runtime.json') -Raw | ConvertFrom-Json
  $metadata.node=$piReceipt.node_version
  $metadata.pi=$piReceipt.pi_version
}
Write-Utf8 (Join-Path $PackageRoot 'build-info.json') ($metadata|ConvertTo-Json -Depth 5)
$readme=@"
RS Studio Windows x64 (Release)
Source commit: $head

Run RUN_RS_STUDIO.cmd. No system Python is required. The application discovers
CPython 3.12.16 relative to this package and prepares geospatial-v1 from the
included offline wheels. Python agent execution is enabled as trusted_local:
scripts run with the current user's filesystem/network permissions.

To refuse remote geospatial inputs, set SICNU_OFFLINE=1 before launch.
For an offline validation session, also set PROJ_NETWORK=OFF.
The full packaged application also exposes its production tools over --mcp.

Package metadata: build-info.json, pe-dependencies.json and manifest.json.
The Python runtime's own pinned-origin/hash receipt remains unchanged under
share/sicnu-geo-rs/python-runtime.json. Third-party notices are under licenses/
and within the complete Python runtime and wheel archives.
"@
if($piEnabled){$readme+="`nPi and Node are bundled. No system Node/npm or runtime download is needed.`nPi model inference uses the selected model endpoint and requires its network access.`nSDK/product receipts and dependency notices are under share/sicnu-geo-rs/ and the complete Pi payload.`n"}
Write-Utf8 (Join-Path $PackageRoot 'README-WINDOWS.txt') $readme
& (Join-Path $pythonRoot 'python.exe') -I -B $prepare --verify-stage $PackageRoot
Check-Exit 'Installed Python component integrity'
if($piEnabled){
  & (Join-Path $pythonRoot 'python.exe') -I -B $piPrepare --verify-install $PackageRoot
  Check-Exit 'Complete Pi integrity after native deployment'
}
Assert-NoReparseTree $PackageRoot
$files=Get-ChildItem -LiteralPath $PackageRoot -Recurse -Force -File | Sort-Object FullName | ForEach-Object {[ordered]@{path=$_.FullName.Substring($PackageRoot.Length+1).Replace('\','/');bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}}
Write-Utf8 (Join-Path $PackageRoot 'manifest.json') (([ordered]@{schema='sicnu.windows-package-manifest/1';source_commit=$head;files=@($files)})|ConvertTo-Json -Depth 5)
$archive=$PackageRoot+'.zip'
if(Test-Path -LiteralPath $archive){throw "Archive already exists: $archive"}
& (Join-Path $pythonRoot 'python.exe') -I -B (Join-Path $RepoRoot 'scripts/archive_windows_application.py') --package-root $PackageRoot --archive $archive
Check-Exit 'Complete application archive'
$archiveHash=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Utf8 ($archive+'.sha256') ($archiveHash+'  '+(Split-Path -Leaf $archive)+"`n")
Write-Output "Application package: $archive"
Write-Output "Archive SHA256: $archiveHash"
Write-Output "PE images checked: $($visited.Count); non-system unresolved imports: 0"
