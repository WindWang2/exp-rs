// src/verify_adapters/bounded_io.cpp
#include "verify_adapters/bounded_io.h"

#include "verify/verify_locale.h"

#include <fstream>
#include <limits>
#include <system_error>

namespace sicnu::verify_adapters
{

std::filesystem::path pathFromUtf8( const std::string &text )
{
    return std::filesystem::path( std::u8string( reinterpret_cast<const char8_t *>( text.data() ),
                                                 text.size() ) );
}

bool pathExists( const std::string &path )
{
    std::error_code ec;
    return std::filesystem::exists( pathFromUtf8( path ), ec );
}

IoResult readFileBounded( const std::string &path, const std::uint64_t capBytes )
{
    IoResult result;
    std::error_code ec;
    const std::filesystem::path fsPath = pathFromUtf8( path );
    if ( !std::filesystem::exists( fsPath, ec ) )
    {
        result.failure = ec ? IoFailure::StatFailed : IoFailure::Missing;
        result.detail = ec ? "cannot stat path" : "path does not exist";
        return result;
    }
    if ( !std::filesystem::is_regular_file( fsPath, ec ) || ec )
    {
        result.failure = IoFailure::NotRegular;
        result.detail = "not a regular file";
        return result;
    }
    // Stat-then-open is inherently racy; the cap is enforced on what is
    // actually READ (cap+1 sentinel byte), not on the stat result — the
    // stat only decides how big the buffer starts.
    std::ifstream file( fsPath, std::ios::binary );
    if ( !file )
    {
        result.failure = IoFailure::OpenFailed;
        result.detail = "cannot open file";
        return result;
    }
    // Size the buffer from the file, not from the cap: probing a small
    // sidecar must not allocate (and zero-fill) the whole digest budget
    // before reading a few hundred bytes.
    const std::uintmax_t statSize = std::filesystem::file_size( fsPath, ec );
    if ( ec )
    {
        result.failure = IoFailure::StatFailed;
        result.detail = "cannot stat file size";
        return result;
    }
    // A cap past what this platform can address clamps, never truncates to
    // a size_t buffer smaller than the cap the caller believes is enforced
    // (size_t is 32-bit on 32-bit builds); the -1 keeps room for the
    // sentinel byte below.
    constexpr std::uint64_t kAddressableBytes =
        static_cast<std::uint64_t>( std::numeric_limits<std::size_t>::max() ) - 1;
    const std::uint64_t effectiveCap = capBytes < kAddressableBytes ? capBytes : kAddressableBytes;
    const std::uint64_t readBytes =
        ( statSize < effectiveCap ? statSize : effectiveCap ) + 1;
    std::string buffer;
    buffer.resize( static_cast<std::size_t>( readBytes ) );
    file.read( buffer.data(), static_cast<std::streamsize>( readBytes ) );
    if ( file.bad() )
    {
        result.failure = IoFailure::ReadError;
        result.detail = "read error";
        return result;
    }
    buffer.resize( static_cast<std::size_t>( file.gcount() ) );
    if ( buffer.size() > effectiveCap )
    {
        result.failure = IoFailure::OverCap;
        result.detail = "file exceeds the read cap";
        return result;
    }
    result.text = std::move( buffer );
    return result;
}

std::optional<Json::Value> parseJsonBounded( const std::string &text, std::string &error )
{
    Json::CharReaderBuilder builder;
    builder["allowComments"] = false;
    builder["stackLimit"] = 128;
    Json::Value root;
    std::string parseError;
    const std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    // Same locale pin as the verifier's own serde: sidecar and checkpoint
    // numbers must parse identically on every host.
    const sicnu::verify::ClassicNumericLocale pin;
    // jsoncpp signals a stackLimit violation by THROWING Json::Exception out
    // of parse() rather than returning false (same shape as grader_json,
    // capability_mirror and scientific_recipe). Sidecars and checkpoints
    // are untrusted content; the contract here is "Nullopt on any parse
    // failure", so the throw dies as a typed refusal.
    try
    {
        if ( !reader->parse( text.data(), text.data() + text.size(), &root, &parseError ) )
        {
            error = "invalid JSON: " + parseError;
            return std::nullopt;
        }
    }
    catch ( const Json::Exception &exception )
    {
        error = std::string( "invalid JSON: " ) + exception.what();
        return std::nullopt;
    }
    error.clear();
    return root;
}

} // namespace sicnu::verify_adapters
