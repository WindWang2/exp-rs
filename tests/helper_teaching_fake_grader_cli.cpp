// helper_teaching_fake_grader_cli.cpp — test double for the REAL grading CLI
// exit/transcript contract (sicnu_geo_rs_cli lab --grade). Same shape, no Qt,
// no GDAL: {schema:"sicnu.lab.grade/1", digest, generated_utc, report} written
// to --out AND stdout, exit 0 pass / 1 fail / 2 usage / 3 unverifiable.
// Behavior keyed on artifact bytes: GOOD → pass, BAD → fail, UNVERIFIABLE →
// exit 3; lab id "usage_lab" (or missing artifact) → exit 2.
//
// DETERMINISM ORACLE: this helper must stay byte-aligned with the transcript
// writer in src/agent/output_verifier.cpp (LabGradeResult::toJson). If the
// real contract changes, update BOTH — test_teaching_admin_core fails on
// schema/digest drift via grader_transcript_invalid / exit-verdict
// cross-checks, which is the signal to re-sync.
#include <csignal>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <process.h>
#endif

namespace {

// 64 hex chars, fixed: tests assert shape, not value.
constexpr const char *kFakeDigest = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

void delayForCancellationFixture()
{
    const char *text = std::getenv( "SICNU_TEST_GRADER_DELAY_MS" );
    if ( !text )
        return;
    char *end = nullptr;
    const long milliseconds = std::strtol( text, &end, 10 );
    if ( end != text && *end == '\0' && milliseconds > 0 && milliseconds <= 2000 )
        std::this_thread::sleep_for( std::chrono::milliseconds( milliseconds ) );
}

std::string transcriptBody( const std::string &lab, const std::string &artifact,
                            const std::string &verdict, double score, const std::string &error,
                            const std::string &deductionJson )
{
    std::string body;
    body += "{";
    body += "\"lab_id\":\"" + lab + "\",";
    body += "\"artifact\":\"" + artifact + "\",";
    body += "\"rules\":\"<fake>\",";
    body += "\"verdict\":\"" + verdict + "\",";
    body += "\"score\":" + std::to_string( score ) + ",";
    body += "\"passing_score\":60.0,";
    body += "\"capped_by_blocking\":false,";
    body += "\"deductions\":" + ( deductionJson.empty() ? std::string( "[]" ) : deductionJson ) + ",";
    body += "\"evidence\":[],";
    body += "\"summary\":{}";
    if ( !error.empty() )
        body += ",\"error\":\"" + error + "\"";
    body += "}";
    return body;
}

void emit( const std::string &outPath, const std::string &document )
{
    std::cout << document << "\n";
    std::cout.flush();
    if ( !outPath.empty() )
    {
        std::ofstream out( outPath.c_str(), std::ios::binary | std::ios::trunc );
        out << document;
    }
}

std::string slurp( const std::string &path )
{
    std::ifstream in( path.c_str(), std::ios::binary );
    if ( !in )
        return std::string();
    return std::string( ( std::istreambuf_iterator<char>( in ) ), std::istreambuf_iterator<char>() );
}

std::string jsonEscape( const std::string &s )
{
    std::string out;
    for ( const char c : s )
    {
        if ( c == '"' || c == '\\' )
        {
            out += '\\';
            out += c;
        }
        else if ( static_cast<unsigned char>( c ) < 0x20 )
        {
            char buf[8];
            std::snprintf( buf, sizeof( buf ), "\\u%04x", static_cast<unsigned>( c ) );
            out += buf;
        }
        else
        {
            out += c;
        }
    }
    return out;
}

} // namespace

