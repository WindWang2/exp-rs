// src/verify_adapters/fs_artifact_probe.cpp
#include "verify_adapters/fs_artifact_probe.h"

#include "bounded_io.h"
#include "verify/verify_sha256.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace sicnu::verify_adapters
{
namespace
{

using sicnu::verify::ArtifactInfo;

std::string lowercaseExtension( const std::string &path )
{
    const std::size_t dot = path.find_last_of( '.' );
    if ( dot == std::string::npos )
        return {};
    std::string ext = path.substr( dot + 1 );
    std::transform( ext.begin(), ext.end(), ext.begin(),
                    []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
    return ext;
}

/// Sidecar naming conventions actually written by this platform: the model
/// publish family (<path>.prov.json — exp-rs-prov/1) and the harness
/// evidence family (.provenance.json / .uncertainty.json /
/// .verification.json). Checked before the extension table so
/// "model.tif.prov.json" is a sidecar, not a raster.
bool hasSidecarSuffix( const std::string &path )
{
    for ( const char *suffix : { ".prov.json", ".provenance.json", ".uncertainty.json",
                                 ".verification.json" } )
    {
        const auto len = std::char_traits<char>::length( suffix );
        if ( path.size() >= len && path.compare( path.size() - len, len, suffix ) == 0 )
            return true;
    }
    return false;
}

/// Shallow kind sniff: name-based only, empty when unknown. Deep structure
/// is what artifact.grid / artifact.schema checks are for.
std::string sniffKind( const std::string &path )
{
    if ( hasSidecarSuffix( path ) )
        return "sidecar";
    const std::string ext = lowercaseExtension( path );
    if ( ext == "json" )
        return "json";
    for ( const char *candidate : { "tif", "tiff", "vrt", "img" } )
        if ( ext == candidate )
            return "raster";
    for ( const char *candidate : { "shp", "gpkg", "geojson", "kml" } )
        if ( ext == candidate )
            return "vector";
    for ( const char *candidate : { "csv", "dbf" } )
        if ( ext == candidate )
            return "table";
    return {};
}

} // namespace

FsArtifactProbe::FsArtifactProbe( std::uint64_t digestBudgetBytes )
    : mDigestBudgetBytes( digestBudgetBytes )
{
}

std::optional<ArtifactInfo> FsArtifactProbe::probe( const std::string &path )
{
    if ( path.empty() )
        return std::nullopt;
    std::error_code ec;
    const std::filesystem::path fsPath = pathFromUtf8( path );
    if ( !std::filesystem::exists( fsPath, ec ) )
    {
        // A definitive "not there" is an answer (exists=false — the engine
        // fails artifact.exists); only an UNANSWERABLE stat is nullopt.
        if ( ec )
            return std::nullopt;
        ArtifactInfo missing;
        missing.exists = false;
        return missing;
    }

    ArtifactInfo info;
    info.exists = true;
    info.kind = sniffKind( path );
    if ( std::filesystem::is_regular_file( fsPath, ec ) && !ec )
    {
        const std::uintmax_t size = std::filesystem::file_size( fsPath, ec );
        if ( !ec )
            info.sizeBytes = static_cast<std::uint64_t>( size );
        if ( info.sizeBytes <= mDigestBudgetBytes )
        {
            const IoResult read = readFileBounded( path, mDigestBudgetBytes );
            if ( read.failure == IoFailure::None )
                info.digest = sicnu::verify::sha256Hex( read.text );
        }
        // Over budget or unreadable: digest stays "" — unavailable, never a
        // fabricated value (the engine reports verify:i_digest_unavailable).
    }
    return info;
}

std::optional<Json::Value> FsArtifactProbe::readJson( const std::string &path )
{
    const IoResult read = readFileBounded( path, kMaxAdapterDocumentBytes );
    if ( read.failure != IoFailure::None )
        return std::nullopt;
    std::string error;
    return parseJsonBounded( read.text, error );
}

} // namespace sicnu::verify_adapters
