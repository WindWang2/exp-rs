/***************************************************************************
 * exprs/plugin_package.cpp
 ***************************************************************************/
#include "exprs/plugin_package.h"

#include <algorithm>
#include <chrono>
#include <array>
#include <cstdint>
#include <cstring>
#ifndef _WIN32
#include <unistd.h>
#else
#include <windows.h>
#endif
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include "exprs/plugin_discovery.h"
#include "exprs/plugin_validator.h"

namespace exprs {

namespace {

namespace fs = std::filesystem;

/// Directory test that does NOT follow symlinks (install payloads must not
/// gain directories through symlink hops).
bool isDirectory( const std::string &path )
{
    std::error_code error;
    return fs::symlink_status( fs::path( path ), error ).type() == fs::file_type::directory;
}

/// True when @p candidate is strictly inside @p root (lexical containment,
/// no ".." components — protects against symlink/zip-slip escapes).
bool contained( const std::string &root, const std::string &candidate )
{
    if ( candidate.size() <= root.size() )
        return false;
    if ( candidate.compare( 0, root.size(), root ) != 0 )
        return false;
    if ( candidate[root.size()] != '/' )
        return false;
    return candidate.find( "..", root.size() + 1 ) == std::string::npos;
}

bool copyTree( const std::string &source, const std::string &target, std::string &error )
{
    std::error_code iteratorError;
    fs::directory_iterator iterator( fs::path( source ), iteratorError );
    if ( iteratorError )
    {
        error = "cannot open " + source;
        return false;
    }
    std::error_code createError;
    fs::create_directory( fs::path( target ), createError );
    if ( createError && !fs::is_directory( fs::path( target ) ) )
    {
        error = "cannot create " + target;
        return false;
    }
    bool ok = true;
    for ( const fs::directory_entry &entry : iterator )
    {
        if ( !ok )
            break;
        const std::string name = entry.path().filename().generic_string();
        if ( name.empty() || name.front() == '.' )
            continue; // skip cache indexes and hidden files
        const std::string childSource = source + "/" + name;
        const std::string childTarget = target + "/" + name;
        if ( !contained( source, childSource ) || !contained( target, childTarget ) )
        {
            error = "path escape refused: " + childSource;
            ok = false;
            break;
        }
        // symlink_status: symlinks/devices/fifos are refused, not followed.
        std::error_code statusError;
        const fs::file_status status = fs::symlink_status( entry.path(), statusError );
        if ( statusError )
        {
            error = "cannot stat " + childSource;
            ok = false;
            break;
        }
        if ( status.type() == fs::file_type::directory )
        {
            ok = copyTree( childSource, childTarget, error );
        }
        else if ( status.type() == fs::file_type::regular )
        {
            std::ifstream input( childSource, std::ios::binary );
            std::ofstream output( childTarget, std::ios::binary | std::ios::trunc );
            if ( !input || !output )
            {
                error = "cannot copy " + childSource;
                ok = false;
                break;
            }
            output << input.rdbuf();
            // Preserve the source mode where the platform supports it.
            std::error_code permissionsError;
            fs::permissions( fs::path( childTarget ), status.permissions(),
                             fs::perm_options::replace, permissionsError );
            (void)permissionsError;
        }
        else
        {
            error = "refusing non-regular entry in package: " + childSource;
            ok = false;
            break;
        }
    }
    return ok;
}

bool removeTree( const std::string &path )
{
    std::error_code error;
    // remove_all on a symlink removes the link itself, not the target.
    fs::remove_all( fs::path( path ), error );
    return !error && !fs::exists( fs::path( path ) );
}

// ---- SHA-256 (self-contained; the SDK is Qt-free, so the Qt-based
// artifact_digest helper of the operators layer cannot be reused here).

class Sha256
{
public:
    void update( const unsigned char *data, size_t length )
    {
        for ( size_t i = 0; i < length; ++i )
        {
            mBuffer[ mBufferLength ] = data[ i ];
            if ( ++mBufferLength == 64 )
            {
                transform( mBuffer.data() );
                mBufferLength = 0;
            }
        }
        mTotalLength += length;
    }