int graderMain( int argc, char **argv )
{
    std::string lab, artifact, outPath;
    for ( int i = 1; i < argc; ++i )
    {
        const std::string arg = argv[i];
        if ( arg == "--lab" && i + 1 < argc )
            lab = argv[++i];
        else if ( arg == "--grade" && i + 1 < argc )
            artifact = argv[++i];
        else if ( arg == "--out" && i + 1 < argc )
            outPath = argv[++i];
    }

    if ( lab.empty() || artifact.empty() )
    {
        std::cerr << "lab: --lab and --grade are required\n";
        return 2;
    }

    delayForCancellationFixture();

    // usage: unknown lab id
    if ( lab == "usage_lab" )
    {
        std::cerr << "lab: unknown lab: usage_lab\n";
        const std::string document =
          std::string( "{\"schema\":\"sicnu.lab.grade/1\",\"digest\":\"" ) + kFakeDigest
          + "\",\"generated_utc\":\"2026-01-01T00:00:00Z\",\"report\":"
          + transcriptBody( lab, artifact, "unverifiable", 0.0, "unknown lab: usage_lab", "" ) + "}";
        emit( outPath, document );
        return 2;
    }

    // broken authority: exit says pass, transcript says fail — the adapter
    // must refuse this (grader_exit_verdict_mismatch), never trust either.
    if ( lab == "mismatch_lab" )
    {
        const std::string document =
          std::string( "{\"schema\":\"sicnu.lab.grade/1\",\"digest\":\"" ) + kFakeDigest
          + "\",\"generated_utc\":\"2026-01-01T00:00:00Z\",\"report\":"
          + transcriptBody( lab, artifact, "fail", 10.0, "", "" ) + "}";
        emit( outPath, document );
        return 0;
    }

    const std::string bytes = slurp( artifact );
    if ( bytes.empty() )
    {
        // missing/unreadable artifact → usage
        std::cerr << "lab: artifact missing or unreadable: " << artifact << "\n";
        const std::string document =
          std::string( "{\"schema\":\"sicnu.lab.grade/1\",\"digest\":\"" ) + kFakeDigest
          + "\",\"generated_utc\":\"2026-01-01T00:00:00Z\",\"report\":"
          + transcriptBody( lab, artifact, "unverifiable", 0.0,
                            "artifact missing or unreadable: " + jsonEscape( artifact ), "" )
          + "}";
        emit( outPath, document );
        return 2;
    }

    if ( bytes.find( "UNVERIFIABLE" ) != std::string::npos )
    {
        const std::string document =
          std::string( "{\"schema\":\"sicnu.lab.grade/1\",\"digest\":\"" ) + kFakeDigest
          + "\",\"generated_utc\":\"2026-01-01T00:00:00Z\",\"report\":"
          + transcriptBody( lab, artifact, "unverifiable", 0.0, "cannot open as raster", "" ) + "}";
        emit( outPath, document );
        return 3;
    }

    if ( bytes.find( "CRASH" ) != std::string::npos )
    {
        // Regression lane for the crash contract: emit a *complete valid*
        // transcript, then die abnormally. A runScript that ignores
        // exitStatus would read this as a normal exit — the adapter must
        // answer unavailable/grader_crashed instead.
        const std::string document =
          std::string( "{\"schema\":\"sicnu.lab.grade/1\",\"digest\":\"" ) + kFakeDigest
          + "\",\"generated_utc\":\"2026-01-01T00:00:00Z\",\"report\":"
          + transcriptBody( lab, artifact, "pass", 99.0, "", "" ) + "}";
        emit( outPath, document );
        std:: fflush( stdout );
#ifdef _WIN32
        ::SetErrorMode( SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX );
        ::RaiseException( EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr );
#else
        std::raise( SIGSEGV );
#endif
        return 99; // unreachable
    }

    if ( bytes.find( "BAD" ) != std::string::npos )
    {
        // Two deductions: the top one must be chosen by MAX WEIGHT (a2, 60),
        // not by array order (a1 first) — locks the lab_batch_runner parity.
        const std::string deduction =
          std::string( "[{\"assertion_id\":\"a1\",\"kind\":\"range\",\"severity\":\"normal\","
                       "\"weight\":10.0,\"observed\":\"40\",\"expected\":\">= 60\",\"delta\":null,"
                       "\"message\":\"low weight fail\"},"
                       "{\"assertion_id\":\"a2\",\"kind\":\"mean_sigma\",\"severity\":\"normal\","
                       "\"weight\":60.0,\"observed\":\"40\",\"expected\":\">= 60\",\"delta\":null,"
                       "\"message\":\"below pass line\"}]" );
        const std::string document =
          std::string( "{\"schema\":\"sicnu.lab.grade/1\",\"digest\":\"" ) + kFakeDigest
          + "\",\"generated_utc\":\"2026-01-01T00:00:00Z\",\"report\":"
          + transcriptBody( lab, artifact, "fail", 40.0, "", deduction ) + "}";
        emit( outPath, document );
        return 1;
    }

    // default (incl. GOOD) → pass
    const std::string document =
      std::string( "{\"schema\":\"sicnu.lab.grade/1\",\"digest\":\"" ) + kFakeDigest
      + "\",\"generated_utc\":\"2026-01-01T00:00:00Z\",\"report\":"
      + transcriptBody( lab, artifact, "pass", 88.5, "", "" ) + "}";
    emit( outPath, document );
    return 0;
}

