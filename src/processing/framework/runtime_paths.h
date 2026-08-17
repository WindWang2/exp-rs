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
#include <QFile>
#include <QString>
#include <QStringList>

namespace sicnu::processing {

/// Resolve a path relative to the project/install root.
/// Supports in-tree builds, out-of-tree builds, test runners, installed packages,
/// and explicit SICNU_DATA_DIR environment overrides.
inline QString resolveRuntimeDataPath( const QString &relativePath )
{
    // 1. Explicit environment override
    const QString envDataDir = qEnvironmentVariable( "SICNU_DATA_DIR" );
    if ( !envDataDir.isEmpty() )
    {
        const QString cand = QDir( envDataDir ).filePath( relativePath );
        if ( QFile::exists( cand ) || QDir( cand ).exists() )
            return cand;
    }

    // 2. Walk up from applicationDirPath() looking for a project root marker (data/ or CMakeLists.txt)
    QDir dir( QCoreApplication::applicationDirPath() );
    while ( !dir.exists( QStringLiteral( "data" ) )
            && !dir.exists( QStringLiteral( "CMakeLists.txt" ) )
            && dir.cdUp() )
    {
    }
    if ( dir.exists( QStringLiteral( "data" ) ) || dir.exists( QStringLiteral( "CMakeLists.txt" ) ) )
    {
        const QString cand = dir.filePath( relativePath );
        if ( QFile::exists( cand ) || QDir( cand ).exists() )
            return cand;
    }

    // 3. Walk up from current working directory
    QDir curDir = QDir::current();
    while ( !curDir.exists( QStringLiteral( "data" ) )
            && !curDir.exists( QStringLiteral( "CMakeLists.txt" ) )
            && curDir.cdUp() )
    {
    }
    if ( curDir.exists( QStringLiteral( "data" ) ) || curDir.exists( QStringLiteral( "CMakeLists.txt" ) ) )
    {
        const QString cand = curDir.filePath( relativePath );
        if ( QFile::exists( cand ) || QDir( cand ).exists() )
            return cand;
    }

    // 4. Standard install layouts relative to app dir
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList installCandidates = {
        QDir( appDir ).filePath( relativePath ),
        QDir( appDir ).filePath( QStringLiteral( "../" ) + relativePath ),
        QDir( appDir ).filePath( QStringLiteral( "../share/sicnu_geo_rs/" ) + relativePath ),
        QDir( appDir ).filePath( QStringLiteral( "share/sicnu_geo_rs/" ) + relativePath ),
        QDir( appDir ).filePath( QStringLiteral( "../share/" ) + relativePath )
    };
    for ( const QString &cand : installCandidates )
    {
        if ( QFile::exists( cand ) || QDir( cand ).exists() )
            return cand;
    }

#ifdef SICNU_SOURCE_DIR
    // 5. Compiled source directory (for out-of-tree builds / tests)
    const QString srcCand = QDir( QStringLiteral( SICNU_SOURCE_DIR ) ).filePath( relativePath );
    if ( QFile::exists( srcCand ) || QDir( srcCand ).exists() )
        return srcCand;
#endif

    // Fallback: return resolved path against the marker dir if found, else original walk
    return dir.absoluteFilePath( relativePath );
}

} // namespace sicnu::processing
