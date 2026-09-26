// src/agent/cartography/export_manifest.cpp
#include "export_manifest.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryFile>

#include <algorithm>
#include <sstream>

#include "geospatial/util/atomic_fs.h"

namespace sicnu::agent::cartography {

namespace {

std::string sha256OfBytes( const QByteArray &bytes )
{
    return std::string( QCryptographicHash::hash( bytes, QCryptographicHash::Sha256 ).toHex().constData() );
}

/// Canonical JSON: jsoncpp's StreamWriter in compact mode. Json::Value
/// object members iterate in sorted-name order, so the serialization of two
/// equal payloads is byte-identical regardless of insertion order.
std::string canonicalJson( const Json::Value &value )
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    builder["commentStyle"] = "None";
    builder["precision"] = 17;
    const std::string serialized = Json::writeString( builder, value );
    return serialized;
}

bool isHexDigest( const std::string &value )
{
    if ( value.size() != 64 )
        return false;
    return std::all_of( value.begin(), value.end(), []( char c ) {
        return ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'f' );
    } );
}

/// Bare-name guard mirroring the export request contract: no separators,
/// no traversal, no dot shenanigans.
bool isBareName( const std::string &name )
{
    if ( name.empty() || name == "." || name == ".." )
        return false;
    if ( name.rfind( "..", 0 ) == 0 )
        return false;
    return name.find( '/' ) == std::string::npos && name.find( '\\' ) == std::string::npos;
}

} // namespace

Json::Value ExportManifest::payloadToJson() const
{
    Json::Value payload( Json::objectValue );
    payload["kind"] = "cartography_export_manifest";
    payload["manifest_version"] = manifest_version;
    payload["layout_name"] = layout_name;
    payload["format"] = format;
    payload["dpi"] = dpi;
    payload["provenance"] = provenance;
    payload["structural_digest"] = structural_digest;
    Json::Value pagesJson( Json::arrayValue );
    for ( const ExportManifestPage &page : pages )
    {
        Json::Value entry( Json::objectValue );
        entry["file_name"] = page.file_name;
        entry["sha256"] = page.sha256;
        entry["bytes"] = static_cast<Json::Int64>( page.bytes );
        if ( !page.feature_id.empty() )
            entry["feature_id"] = page.feature_id;
        if ( !page.label.empty() )
            entry["label"] = page.label;
        if ( !page.extent.isNull() )
            entry["extent"] = page.extent;
        pagesJson.append( entry );
    }
    payload["page_count"] = static_cast<Json::Int64>( pages.size() );
    payload["pages"] = pagesJson;
    return payload;
}

std::string ExportManifest::digest() const
{
    return sha256OfBytes( QByteArray::fromStdString( canonicalJson( payloadToJson() ) ) );
}

Json::Value ExportManifest::toJson() const
{
    Json::Value document = payloadToJson();
    document["manifest_digest"] = digest();
    document["environment"] = environment;
    return document;
}

std::vector<std::string> validateExportManifest( const Json::Value &document )
{
    std::vector<std::string> problems;
    if ( !document.isObject() )
    {
        problems.push_back( "manifest must be a JSON object" );
        return problems;
    }
    if ( !document.isMember( "kind" ) || document["kind"].asString() != "cartography_export_manifest" )
        problems.push_back( "kind must be 'cartography_export_manifest'" );
    if ( !document.isMember( "manifest_version" ) ||
         document["manifest_version"].asInt() != kExportManifestVersion )
        problems.push_back( "unsupported manifest_version" );
    for ( const char *member : { "layout_name", "format", "structural_digest" } )
        if ( !document.isMember( member ) || !document[member].isString() )
            problems.push_back( std::string( "missing string member: " ) + member );
    if ( !document.isMember( "dpi" ) || !document["dpi"].isNumeric() )
        problems.push_back( "missing numeric member: dpi" );
    if ( !document.isMember( "pages" ) || !document["pages"].isArray() )
    {
        problems.push_back( "missing pages array" );
        return problems;
    }
    if ( document["pages"].size() > kMaxManifestPages )
        problems.push_back( "pages exceed the " + std::to_string( kMaxManifestPages ) +
                            "-entry manifest bound" );
    for ( Json::ArrayIndex index = 0; index < document["pages"].size(); ++index )
    {
        const Json::Value &page = document["pages"][index];
        const std::string where = "pages[" + std::to_string( index ) + "]";
        if ( !page.isObject() )
        {
            problems.push_back( where + " must be an object" );
            continue;
        }
        if ( !page.isMember( "file_name" ) || !isBareName( page["file_name"].asString() ) )
            problems.push_back( where + ".file_name must be a bare name" );
        if ( !page.isMember( "sha256" ) || !isHexDigest( page["sha256"].asString() ) )
            problems.push_back( where + ".sha256 must be a lowercase hex sha256" );
        if ( !page.isMember( "bytes" ) || !page["bytes"].isIntegral() || page["bytes"].asInt64() <= 0 )
            problems.push_back( where + ".bytes must be a positive integer" );
    }
    return problems;
}

