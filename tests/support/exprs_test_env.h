// tests/support/exprs_test_env.h — shared plugin-test environment helpers
// (R5 track 03, issues #1362/#1364). Header-only: quoted includes resolve
// relative to tests/, so test targets need no extra CMake wiring.
#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>

#ifndef _WIN32
#include <unistd.h> // getpid
#else
#include <process.h> // _getpid
#endif

namespace exprs_test {

/// Cross-platform pid, resolved locally so NO sdk dependency (header or
/// link) is imposed on consumers: the isolation helpers here are included by
/// parity/shell targets whose include path and link set carry no sicnu_sdk.
inline long localPid()
{
#ifdef _WIN32
    return static_cast<long>( ::_getpid() );
#else
    return static_cast<long>( ::getpid() );
#endif
}

/// Process-unique scratch root: ctest's PRE_TEST discovery runs each case as
/// its own process (often two in parallel under ctest -j2); a shared fixed
/// temp name lets one process's remove_all trample another's mid-test — the
/// root cause of the historical test_exprs_plugin_loader flake family.
inline std::string scratchRoot( const char *leaf )
{
    return ( std::filesystem::temp_directory_path()
             / ( std::string( leaf ) + "."
                 + std::to_string( localPid() ) ) )
        .generic_string();
}

/// RAII cleanup for one pid-unique scratch root: stale residue is removed at
/// construction, the whole tree at destruction. Catch2 aborts a failing case
/// by unwinding, so the destructor runs on every outcome, and
/// remove_all(std::error_code) never throws — the guard cannot swallow or
/// mask the original failure (issue #1362).
struct ScratchGuard
{
    const std::string path;
    explicit ScratchGuard( const char *leaf )
        : path( scratchRoot( leaf ) )
    {
        std::error_code ec;
        std::filesystem::remove_all( path, ec );
    }
    ~ScratchGuard()
    {
        std::error_code ec;
        std::filesystem::remove_all( path, ec );
    }
    ScratchGuard( const ScratchGuard & ) = delete;
    ScratchGuard &operator=( const ScratchGuard & ) = delete;
};

namespace detail {
inline void putEnvUserRoot( const std::string &path )
{
#ifdef _WIN32
    _putenv( ( std::string( "SICNU_PLUGIN_USER_ROOT=" ) + path ).c_str() );
#else
    ::setenv( "SICNU_PLUGIN_USER_ROOT", path.c_str(), 1 );
#endif
}
} // namespace detail

/// Redirects the user plugin root (and the enable/disable index next to it)
/// to a pid-unique scratch. Install ONE object at namespace scope per test
/// binary: without it, every setEnabled() persists the REAL
/// $HOME/sicnu_geo_rs/plugins.index.json — parallel case processes race that
/// shared file, and tests write into the developer's profile.
/// set() re-arms the binary-wide redirect after a test-local override (e.g.
/// an UpgradeFixture nesting its own root) tears its own root down.
struct UserRootRedirect
{
    const std::string scratch;
    const std::string pluginsPath;

    UserRootRedirect()
        : scratch( scratchRoot( "exprs_test_userroot" ) )
        , pluginsPath( scratch + "/plugins" )
    {
        set();
        std::error_code ec;
        std::filesystem::remove_all( scratch, ec );
    }
    ~UserRootRedirect()
    {
        std::error_code ec;
        std::filesystem::remove_all( scratch, ec );
    }
    void set() const { detail::putEnvUserRoot( pluginsPath ); }
    UserRootRedirect( const UserRootRedirect & ) = delete;
    UserRootRedirect &operator=( const UserRootRedirect & ) = delete;
};


} // namespace exprs_test