#ifdef _WIN32
namespace {
// _wspawnv joins argv into a command line without quoting. Match the MSVCRT
// escaping rules also used by exprs::ExternalProcess, keeping this helper
// independent of production libraries.
std::wstring quoteCrtArgument( const std::wstring &argument )
{
    std::wstring quoted( 1, L'"' );
    std::size_t backslashes = 0;
    for ( const wchar_t character : argument )
    {
        if ( character == L'\\' )
        {
            ++backslashes;
            continue;
        }
        if ( character == L'"' )
            quoted.append( backslashes * 2 + 1, L'\\' );
        else
            quoted.append( backslashes, L'\\' );
        quoted.push_back( character );
        backslashes = 0;
    }
    quoted.append( backslashes * 2, L'\\' );
    quoted.push_back( L'"' );
    return quoted;
}

std::string utf8Argument( const wchar_t *argument )
{
    const std::wstring wide( argument );
    const int size = ::WideCharToMultiByte( CP_UTF8, 0, wide.data(), static_cast<int>( wide.size() ),
                                           nullptr, 0, nullptr, nullptr );
    std::string text( static_cast<std::size_t>( size ), '\0' );
    ::WideCharToMultiByte( CP_UTF8, 0, wide.data(), static_cast<int>( wide.size() ),
                           text.data(), size, nullptr, nullptr );
    return text;
}
} // namespace

int wmain( int argc, wchar_t **argv )
{
    // Test-only interpreter proxy. Forward an argv array directly to the
    // harness host, with no shell or dependency on file associations.
    const wchar_t *host = _wgetenv( L"SICNU_TEST_FORWARD_PYTHON" );
    if ( host && *host )
    {
        const wchar_t *markerPath = _wgetenv( L"SICNU_TEST_PYTHON_MARKER" );
        if ( !markerPath || !*markerPath )
            return 2;
        if ( argc > 1 && std::wstring( argv[1] ) != L"-c" )
        {
            std::ofstream marker( std::filesystem::path( markerPath ), std::ios::binary | std::ios::app );
            marker << utf8Argument( argv[0] ) << '\n' << utf8Argument( argv[1] ) << '\n';
            marker.close();
            if ( !marker.good() )
            {
                const int error = errno;
                const DWORD nativeError = ::GetLastError();
                std::cerr << "[teaching-pin-proxy] marker write failed; errno=" << error
                          << "; win32_error=" << nativeError << '\n';
                return 2;
            }
        }
        std::vector<std::wstring> quoted;
        quoted.reserve( static_cast<std::size_t>( argc ) );
        quoted.push_back( quoteCrtArgument( host ) );
        for ( int i = 1; i < argc; ++i )
            quoted.push_back( quoteCrtArgument( argv[i] ) );
        std::vector<const wchar_t *> forwarded;
        for ( const std::wstring &argument : quoted )
            forwarded.push_back( argument.c_str() );
        forwarded.push_back( nullptr );
        const intptr_t code = _wspawnv( _P_WAIT, host, forwarded.data() );
        if ( code < 0 )
            std::cerr << "[teaching-pin-proxy] spawn failed; errno=" << errno << '\n';
        return code < 0 ? 2 : static_cast<int>( code );
    }

    std::vector<std::string> arguments;
    arguments.reserve( static_cast<std::size_t>( argc ) );
    for ( int i = 0; i < argc; ++i )
        arguments.push_back( utf8Argument( argv[i] ) );
    std::vector<char *> pointers;
    for ( std::string &argument : arguments )
        pointers.push_back( argument.data() );
    pointers.push_back( nullptr );
    return graderMain( argc, pointers.data() );
}
#else
int main( int argc, char **argv )
{
    return graderMain( argc, argv );
}
#endif
