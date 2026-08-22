#define CATCH_CONFIG_RUNNER
#include <catch2/catch_session.hpp>
#include <cstdlib>
#include <vector>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

int main( int argc, char *argv[] )
{
#ifdef _WIN32
  // Windows: Convert UTF-16 command-line to UTF-8 so Catch2 test-name filters
  // containing Unicode symbols (e.g. °, →, —, ×) match UTF-8 TEST_CASE names.
  int wArgc = 0;
  LPWSTR *wArgv = CommandLineToArgvW( GetCommandLineW(), &wArgc );
  std::vector<std::string> utf8Args;
  std::vector<char *> utf8Argv;
  if ( wArgv && wArgc > 0 )
  {
    utf8Args.reserve( wArgc );
    utf8Argv.reserve( wArgc + 1 );
    for ( int i = 0; i < wArgc; ++i )
    {
      int sizeNeeded = WideCharToMultiByte( CP_UTF8, 0, wArgv[i], -1, nullptr, 0, nullptr, nullptr );
      if ( sizeNeeded > 0 )
      {
        std::string s( sizeNeeded - 1, '\0' );
        WideCharToMultiByte( CP_UTF8, 0, wArgv[i], -1, &s[0], sizeNeeded, nullptr, nullptr );
        utf8Args.push_back( std::move( s ) );
      }
      else
      {
        utf8Args.push_back( std::string() );
      }
    }
    for ( auto &s : utf8Args )
      utf8Argv.push_back( &s[0] );
    utf8Argv.push_back( nullptr );
    LocalFree( wArgv );
  }

  const int result = utf8Argv.empty()
    ? Catch::Session().run( argc, argv )
    : Catch::Session().run( static_cast<int>( utf8Argv.size() - 1 ), utf8Argv.data() );

  _exit( result );
#else
  return Catch::Session().run( argc, argv );
#endif
}
