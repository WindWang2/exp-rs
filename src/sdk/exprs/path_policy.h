/***************************************************************************
 * exprs/path_policy.h — plugin path containment policy
 *
 * Single owner of the path rules the plugin system enforces:
 *
 *   - entrypoint containment: a plugin payload file must resolve to a
 *     regular file INSIDE the plugin root (canonical comparison, so symlink
 *     hops out of the root are escapes);
 *   - workspace containment: when a workspace root is configured
 *     (SICNU_MCP_WORKSPACE), every filesystem effect of an external-tool
 *     execution must stay inside it.
 *
 * The policy is lexical+canonical path analysis. It is NOT an operating
 * system sandbox: a process spawned under this policy is still the user's
 * process. What the policy does and does not protect against is documented
 * in docs/plugins/isolation.md.
 ***************************************************************************/
#pragma once

#include <string>

namespace exprs {

/// Reason a candidate path was rejected by the containment policy.
enum class PathPolicyRejection
{
    Accepted = 0,
    Empty,            ///< candidate is empty
    Absolute,         ///< candidate is absolute (must be root-relative)
    DotDot,           ///< candidate contains a ".." path component
    NotRegularFile,   ///< resolved target exists but is not a regular file
    Missing,          ///< resolved target does not exist
    OutsideRoot,      ///< resolved target escapes the containment root
    NotCanonical,     ///< root itself could not be canonicalized
};

const char *pathPolicyRejectionName( PathPolicyRejection rejection );

class PathPolicy
{
public:
    /// Lexical pre-check of a manifest-relative path: rejects empty values,
    /// absolute paths and any ".." component before any filesystem access.
    /// Returns Accepted when the value may proceed to canonical resolution.
    static PathPolicyRejection checkRelativeLexically( const std::string &candidate );

    /// Full containment check for a plugin payload:
    ///  1. canonicalizes @p root (must exist);
    ///  2. rejects @p candidate lexically (checkRelativeLexically);
    ///  3. resolves root/candidate with symlinks followed;
    ///  4. requires the resolved target to be a regular file inside the
    ///     canonical root.
    static PathPolicyRejection checkPayloadInsideRoot( const std::string &root,
                                                       const std::string &candidate,
                                                       std::string &resolvedPath );

    /// True when @p candidate resolves inside canonical @p root. Non-existing
    /// candidates are resolved lexically (parent-walk) so output paths that do
    /// not exist yet can still be checked. Absolute and ".." inputs follow the
    /// same resolution; callers that need to forbid them use
    /// checkRelativeLexically first.
    static bool resolvesInsideRoot( const std::string &root, const std::string &candidate );

    /// Canonicalizes @p path; empty string when canonicalization fails.
    static std::string canonical( const std::string &path );

    /// True when @p path is an absolute path in the platform syntax.
    static bool isAbsolute( const std::string &path );

    /// The configured workspace root (SICNU_MCP_WORKSPACE), canonicalized.
    /// Empty when the variable is unset/empty or cannot be canonicalized.
    static std::string workspaceRoot();
};

} // namespace exprs
