// capsule_portability.h — path portability for ReproducibilityCapsules.
//
// Privacy/portability doctrine: an absolute LOCAL path must never become
// capsule identity (and never enter the document at all). Recorded paths are
// rewritten to portable references:
//
//   <workspaceRoot>/out/raster.tif   →  "workspace:out/raster.tif"
//   anything else                    →  "external:<file-name>"
//
// The content digest recorded beside the reference is the real identity; the
// reference only tells a HUMAN (or replay tool) where to look first. Forward
// slashes are canonical so documents survive Windows⇄POSIX relocation.
#pragma once

#include <QString>

namespace sicnu::experiment::capsule
{

/// Rewrites @p path (as recorded on the producing machine) against
/// @p workspaceRoot (the producing workspace's absolute root; may be empty).
/// Never returns a string containing the absolute path.
QString toPortableRef( const QString &path, const QString &workspaceRoot );

/// True when @p ref is a portable reference (workspace:/external: scheme).
bool isPortableRef( const QString &ref );

} // namespace sicnu::experiment::capsule
