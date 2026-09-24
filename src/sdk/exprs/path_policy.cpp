/***************************************************************************
 * exprs/path_policy.cpp
 ***************************************************************************/
#include "exprs/path_policy.h"

#include "platform/portable.h"

#include <cstdlib>

#include <filesystem>

namespace exprs {

namespace fs = std::filesystem;

const char *pathPolicyRejectionName( PathPolicyRejection rejection )
{
    switch ( rejection )
    {
        case PathPolicyRejection::Accepted:
            return "accepted";
        case PathPolicyRejection::Empty:
            return "empty";
        case PathPolicyRejection::Absolute:
            return "absolute";
        case PathPolicyRejection::DotDot:
            return "dotdot";
        case PathPolicyRejection::NotRegularFile:
            return "not_regular_file";
        case PathPolicyRejection::Missing:
            return "missing";
        case PathPolicyRejection::OutsideRoot:
            return "outside_root";
        case PathPolicyRejection::NotCanonical:
            return "not_canonical";
    }
    return "unknown";
}

namespace {

/// True when @p candidate (already canonical) equals @p root or lies under
/// it (root + separator + at least one character).
bool isInside( const fs::path &root, const fs::path &candidate )
{
    // Rendered as UTF-8 on every platform: generic_string() is ACP-encoded
    // on Windows, and a '?'-substituted rendering could make two different
    // paths compare equal under the prefix test below.
    const std::string rootText = pathToUtf8String( root );
    const std::string candidateText = pathToUtf8String( candidate );
    if ( candidateText == rootText )
        return true; // the root itself is contained (cwd == workspace root)
    if ( candidateText.size() <= rootText.size() )
        return false;
    if ( candidateText.compare( 0, rootText.size(), rootText ) != 0 )
        return false;
    if ( rootText.empty() || rootText.back() == '/' )
        return true;
    return candidateText[rootText.size()] == '/';
}

/// Builds a platform path from the policy's UTF-8 text inputs.
///
/// std::filesystem's std::string constructor decodes with the process' ANSI
/// code page on MSVC, which THROWS std::system_error for UTF-8 byte sequences
/// that have no ANSI mapping ("unicode-é中.txt" from a manifest on a zh-CN
/// host). Path text here is UTF-8 by contract (manifests, IPC, path fields), so
/// the UTF-8 decoding constructor is used, and the conversion is still
/// guarded: a byte sequence that is not valid UTF-8 cannot be a path we could
/// resolve anyway, and it must surface as a typed rejection rather than an
/// exception out of the policy.
fs::path pathFromUtf8( const std::string &text )
{
    return fs::path( std::u8string( reinterpret_cast<const char8_t *>( text.data() ),
                                    text.size() ) );
}

/// The UTF-8 twin of pathFromUtf8: path::generic_string() re-encodes through
/// the process ANSI code page on MSVC, so every path RENDERED back into the
/// std::string world here goes through the wide native form instead.
std::string pathToUtf8String( const fs::path &path )
{
    const std::u8string u8 = path.generic_u8string();
    return std::string( u8.begin(), u8.end() );
}

} // namespace

PathPolicyRejection PathPolicy::checkRelativeLexically( const std::string &candidate )
{
    if ( candidate.empty() )
        return PathPolicyRejection::Empty;
    // The whole body is guarded: MSVC's std::filesystem converts through the
    // process' ANSI code page and THROWS lazily (component iteration, compare)
    // for text that has no ANSI mapping. A path that the platform cannot even
    // express can never be a manifest-relative payload, so that is a typed
    // rejection rather than an exception out of the policy.
    try
    {
        const fs::path path = pathFromUtf8( candidate );
        if ( path.is_absolute() )
            return PathPolicyRejection::Absolute;
        for ( const fs::path &component : path )
        {
            if ( component == ".." )
                return PathPolicyRejection::DotDot;
        }
        return PathPolicyRejection::Accepted;
    }
    catch ( const std::exception & )
    {
        return PathPolicyRejection::NotCanonical;
    }
}

std::string PathPolicy::canonical( const std::string &path )
{
    if ( path.empty() )
        return {};
    try
    {
        std::error_code error;
        const fs::path resolved = fs::weakly_canonical( pathFromUtf8( path ), error );
        if ( error )
            return {};
        return pathToUtf8String( resolved );
    }
    catch ( const std::exception & )
    {
        return {};
    }
}

bool PathPolicy::isAbsolute( const std::string &path )
{
    if ( path.empty() )
        return false;
    try
    {
        return pathFromUtf8( path ).is_absolute();
    }
    catch ( const std::exception & )
    {
        return false;
    }
}

PathPolicyRejection PathPolicy::checkPayloadInsideRoot( const std::string &root,
                                                        const std::string &candidate,
                                                        std::string &resolvedPath )
{
    resolvedPath.clear();
    const PathPolicyRejection lexical = checkRelativeLexically( candidate );
    if ( lexical != PathPolicyRejection::Accepted )
        return lexical;

    try
    {
        std::error_code error;
        const fs::path canonicalRoot = fs::canonical( pathFromUtf8( root ), error );
        if ( error )
            return PathPolicyRejection::NotCanonical;

        // weakly_canonical follows symlinks on the existing prefix — a symlink
        // inside the root pointing outside resolves to the outside target.
        const fs::path resolved =
            fs::weakly_canonical( canonicalRoot / pathFromUtf8( candidate ), error );
        if ( error )
            return PathPolicyRejection::NotCanonical;

        if ( !fs::exists( resolved, error ) )
            return PathPolicyRejection::Missing;
        if ( !fs::is_regular_file( resolved, error ) )
            return PathPolicyRejection::NotRegularFile;
        if ( !isInside( canonicalRoot, resolved ) )
            return PathPolicyRejection::OutsideRoot;

        resolvedPath = pathToUtf8String( resolved );
        return PathPolicyRejection::Accepted;
    }
    catch ( const std::exception & )
    {
        // Unrepresentable text (or any other platform conversion surprise):
        // the payload is refused, never resolved by accident.
        return PathPolicyRejection::NotCanonical;
    }
}

bool PathPolicy::resolvesInsideRoot( const std::string &root, const std::string &candidate )
{
    if ( root.empty() || candidate.empty() )
        return false;
    try
    {
        std::error_code error;
        const fs::path canonicalRoot = fs::weakly_canonical( pathFromUtf8( root ), error );
        if ( error )
            return false;
        const fs::path resolved = fs::weakly_canonical( pathFromUtf8( candidate ), error );
        if ( error )
            return false;
        return isInside( canonicalRoot, resolved );
    }
    catch ( const std::exception & )
    {
        return false;
    }
}

std::string PathPolicy::workspaceRoot()
{
    // The sandbox root is a path value consumed as UTF-8 text: read it
    // through the portable boundary so a non-ASCII workspace path on
    // Windows is not mangled into the ANSI code page before canonical().
    const std::string raw = sicnu::portable::envUtf8( "SICNU_MCP_WORKSPACE" );
    if ( raw.empty() )
        return {};
    return canonical( raw );
}

} // namespace exprs
