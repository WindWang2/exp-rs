// src/processing/framework/runtime_paths.h
//
// Pure path-resolution helpers usable from any layer (processing/data/…) without
// an upward include into the app layer. Previously src/processing reached up
// into app/app_paths.h for resolveDataPath() — an inverted dependency
// (perf/architecture goal §3b). This header owns the pure logic; app/app_paths.h
// keeps its app-facing facade and delegates here.
#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QString>

namespace sicnu::processing {

/// Resolve a path relative to the project/install root by walking up from the
/// executable directory until a project marker (CMakeLists.txt or data/) is
/// found. Returns the absolute path (the marker dir + @a relativePath); if no
/// marker is found, returns the relativePath resolved against the topmost dir.
inline QString resolveRuntimeDataPath( const QString &relativePath )
{
    QDir dir( QCoreApplication::applicationDirPath() );
    while ( !dir.exists( QStringLiteral( "CMakeLists.txt" ) )
            && !dir.exists( QStringLiteral( "data" ) )
            && dir.cdUp() )
    {
        // keep going up
    }
    return dir.absoluteFilePath( relativePath );
}

} // namespace sicnu::processing
