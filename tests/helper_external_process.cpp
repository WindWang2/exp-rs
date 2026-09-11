// tests/helper_external_process.cpp — behavior helper for external-process
// parity tests (isolation runtime 5.0). One static-linked binary covering the
// scenarios the POSIX suite exercises with shell fixtures:
//
//   argv A B C        print argv[1..] as a JSON array to stdout
//   env NAME          print the value of environment variable NAME
//   cwd               print the current working directory
//   exit N            exit with code N
//   write-bytes N     write N 'a' bytes to stdout, exit 0
//   sleep-ms N        sleep N milliseconds, exit 0
//   spin              sleep forever (kill/timeout target)
//   spawn-sleep MS    print {"child": "<pid>"} then spawn self `sleep-ms MS`
//                     and wait — the child is the kill-tree probe
//
// Everything is deliberately stdio + std only, so the same binary works on
// the Windows and POSIX lanes.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

void writeAll( const std::string &text )
{
    std::fwrite( text.data(), 1, text.size(), stdout );
    std::fflush( stdout );
}

std::string envValue( const char *name )
{
    const char *value = std::getenv( name );
    return value ? std::string( value ) : std::string();
}

std::string currentDirectory()
{
#ifdef _WIN32
    char buffer[ MAX_PATH ];
    const DWORD got = ::GetCurrentDirectoryA( MAX_PATH, buffer );
    return got > 0 ? std::string( buffer, got ) : std::string();
#else
    char buffer[ 4096 ];
    return ::getcwd( buffer, sizeof( buffer ) ) ? std::string( buffer ) : std::string();
#endif
}

int sleepMs( long long milliseconds )
{
    std::this_thread::sleep_for( std::chrono::milliseconds( milliseconds ) );
    return 0;
}

#ifdef _WIN32
std::string numberAsString( unsigned long long value )
{
    char buffer[ 32 ];
    std::snprintf( buffer, sizeof( buffer ), "%llu", value );
    return buffer;
}
#else
std::string numberAsString( unsigned long long value )
{
    char buffer[ 32 ];
    std::snprintf( buffer, sizeof( buffer ), "%llu", value );
    return buffer;
}
#endif

int spawnSleep( const std::string &milliseconds, const std::string &self )
{
#ifdef _WIN32
    STARTUPINFOA startupInfo;
    ZeroMemory( &startupInfo, sizeof( startupInfo ) );
    startupInfo.cb = sizeof( startupInfo );
    PROCESS_INFORMATION processInfo;
    ZeroMemory( &processInfo, sizeof( processInfo ) );
    const std::string command = "\"" + self + "\" sleep-ms " + milliseconds;
    std::vector<char> mutableCommand( command.begin(), command.end() );
    mutableCommand.push_back( '\0' );
    if ( !::CreateProcessA( nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, 0,
                            nullptr, nullptr, &startupInfo, &processInfo ) )
        return 3;
    writeAll( "{\"child\": \"" + numberAsString( processInfo.dwProcessId ) + "\"}\n" );
    ::WaitForSingleObject( processInfo.hProcess, INFINITE );
    ::CloseHandle( processInfo.hThread );
    ::CloseHandle( processInfo.hProcess );
    return 0;
#else
    const pid_t pid = ::fork();
    if ( pid == 0 )
    {
        std::string ms = milliseconds;
        execl( self.c_str(), self.c_str(), "sleep-ms", ms.c_str(),
               static_cast<char *>( nullptr ) );
        _exit( 3 );
    }
    if ( pid < 0 )
        return 3;
    writeAll( "{\"child\": \"" + numberAsString( static_cast<unsigned long long>( pid ) )
              + "\"}\n" );
    int status = 0;
    ::waitpid( pid, &status, 0 );
    return 0;
#endif
}

} // namespace

int main( int argc, char **argv )
{
    if ( argc < 2 )
        return 2;
    const std::string command = argv[ 1 ];

    if ( command == "argv" )
    {
        std::string json = "[";
        for ( int index = 2; index < argc; ++index )
        {
            if ( index > 2 )
                json += ", ";
            std::string escaped;
            for ( const char c : std::string( argv[ index ] ) )
            {
                if ( c == '"' || c == '\\' )
                    escaped += '\\';
                escaped += c;
            }
            json += "\"" + escaped + "\"";
        }
        json += "]";
        writeAll( json + "\n" );
        return 0;
    }
    if ( command == "env" && argc == 3 )
    {
        writeAll( envValue( argv[ 2 ] ) + "\n" );
        return 0;
    }
    if ( command == "cwd" )
    {
        writeAll( currentDirectory() + "\n" );
        return 0;
    }
    if ( command == "exit" && argc == 3 )
        return std::atoi( argv[ 2 ] );
    if ( command == "write-bytes" && argc == 3 )
    {
        const long long count = std::atoll( argv[ 2 ] );
        std::string chunk( 4096, 'a' );
        long long written = 0;
        while ( written < count )
        {
            const long long take =
                ( count - written < static_cast<long long>( chunk.size() ) )
                    ? ( count - written )
                    : static_cast<long long>( chunk.size() );
            std::fwrite( chunk.data(), 1, static_cast<size_t>( take ), stdout );
            written += take;
        }
        std::fflush( stdout );
        return 0;
    }
    if ( command == "sleep-ms" && argc == 3 )
        return sleepMs( std::atoll( argv[ 2 ] ) );
    if ( command == "spin" )
    {
        for ( ;; )
            std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
    }
    if ( command == "spawn-sleep" && argc == 3 )
        return spawnSleep( argv[ 2 ], argv[ 0 ] );
    return 2;
}
