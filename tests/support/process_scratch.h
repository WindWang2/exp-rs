#pragma once

// Per-process scratch directories (R6 test-determinism track, #1392).
//
// ctest runs every Catch2 case in its own process, and tier 1 uses `-j4`, so
// two cases that derive the SAME scratch tree collide: each process opens with
// remove_all() of that tree and deletes its sibling's fixture mid-write.
// #1414 hit this in test_io_canonical_metadata ("Cannot open raster" after a
// sibling's remove_all); the tier-1 `-j4` lane run on top of this PR surfaced
// the same collision in test_io_multidim (three cases share the "cube9"
// scratch name) and test_quality_mosaic_operator (one fixed dir shared by four
// cases).
//
// A directory keyed on the process id is private to the process that created
// it and is removed when that process exits, so the tree cannot outlive a
// crashed run and two parallel processes can never touch the same files. The
// per-case `name` component is kept as well: a single process running several
// cases (a plain `./test_x` invocation, or any `-j1` lane) still gets one
// directory per case, which is what makes a fixed `name` safe to keep using.
//
// Qt-free on purpose so the io/sdk/study targets (no Qt link) can use it.

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include <filesystem>
#include <string>

namespace sicnu_test
{

inline long testProcessId()
{
#ifdef _WIN32
  return static_cast<long>( _getpid() );
#else
  return static_cast<long>( getpid() );
#endif
}

/// Scratch directory `<temp>/<prefix>_<pid>/<name>`, created empty.
///
/// The `<prefix>_<pid>` root is created and cleaned once per process (a
/// function-local static's destructor removes it at exit, so a crash leaves
/// nothing behind); the per-call `<name>` directory is wiped and recreated so
/// a repeated call in one process still starts from a clean scratch.
inline std::string processScratchDir( const std::string &prefix, const std::string &name )
{
  static std::string root;
  if ( root.empty() )
  {
    root = ( std::filesystem::temp_directory_path()
             / ( prefix + "_" + std::to_string( testProcessId() ) ) )
               .string();
    std::error_code ec;
    std::filesystem::remove_all( root, ec ); // a reused pid must not inherit
    std::filesystem::create_directories( root, ec );
    struct Cleanup
    {
      std::string path;
      ~Cleanup()
      {
        std::error_code ignored;
        std::filesystem::remove_all( path, ignored );
      }
    };
    static Cleanup cleanup{ root };
  }

  const std::filesystem::path dir = std::filesystem::path( root ) / name;
  std::error_code ec;
  std::filesystem::remove_all( dir, ec ); // idempotent suites: clean scratch
  std::filesystem::create_directories( dir, ec );
  return dir.string();
}

} // namespace sicnu_test
