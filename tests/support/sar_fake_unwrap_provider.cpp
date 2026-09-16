// tests/support/sar_fake_unwrap_provider.cpp — a controllable stand-in for
// an external unwrap provider (Advanced InSAR 11.0, package D tests).
//
// Contract mirror of sar_unwrap_provider.cpp's process interface:
//   argv: <input.f32> <output.f32> <width> [height]
//   stdin/stdout: unused; exit code + output file carry the semantics.
// The behavior mode comes from FAKE_UNWRAP_MODE (env):
//   ok        — echo the input plane (identity "unwrap")
//   crash     — exit(3) after reading the input
//   truncated — write only half the plane
//   nan       — echo but overwrite sample 0 with NaN
//   hang      — sleep 30 s (timeout/cancel tests)
// Raw Float32 planes only — no GDAL, no Qt, deterministic.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
static void fakeSleepSeconds( int s ) { Sleep( s * 1000 ); }
#else
#include <unistd.h>
static void fakeSleepSeconds( int s ) { sleep( s ); }
#endif

int main( int argc, char **argv )
{
    if ( argc < 4 )
        return 2;
    const char *inputPath = argv[1];
    const char *outputPath = argv[2];
    const int width = std::atoi( argv[3] );

    FILE *in = std::fopen( inputPath, "rb" );
    if ( !in )
        return 4;
    std::fseek( in, 0, SEEK_END );
    const long bytes = std::ftell( in );
    std::fseek( in, 0, SEEK_SET );
    const long count = bytes / static_cast<long>( sizeof( float ) );
    if ( count <= 0 || width <= 0 || count % width != 0 )
    {
        std::fclose( in );
        return 5;
    }
    std::vector<char> buf( static_cast<size_t>( bytes ) );
    if ( std::fread( buf.data(), 1, buf.size(), in ) != buf.size() )
    {
        std::fclose( in );
        return 6;
    }
    std::fclose( in );

    const char *modeEnv = std::getenv( "FAKE_UNWRAP_MODE" );
    const std::string m = modeEnv ? modeEnv : "ok";
    if ( m == "hang" )
    {
        fakeSleepSeconds( 30 );
        return 0;
    }
    if ( m == "crash" )
        return 3;

    FILE *out = std::fopen( outputPath, "wb" );
    if ( !out )
        return 7;
    if ( m == "truncated" )
    {
        std::fwrite( buf.data(), 1, buf.size() / 2, out );
        std::fclose( out );
        return 0;
    }
    if ( m == "nan" )
    {
        float nan = std::numeric_limits<float>::quiet_NaN();
        std::memcpy( buf.data(), &nan, sizeof( float ) );
    }
    std::fwrite( buf.data(), 1, buf.size(), out );
    std::fclose( out );
    return 0;
}