    std::string hex()
    {
        // Bit count of the ORIGINAL message — captured before padding, whose
        // bytes must not enter the length field.
        const unsigned long long bits = mTotalLength * 8ULL;
        unsigned char padding = 0x80;
        update( &padding, 1 );
        padding = 0;
        while ( mBufferLength != 56 )
            update( &padding, 1 );
        std::array<unsigned char, 8> lengthBytes{};
        for ( int i = 0; i < 8; ++i )
            lengthBytes[ i ] = static_cast<unsigned char>( bits >> ( 56 - i * 8 ) );
        update( lengthBytes.data(), 8 );

        std::ostringstream output;
        for ( const uint32_t word : mState )
            output << toHexByte( ( word >> 24 ) & 0xFF ) << toHexByte( ( word >> 16 ) & 0xFF )
                   << toHexByte( ( word >> 8 ) & 0xFF ) << toHexByte( word & 0xFF );
        return output.str();
    }

private:
    static std::string toHexByte( uint32_t value )
    {
        static const char *digits = "0123456789abcdef";
        std::string out( 2, '0' );
        out[ 0 ] = digits[ ( value >> 4 ) & 0xF ];
        out[ 1 ] = digits[ value & 0xF ];
        return out;
    }

    static uint32_t rotateRight( uint32_t value, unsigned count )
    {
        return ( value >> count ) | ( value << ( 32 - count ) );
    }

    void transform( const unsigned char *block )
    {
        static const uint32_t k[ 64 ] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
            0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
            0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
            0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
            0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
            0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
            0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
            0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
            0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
        };
        std::array<uint32_t, 64> w{};
        for ( int i = 0; i < 16; ++i )
            w[ i ] = ( uint32_t( block[ i * 4 ] ) << 24 ) | ( uint32_t( block[ i * 4 + 1 ] ) << 16 )
                     | ( uint32_t( block[ i * 4 + 2 ] ) << 8 ) | uint32_t( block[ i * 4 + 3 ] );
        for ( int i = 16; i < 64; ++i )
        {
            const uint32_t s0 =
                rotateRight( w[ i - 15 ], 7 ) ^ rotateRight( w[ i - 15 ], 18 ) ^ ( w[ i - 15 ] >> 3 );
            const uint32_t s1 =
                rotateRight( w[ i - 2 ], 17 ) ^ rotateRight( w[ i - 2 ], 19 ) ^ ( w[ i - 2 ] >> 10 );
            w[ i ] = w[ i - 16 ] + s0 + w[ i - 7 ] + s1;
        }
        uint32_t a = mState[ 0 ], b = mState[ 1 ], c = mState[ 2 ], d = mState[ 3 ];
        uint32_t e = mState[ 4 ], f = mState[ 5 ], g = mState[ 6 ], h = mState[ 7 ];
        for ( int i = 0; i < 64; ++i )
        {
            const uint32_t s1 = rotateRight( e, 6 ) ^ rotateRight( e, 11 ) ^ rotateRight( e, 25 );
            const uint32_t ch = ( e & f ) ^ ( ~e & g );
            const uint32_t temp1 = h + s1 + ch + k[ i ] + w[ i ];
            const uint32_t s0 = rotateRight( a, 2 ) ^ rotateRight( a, 13 ) ^ rotateRight( a, 22 );
            const uint32_t maj = ( a & b ) ^ ( a & c ) ^ ( b & c );
            const uint32_t temp2 = s0 + maj;
            h = g; g = f; f = e; e = d + temp1;
            d = c; c = b; b = a; a = temp1 + temp2;
        }
        mState[ 0 ] += a; mState[ 1 ] += b; mState[ 2 ] += c; mState[ 3 ] += d;
        mState[ 4 ] += e; mState[ 5 ] += f; mState[ 6 ] += g; mState[ 7 ] += h;
        std::memset( w.data(), 0, sizeof( w ) );
    }

