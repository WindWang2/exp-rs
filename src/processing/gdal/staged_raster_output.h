// src/processing/gdal/staged_raster_output.h — staged atomic publication for
// direct-path raster writers.
#pragma once

#include <QString>

#include <memory>

#include "geospatial/common.h"
#include "geospatial/util/atomic_fs.h"

namespace sicnu::processing
{

/// Staged publication for raster outputs written directly to a caller-named
/// path (the GUI/CLI paths that bypass the OutputCommitter, #617): the
/// dataset is created at a unique staged path NEXT TO the target
/// (atomic_fs::stagedPathFor keeps the final extension, so extension-driven
/// drivers still recognize the staged dataset), and is published with
/// fsync + atomic rename only after the writer reports success. From
/// construction to a successful publish() the guard owns the staged file:
/// destruction without publish discards the staged group and leaves the
/// target untouched — a failed run can neither leave a partial product at
/// the target path nor destroy the previous result there.
///
/// Thin composition over the geospatial atomic_fs authority (stagedPathFor /
/// fsyncFile / publishStagedFile / discardStaged); no staging semantics are
/// re-implemented here.
///
/// Note: publication renames the staged file OVER the target path, so a
/// pre-existing SYMLINK at the target is replaced as a link (the referent is
/// left untouched) — the same rename(2) semantics publishStagedFile has
/// always had, and strictly safer than the old truncate-in-place write.
class StagedRasterOutput
{
  public:
    /// Allocates the staged path. Throws sicnu::geo::GeoError when the
    /// staging path cannot be allocated (unwritable target directory) — use
    /// makeStagedRasterOutput at call sites that report bool + message.
    explicit StagedRasterOutput( const QString &targetPath )
      : m_target( targetPath ),
        m_staged( QString::fromStdString(
          sicnu::geo::atomic_fs::stagedPathFor( targetPath.toStdString() ) ) ) {}

    ~StagedRasterOutput()
    {
      if ( !m_published )
        sicnu::geo::atomic_fs::discardStaged( m_staged.toStdString() );
    }

    StagedRasterOutput( const StagedRasterOutput & ) = delete;
    StagedRasterOutput &operator=( const StagedRasterOutput & ) = delete;

    /// The path the writer must create/write (same directory and extension
    /// as the target).
    QString stagedPath() const { return m_staged; }

    /// Flushes the staged file to stable storage and atomically renames it
    /// over the target. On failure the staged file is discarded and the
    /// previous target content survives; @a error (if non-null) receives the
    /// reason. Returns false on failure, true on successful publication.
    bool publish( QString *error = nullptr )
    {
      try
      {
        const std::string staged = m_staged.toStdString();
        sicnu::geo::atomic_fs::fsyncFile( staged );
        sicnu::geo::atomic_fs::publishStagedFile( staged, m_target.toStdString() );
      }
      catch ( const sicnu::geo::GeoError &e )
      {
        sicnu::geo::atomic_fs::discardStaged( m_staged.toStdString() );
        m_published = true; // nothing left to discard from the destructor
        if ( error )
          *error = QString::fromUtf8( e.what() );
        return false;
      }
      m_published = true;
      return true;
    }

  private:
    QString m_target;
    QString m_staged;
    bool m_published = false;
};

/// Exception-free construction for bool-returning writer entry points: the
/// staging path is allocated inside; on failure @a error receives the reason
/// and nullptr is returned.
inline std::unique_ptr<StagedRasterOutput> makeStagedRasterOutput( const QString &targetPath,
                                                                   QString *error = nullptr )
{
  try
  {
    return std::unique_ptr<StagedRasterOutput>( new StagedRasterOutput( targetPath ) );
  }
  catch ( const sicnu::geo::GeoError &e )
  {
    if ( error )
      *error = QString::fromUtf8( e.what() );
    return nullptr;
  }
}

} // namespace sicnu::processing
