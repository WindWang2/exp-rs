// src/processing/providers/qgis_algorithms/algorithm_write_guards.h
//
// Shared error-path helpers for the qgis_algorithms provider (#1043):
// every sink/raster/file write failure must fail the algorithm with a typed
// QgsProcessingException, and a failed or canceled run must never leave a
// plausible-looking partial output file behind at a user-visible path.
#pragma once

#include <QString>
#include <QStringList>
#include <QFileInfo>
#include <QFile>

#include <gdal.h>
#include <cpl_conv.h>

#include <qgsfeature.h>
#include <qgsfeaturesink.h>
#include <qgsprocessingfeedback.h>
#include <qgsexception.h>

#include <utility>

namespace sicnu::qgis_algorithms
{

  /**
   * Throws QgsProcessingException when the run was canceled. Algorithms must
   * call this instead of breaking out of their feature/block loops: an early
   * exit that returns normally reports success with a partial output (#1043).
   */
  inline void checkCanceled( const QgsProcessingFeedback *feedback )
  {
    if ( feedback && feedback->isCanceled() )
      throw QgsProcessingException( QObject::tr( "Processing canceled" ) );
  }

  /**
   * Adds a feature to the sink and throws QgsProcessingException when the
   * sink rejects it (disk full, field/geometry mismatch, provider limit).
   * The sink's own lastError() is included when available. Roughly 20 call
   * sites ignored this result and reported success with missing features
   * (#1043).
   */
  inline void addFeatureChecked( QgsFeatureSink *sink, QgsFeature &feature,
                                 QgsProcessingFeedback *feedback = nullptr,
                                 QgsFeatureSink::Flags flags = QgsFeatureSink::FastInsert )
  {
    checkCanceled( feedback );
    if ( !sink )
      throw QgsProcessingException( QObject::tr( "Output sink is not available" ) );

    if ( !sink->addFeature( feature, flags ) )
    {
      const QString err = sink->lastError();
      throw QgsProcessingException( err.isEmpty()
                                      ? QObject::tr( "Could not write feature to output (disk full, geometry mismatch, or format limit)" )
                                      : QObject::tr( "Could not write feature to output: %1" ).arg( err ) );
    }
  }

  /**
   * Flushes buffered features before the algorithm reports success. Sinks
   * buffer internally, and the silent flush in the sink destructor cannot
   * fail the run — an unchecked flush at success time loses features while
   * reporting success (#1043).
   */
  inline void flushSinkChecked( QgsFeatureSink *sink )
  {
    if ( !sink )
      return;

    if ( !sink->flushBuffer() )
    {
      const QString err = sink->lastError();
      throw QgsProcessingException( err.isEmpty()
                                      ? QObject::tr( "Could not flush features to output" )
                                      : QObject::tr( "Could not flush features to output: %1" ).arg( err ) );
    }
  }

  /**
   * RAII guard that removes destination files unless disarm()ed.
   *
   * Arm it right after the algorithm has (re)created the destination — once
   * the previous content is gone, any later failure or cancellation must not
   * leave a truncated-but-valid-looking file at a user-specified path
   * (TaskCenter only unlinks outputs under scratch roots). Declare the guard
   * BEFORE the sink/dataset handle that owns the destination so destruction
   * order closes the writer first and only then removes the file (required on
   * Windows, where an open handle blocks deletion).
   */
  class PartialOutputGuard
  {
    public:
      PartialOutputGuard() = default;
      explicit PartialOutputGuard( QString path ) { arm( std::move( path ) ); }

      PartialOutputGuard( const PartialOutputGuard &other ) = delete;
      PartialOutputGuard &operator=( const PartialOutputGuard &other ) = delete;

      /**
       * Arms cleanup for \a path. Non-file destinations are skipped: memory
       * sinks resolve \a destination to a layer id (no '/' separator, no file
       * on disk) and have nothing to unlink.
       */
      void arm( QString path )
      {
        if ( path.isEmpty() || path.startsWith( QLatin1String( "memory:" ) ) )
          return;
        const bool looksLikePath = path.contains( '/' ) || path.contains( '\\' );
        if ( !looksLikePath && !QFileInfo::exists( path ) )
          return;

        mPaths.append( std::move( path ) );
        mArmed = true;
      }

      void disarm() { mArmed = false; }

      ~PartialOutputGuard()
      {
        if ( !mArmed )
          return;

        for ( const QString &path : std::as_const( mPaths ) )
          removeOutputFile( path );
      }

    private:
      /**
       * Removes \a path plus the sidecar files GDAL/OGR create next to it, so
       * a half-written GPKG (WAL/journal) or shapefile (dbf/shx/...) is not
       * left behind as a companion "valid" dataset.
       */
      static void removeOutputFile( const QString &path )
      {
        if ( path.startsWith( QLatin1String( "/vsi" ) ) )
        {
          if ( VSIUnlink( path.toUtf8().constData() ) != 0 )
            qWarning( "Could not remove partial output after a failed run: %s", qUtf8Printable( path ) );
          return;
        }

        QStringList paths{ path, path + QLatin1String( "-wal" ),
                           path + QLatin1String( "-shm" ), path + QLatin1String( "-journal" ) };
        if ( path.endsWith( QLatin1String( ".shp" ), Qt::CaseInsensitive ) )
        {
          const QString base = path.left( path.size() - 4 );
          paths << base + QLatin1String( ".dbf" ) << base + QLatin1String( ".shx" )
                << base + QLatin1String( ".prj" ) << base + QLatin1String( ".cpg" )
                << base + QLatin1String( ".qix" );
        }

        for ( const QString &candidate : std::as_const( paths ) )
        {
          const QFileInfo info( candidate );
          if ( info.exists() && info.isFile() && !QFile::remove( candidate ) )
            qWarning( "Could not remove partial output after a failed run: %s", qUtf8Printable( candidate ) );
        }
      }

      QStringList mPaths;
      bool mArmed = false;
  };

} // namespace sicnu::qgis_algorithms
