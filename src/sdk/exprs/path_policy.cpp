/***************************************************************************
 * exprs/path_policy.cpp
 ***************************************************************************/
#include "exprs/path_policy.h"

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
    const std::string rootText = root.generic_string();
    const std::string candidateText = candidate.generic_string();
    if ( candidateText == rootText )
        return true; // the root itself is contained (cwd == workspace root)
    if ( candidateText.size() <= rootText.size() )
        return false;
    if ( candidateText.compare( 0, rootText.size(), rootText ) != 0 )
        return false;
    if ( rootText.back() == '/' )
        return true;
    return candidateText[rootText.size()] == '/';
}

} // namespace

PathPolicyRejection PathPolicy::checkRelativeLexically( const std::string &candidate )
{
    if ( candidate.empty() )
        return PathPolicyRejection::Empty;
    const fs::path path( candidate );
    if ( path.is_absolute() )
        return PathPolicyRejection::Absolute;
    for ( const fs::path &component : path )
    {
        if ( component == ".." )
            return PathPolicyRejection::DotDot;
    }
    return PathPolicyRejection::Accepted;
}

std::string PathPolicy::canonical( const std::string &path )
{
    if ( path.empty() )
        return {};
    std::error_code error;
    const fs::path resolved = fs::weakly_canonical( fs::path( path ), error );
    if ( error )
        return {};
    return resolved.generic_string();
}

bool PathPolicy::isAbsolute( const std::string &path )
{
    return !path.empty() && fs::path( path ).is_absolute();
}

PathPolicyRejection PathPolicy::checkPayloadInsideRoot( const std::string &root,
                                                        const std::string &candidate,
                                                        std::string &resolvedPath )
{
    resolvedPath.clear();
    const PathPolicyRejection lexical = checkRelativeLexically( candidate );
    if ( lexical != PathPolicyRejection::Accepted )
        return lexical;

    std::error_code error;
    const fs::path canonicalRoot = fs::canonical( fs::path( root ), error );
    if ( error )
        return PathPolicyRejection::NotCanonical;

    // weakly_canonical follows symlinks on the existing prefix — a symlink
    // inside the root pointing outside resolves to the outside target.
    const fs::path resolved = fs::weakly_canonical( canonicalRoot / fs::path( candidate ), error );
    if ( error )
        return PathPolicyRejection::NotCanonical;

    if ( !fs::exists( resolved, error ) )
        return PathPolicyRejection::Missing;
    if ( !fs::is_regular_file( resolved, error ) )
        return PathPolicyRejection::NotRegularFile;
    if ( !isInside( canonicalRoot, resolved ) )
        return PathPolicyRejection::OutsideRoot;

    resolvedPath = resolved.generic_string();
    return PathPolicyRejection::Accepted;
}

bool PathPolicy::resolvesInsideRoot( const std::string &root, const std::string &candidate )
{
    if ( root.empty() || candidate.empty() )
        return false;
    std::error_code error;
    const fs::path canonicalRoot = fs::weakly_canonical( fs::path( root ), error );
    if ( error )
        return false;
    const fs::path resolved = fs::weakly_canonical( fs::path( candidate ), error );
    if ( error )
        return false;
    return isInside( canonicalRoot, resolved );
}

std::string PathPolicy::workspaceRoot()
{
    const char *raw = std::getenv( "SICNU_MCP_WORKSPACE" );
    if ( !raw || !*raw )
        return {};
    return canonical( raw );
}

} // namespace exprs