bool verifyExportManifestDigest( const Json::Value &document )
{
    if ( !document.isObject() || !document.isMember( "manifest_digest" ) ||
         !document["manifest_digest"].isString() )
        return false;
    // Rebuild the payload from the document's own members (everything the
    // payloadToJson writer emits minus the digest/environment additions).
    Json::Value payload( Json::objectValue );
    for ( const char *member : { "kind", "manifest_version", "layout_name", "format", "dpi",
                                 "provenance", "structural_digest", "page_count", "pages" } )
        payload[member] = document.get( member, Json::Value() );
    const std::string recomputed =
        sha256OfBytes( QByteArray::fromStdString( canonicalJson( payload ) ) );
    return recomputed == document["manifest_digest"].asString();
}

bool writeExportManifest( const std::string &directory, const std::string &base_name,
                          const std::string &format, const ExportManifest &manifest,
                          std::string *error )
{
    const auto fail = [error]( const QString &message ) {
        if ( error )
            *error = message.toStdString();
        return false;
    };
    if ( base_name.empty() || !isBareName( base_name ) || !isBareName( format ) )
        return fail( QStringLiteral( "manifest sidecar needs a bare base name and format" ) );
    if ( manifest.empty() )
        return fail( QStringLiteral( "refusing to write a manifest with zero pages" ) );
    if ( static_cast<int>( manifest.pages.size() ) > kMaxManifestPages )
        return fail( QStringLiteral( "manifest pages exceed the %1-entry bound" )
                       .arg( kMaxManifestPages ) );

    QDir dir( QString::fromStdString( directory ) );
    if ( !dir.exists() && !dir.mkpath( "." ) )
        return fail( QStringLiteral( "cannot create manifest directory '%1'" ).arg( dir.path() ) );

    Json::Value document = manifest.toJson();
    const std::string serialized = canonicalJson( document );
    const QString finalPath =
      dir.filePath( QString::fromStdString( base_name + "." + format + ".manifest.json" ) );

    QTemporaryFile temp( dir.filePath( QString::fromStdString( base_name + ".XXXXXX" ) +
                                       QStringLiteral( ".manifest.json" ) ) );
    temp.setAutoRemove( true );
    if ( !temp.open() )
        return fail( QStringLiteral( "cannot create a temporary manifest in '%1'" ).arg( dir.path() ) );
    const QString tempPath = temp.fileName();
    const QByteArray payloadBytes( serialized.c_str(), static_cast<qsizetype>( serialized.size() ) );
    if ( temp.write( payloadBytes ) != payloadBytes.size() )
        return fail( QStringLiteral( "short write on the temporary manifest" ) );
    if ( !temp.flush() )
        return fail( QStringLiteral( "cannot flush the temporary manifest" ) );
    // Durability gate (atomic_fs.h contract): the flushed bytes must reach
    // the device before either rename mechanism commits the directory entry.
    sicnu::geo::atomic_fs::fsyncFile( tempPath.toStdString() );

    // Prefer QTemporaryFile::rename (closes + clears auto-remove). On
    // Windows overwrite refusal, fall through to atomic_fs publish rather
    // than remove+rename (#1178).
    if ( !temp.rename( finalPath ) )
    {
        try
        {
            sicnu::geo::atomic_fs::publishStagedFile( tempPath.toStdString(),
                                                      finalPath.toStdString() );
        }
        catch ( const sicnu::geo::GeoError &ex )
        {
            return fail( QStringLiteral( "cannot publish the manifest at '%1': %2" )
                           .arg( finalPath, QString::fromUtf8( ex.what() ) ) );
        }
    }
    // The rename/publish moved the tracked file itself: a still-set
    // autoRemove would unlink the DELIVERED manifest at scope exit.
    temp.setAutoRemove( false );
    return true;
}

Json::Value readExportManifest( const std::string &directory, const std::string &base_name,
                                const std::string &format, bool *digest_ok, std::string *error )
{
    if ( digest_ok )
        *digest_ok = false;
    const auto fail = [error]( const QString &message ) {
        if ( error )
            *error = message.toStdString();
        return Json::Value( Json::nullValue );
    };
    if ( base_name.empty() || !isBareName( base_name ) || !isBareName( format ) )
        return fail( QStringLiteral( "manifest read needs a bare base name and format" ) );
    QDir dir( QString::fromStdString( directory ) );
    const QString path =
      dir.filePath( QString::fromStdString( base_name + "." + format + ".manifest.json" ) );
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) )
        return fail( QStringLiteral( "cannot read manifest '%1'" ).arg( path ) );
    const QByteArray raw = file.readAll();
    file.close();

    Json::CharReaderBuilder builder;
    std::string parseErrors;
    Json::Value document;
    std::istringstream stream( raw.toStdString() );
    if ( !Json::parseFromStream( builder, stream, &document, &parseErrors ) )
        return fail( QStringLiteral( "manifest parse failed: %1" )
                       .arg( QString::fromStdString( parseErrors ) ) );

    const std::vector<std::string> problems = validateExportManifest( document );
    if ( !problems.empty() )
        return fail( QStringLiteral( "invalid manifest: %1" )
                       .arg( QString::fromStdString( problems.front() ) ) );
    if ( digest_ok )
        *digest_ok = verifyExportManifestDigest( document );
    return document;
}

} // namespace sicnu::agent::cartography