    std::array<uint32_t, 8> mState { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    std::array<unsigned char, 64> mBuffer{};
    size_t mBufferLength = 0;
    unsigned long long mTotalLength = 0;
};

/// Streaming SHA-256 of a file (lowercase hex). "" when unreadable.
std::string fileSha256Hex( const std::string &path )
{
    std::ifstream input( path, std::ios::binary );
    if ( !input )
        return std::string();
    Sha256 hash;
    std::vector<char> buffer( 256 * 1024 );
    while ( input )
    {
        input.read( buffer.data(), static_cast<std::streamsize>( buffer.size() ) );
        const std::streamsize read = input.gcount();
        if ( read > 0 )
            hash.update( reinterpret_cast<const unsigned char *>( buffer.data() ),
                         static_cast<size_t>( read ) );
    }
    if ( input.bad() )
        return std::string();
    return hash.hex();
}

long currentProcessId()
{
#ifdef _WIN32
    return static_cast<long>( ::GetCurrentProcessId() );
#else
    return static_cast<long>( ::getpid() );
#endif
}

/// Verifies the declared "package.checksums" object against the staged
/// copy. Declared files that are missing from the payload fail; files
/// without a declaration pass unchecked (declared checksums are an
/// integrity ADD-ON, not a manifest of completeness).
bool verifyChecksums( const std::string &stagingDir, const Json::Value &checksums,
                      std::string &error )
{
    for ( const auto &relativePath : checksums.getMemberNames() )
    {
        // Type and shape guards BEFORE any use: an empty key would be UB in
        // front(), and a declared digest that is not 64 hex chars can never
        // match (fail it here so the error says why).
        const Json::Value &declaredValue = checksums[ relativePath ];
        if ( relativePath.empty() || !declaredValue.isString()
             || declaredValue.asString().size() != 64
             || declaredValue.asString().find_first_not_of( "0123456789abcdefABCDEF" )
                    != std::string::npos )
        {
            error = "invalid checksum declaration for '" + relativePath
                        + "' (need a 64-char hex digest)";
            return false;
        }
        const std::string declared = declaredValue.asString();
        if ( declared.find( ".." ) != std::string::npos
             || relativePath.find( ".." ) != std::string::npos
             || relativePath.front() == '/' )
        {
            error = "suspicious checksum path: " + relativePath;
            return false;
        }
        const std::string actual = fileSha256Hex( stagingDir + "/" + relativePath );
        if ( actual.empty() )
        {
            error = "checksummed file missing from payload: " + relativePath;
            return false;
        }
        if ( actual != declared )
        {
            error = "checksum mismatch for " + relativePath + ": declared " + declared
                        + ", actual " + actual;
            return false;
        }
    }
    return true;
}

} // namespace

bool PluginPackage::install( const std::string &sourceDir, std::string &installedDir,
                             PluginDiagnosticLog &log )
{
    PluginDiagnostic diagnostic;
    diagnostic.file = sourceDir;
    PluginManifest manifest;
    if ( !loadManifestFromFile( sourceDir + "/plugin.json", manifest, diagnostic ) )
    {
        log.add( diagnostic );
        return false;
    }

    PluginValidationRequest request;
    request.pluginDir = sourceDir;
    if ( !PluginManifestValidator::validate( manifest, request, log ) )
        return false;

    if ( !PluginManifestValidator::isValidPluginId( manifest.id ) )
    {
        PluginDiagnostic failure;
        failure.code = PluginDiagnosticCode::ManifestInvalidField;
        failure.pluginId = manifest.id;
        failure.message = "invalid plugin id — refused";
        log.add( failure );
        return false;
    }
    const std::string userRoot = PluginDiscovery::userPluginRoot();
    const std::string target = userRoot + "/" + manifest.id;

    // Guard against id takeover by a different payload (an existing
    // directory with a manifest declaring a different id).
    PluginDiagnostic existingError;
    PluginManifest existing;
    if ( loadManifestFromFile( target + "/plugin.json", existing, existingError )
         && existing.id != manifest.id )
    {
        PluginDiagnostic conflict;
        conflict.code = PluginDiagnosticCode::ManifestDuplicateId;
        conflict.pluginId = manifest.id;
        conflict.file = target;
        conflict.message = "target directory hosts a different plugin id '" + existing.id + "'";
        log.add( conflict );
        return false;
    }

    // Create the user root chain.
    std::error_code createError;
    fs::create_directories( fs::path( userRoot ), createError );
    if ( createError && !fs::is_directory( fs::path( userRoot ) ) )
    {
        PluginDiagnostic failure;
        failure.code = PluginDiagnosticCode::ResourceMissing;
        failure.pluginId = manifest.id;
        failure.message = "cannot create user plugin root " + userRoot;
        log.add( failure );
        return false;
    }

    // ---- Staged install (plugin-platform 8.0): copy into a staging
    // directory on the SAME filesystem, verify declared checksums, then
    // swap atomically with rollback. A failed upgrade leaves the previous
    // install INTACT (the v1 flow removed it before copying).
    const std::string stagingRoot = userRoot + "/.staging";
    std::error_code stagingError;
    fs::create_directories( fs::path( stagingRoot ), stagingError );
    if ( stagingError && !fs::is_directory( fs::path( stagingRoot ) ) )
    {
        PluginDiagnostic failure;
        failure.code = PluginDiagnosticCode::ResourceMissing;
        failure.pluginId = manifest.id;
        failure.message = "cannot create staging root " + stagingRoot;
        log.add( failure );
        return false;
    }
    // Sweep staging leftovers of CRASHED installs (older than 24 h): only
    // the current pid's own directory is otherwise removed.
    {
        std::error_code sweepError;
        const auto now = fs::file_time_type::clock::now();
        fs::directory_iterator stagingIterator( fs::path( stagingRoot ), sweepError );
        if ( !sweepError )
        {
            for ( const fs::directory_entry &entry : stagingIterator )
            {
                std::error_code timeError;
                const auto lastWrite = fs::last_write_time( entry.path(), timeError );
                if ( timeError )
                    continue;
                if ( now - lastWrite > std::chrono::hours( 24 ) )
                    fs::remove_all( entry.path(), sweepError );
            }
        }
    }
    const std::string stagingDir =
        stagingRoot + "/" + manifest.id + "." + std::to_string( currentProcessId() );
    removeTree( stagingDir ); // stale staging from a crashed sibling run

    std::string copyError;
    if ( !copyTree( sourceDir, stagingDir, copyError ) )
    {
        PluginDiagnostic failure;
        failure.code = PluginDiagnosticCode::ResourceMissing;
        failure.pluginId = manifest.id;
        failure.message = copyError;
        log.add( failure );
        removeTree( stagingDir );
        return false;
    }

    // Declared checksums: integrity of the staged payload (declared
    // checksums detect CORRUPTION, they do not authenticate a signer).
    const Json::Value &packageJson = manifest.package;
    if ( packageJson.isObject() && packageJson.isMember( "checksums" ) )
    {
        const Json::Value &checksums = packageJson[ "checksums" ];
        std::string checksumError;
        if ( !checksums.isObject()
             || !verifyChecksums( stagingDir, checksums, checksumError ) )
        {
            PluginDiagnostic failure;
            failure.code = PluginDiagnosticCode::ManifestInvalidField;
            failure.pluginId = manifest.id;
            failure.message = "package checksum verification failed: " + checksumError;
            log.add( failure );
            removeTree( stagingDir );
            return false;
        }
    }
    if ( packageJson.isObject() && packageJson.isMember( "sbom" ) )
    {
        PluginDiagnostic note;
        note.code = PluginDiagnosticCode::None;
        note.severity = PluginDiagnosticSeverity::Info;
        note.pluginId = manifest.id;
        note.message = "package carries SBOM metadata (format "
                           + packageJson[ "sbom" ].get( "format", "" ).asString() + ", path "
                           + packageJson[ "sbom" ].get( "path", "" ).asString()
                           + "); carried as metadata, integrity-only contract";
        log.add( note );
    }

    // Atomic swap with rollback: previous install moves aside, staging
    // takes its place, the aside copy is dropped; any failure puts the
    // previous install back.
    const std::string backupDir = stagingRoot + "/" + manifest.id + ".old."
                                  + std::to_string( static_cast<long>(
#ifdef _WIN32
        ::GetCurrentProcessId()
#else
        ::getpid()
#endif
        ) );
    removeTree( backupDir );
    const bool hadPrevious = isDirectory( target );
    if ( hadPrevious )
    {
        std::error_code renameError;
        fs::rename( fs::path( target ), fs::path( backupDir ), renameError );
        if ( renameError )
        {
            PluginDiagnostic failure;
            failure.code = PluginDiagnosticCode::ResourceMissing;
            failure.pluginId = manifest.id;
            failure.message = "cannot move the previous install aside at " + target;
            log.add( failure );
            removeTree( stagingDir );
            return false;
        }
    }
    {
        std::error_code renameError;
        fs::rename( fs::path( stagingDir ), fs::path( target ), renameError );
        if ( renameError )
        {
            if ( hadPrevious )
            {
                std::error_code restoreError;
                fs::rename( fs::path( backupDir ), fs::path( target ), restoreError );
            }
            PluginDiagnostic failure;
            failure.code = PluginDiagnosticCode::ResourceMissing;
            failure.pluginId = manifest.id;
            failure.message = "cannot promote the staged install at " + target
                                  + "; previous install restored";
            log.add( failure );
            removeTree( stagingDir );
            return false;
        }
    }
    if ( hadPrevious )
        removeTree( backupDir );
    removeTree( stagingDir );

    installedDir = target;
    PluginDiagnostic success;
    success.code = PluginDiagnosticCode::None;
    success.severity = PluginDiagnosticSeverity::Info;
    success.pluginId = manifest.id;
    success.message = "installed " + manifest.id + " " + manifest.version + " into " + target;
    log.add( success );
    return true;
}

bool PluginPackage::uninstall( const std::string &pluginId, PluginDiagnosticLog &log )
{
    // The id comes from the CLI/SDK surface and becomes a path: gate it on
    // the manifest id grammar BEFORE any path arithmetic, and verify the
    // result stays inside the user root (defence in depth against '..').
    if ( !PluginManifestValidator::isValidPluginId( pluginId ) )
    {
        PluginDiagnostic failure;
        failure.code = PluginDiagnosticCode::ManifestInvalidField;
        failure.pluginId = pluginId;
        failure.message = "invalid plugin id — refused";
        log.add( failure );
        return false;
    }
    const std::string userRoot = PluginDiscovery::userPluginRoot();
    const std::string target = userRoot + "/" + pluginId;
    if ( !contained( userRoot, target ) )
    {
        PluginDiagnostic failure;
        failure.code = PluginDiagnosticCode::ManifestInvalidField;
        failure.pluginId = pluginId;
        failure.message = "resolved path escapes the user plugin root — refused";
        log.add( failure );
        return false;
    }
    if ( !isDirectory( target ) )
    {
        PluginDiagnostic failure;
        failure.code = PluginDiagnosticCode::EntrypointMissing;
        failure.pluginId = pluginId;
        failure.message = "plugin is not installed in the user root (" + target + ")";
        log.add( failure );
        return false;
    }
    PluginDiagnostic existingError;
    PluginManifest existing;
    if ( loadManifestFromFile( target + "/plugin.json", existing, existingError )
         && existing.id != pluginId )
    {
        PluginDiagnostic conflict;
        conflict.code = PluginDiagnosticCode::ManifestDuplicateId;
        conflict.pluginId = pluginId;
        conflict.message = "refusing to remove: directory hosts plugin '" + existing.id + "'";
        log.add( conflict );
        return false;
    }
    if ( !removeTree( target ) )
    {
        PluginDiagnostic failure;
        failure.code = PluginDiagnosticCode::ResourceMissing;
        failure.pluginId = pluginId;
        failure.message = "cannot remove " + target;
        log.add( failure );
        return false;
    }
    return true;
}

std::vector<std::string> PluginPackage::installedIds()
{
    std::vector<std::string> ids;
    const std::string userRoot = PluginDiscovery::userPluginRoot();
    std::error_code error;
    fs::directory_iterator iterator( fs::path( userRoot ), error );
    if ( error )
        return ids;
    for ( const fs::directory_entry &entry : iterator )
    {
        const std::string name = entry.path().filename().generic_string();
        if ( name.empty() || name.front() == '.' )
            continue;
        PluginDiagnostic manifestError;
        PluginManifest manifest;
        if ( loadManifestFromFile( userRoot + "/" + name + "/plugin.json", manifest,
                                   manifestError ) )
            ids.push_back( manifest.id );
    }
    std::sort( ids.begin(), ids.end() );
    return ids;
}

} // namespace exprs
