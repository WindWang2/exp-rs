// rs_edit_persistence.h — F11 Package G: persistence / interchange helpers.
//
// Contract:
//   * Exports go through QgsVectorFileWriter into a temp file
//     "<target>.tmp-<pid>" that atomically renames over the target on
//     success; every failure path deletes the temp file. QString paths
//     throughout (Unicode-safe). The source layer is never modified.
//   * Driver is selected by suffix: ".gpkg" → GPKG, ".geojson" → GeoJSON.
//     Anything else is refused (no silent default driver).
//   * commitReported() wraps layer commit with full error collection (the
//     legacy silent-commit-failure bug class, fixed at the API level).
#pragma once

#include <QString>
#include <QStringList>

class QgsVectorLayer;

class RsEditPersistence
{
  public:
    struct ExportResult
    {
        bool ok = false;
        QString error;        // fail-closed reason when !ok
        QString writtenPath;  // the target path on success
    };

    /// Export the (committed) features of \a layer to \a targetPath.
    /// \a layerName names the output layer inside the file. Overwrites an
    /// existing target only via the atomic rename.
    static ExportResult exportLayer( QgsVectorLayer *layer, const QString &targetPath,
                                     const QString &layerName );

    /// GeoJSON convenience overload.
    static ExportResult exportLayerToGeoJson( QgsVectorLayer *layer, const QString &targetPath,
                                              const QString &layerName );
    /// GeoPackage convenience overload.
    static ExportResult exportLayerToGpkg( QgsVectorLayer *layer, const QString &targetPath,
                                           const QString &layerName );

    /// Commit every dirty attached layer of the session; per-layer failures
    /// are appended to \a errors as "layerName: <error>". Returns the number
    /// of layers committed. (Session-level commitAll already does this; this
    /// entry point exists for callers holding layers without a session.)
    static int commitReported( const QList<QgsVectorLayer *> &layers, QStringList *errors );
};
