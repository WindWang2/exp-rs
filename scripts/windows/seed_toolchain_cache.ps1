# seed_toolchain_cache.ps1 — write a CMake initial-cache script (-C) that seeds
# toolchain LOCATION variables from an existing configured tree.
#
# Used by scripts\windows\build.cmd smoke for the clean-tree gate: the fresh
# configure re-discovers every dependency, but the toolchain (generator,
# compilers, Qt/vcpkg/QCA/keychain prefixes, bison/flex) is located from the
# reference build dir. A -C script is used instead of -D command-line arguments
# because the values routinely contain spaces (C:/Program Files/...), which no
# batch quoting scheme survives reliably.
#
# Output: one `set(KEY "VALUE" CACHE STRING "" FORCE)` line per found key.
param(
    [string]$BaseCache,
    [Parameter(Mandatory = $true)][string]$Out,
    [string]$Catch2Source
)

$keys = @(
    'CMAKE_MAKE_PROGRAM'
    'CMAKE_CXX_COMPILER'
    'CMAKE_C_COMPILER'
    'CMAKE_TOOLCHAIN_FILE'
    'CMAKE_PREFIX_PATH'
    'VCPKG_INSTALLED_DIR'
    'VCPKG_TARGET_TRIPLET'
    'Qt6_DIR'
    'Qt6Keychain_DIR'
    'QCA_INCLUDE_DIR'
    'QCA_LIBRARY'
    'BISON_EXECUTABLE'
    'FLEX_EXECUTABLE'
)

$lines = New-Object System.Collections.Generic.List[string]
foreach ($k in $keys) {
    if (-not $BaseCache) { break }
    $m = Select-String -Path $BaseCache -Pattern ('^' + $k + ':[A-Z]*=(.*)$') | Select-Object -Last 1
    if ($m) {
        $v = $m.Matches[0].Groups[1].Value
        if ($v -and $v -notlike '*NOTFOUND*') {
            $lines.Add(('set(' + $k + ' "' + $v + '" CACHE STRING "" FORCE)'))
        }
    }
}

if (-not $BaseCache -and -not $Catch2Source) {
    Write-Error "seed_toolchain_cache: nothing to do (no BaseCache and no Catch2Source)"
    exit 1
}
if ($Catch2Source) {
    # Same reasoning as the toolchain keys: a path with spaces must not travel
    # the command line, so the offline Catch2 source also goes through this
    # initial-cache script.
    $lines.Add(('set(FETCHCONTENT_SOURCE_DIR_CATCH2 "' + $Catch2Source + '" CACHE STRING "" FORCE)'))
}
if ($lines.Count -eq 0) {
    Write-Error "no toolchain keys found in $BaseCache"
    exit 1
}
Set-Content -Path $Out -Value $lines
Write-Host ("seeded {0} toolchain keys -> {1}" -f $lines.Count, $Out)
