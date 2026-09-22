// rs_edit_persistence.cpp — see rs_edit_persistence.h.
#include "rs_edit_persistence.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>

#include <qgsvectorfilewriter.h>
#include <qgsvectorlayer.h>

#include "geospatial/util/atomic_fs.h"

namespace
{

QString driverForSuffix( const QString &path, bool *known )
{
    *known = true;
    const QString suffix = QFileInfo( path ).suffix().toLower();
    if ( suffix == QLatin1String( "gpkg" ) )
        return QStringLiteral( "GPKG" );
    if ( suffix == QLatin1String( "geojson" ) )
        return QStringLiteral( "GeoJSON" );
    *known = false;
    return QString();
}

/// Best-effort temp cleanup used on every failure path.
void removeTemp( const QString &tempPath )
{
    QFile::remove( tempPath );
}

QString writerErrorName( QgsVectorFileWriter::WriterError error )
{
    switch ( error )
    {
        case QgsVectorFileWriter::NoError:
            return QStringLiteral( "no error" );
        case QgsVectorFileWriter::ErrDriverNotFound:
            return QStringLiteral( "driver not found" );
        case QgsVectorFileWriter::ErrCreateDataSource:
            return QStringLiteral( "cannot create the target data source" );
        case QgsVectorFileWriter::ErrCreateLayer:
            return QStringLiteral( "cannot create the output layer" );
        case QgsVectorFileWriter::ErrAttributeTypeUnsupported:
            return QStringLiteral( "unsupported attribute type" );
        case QgsVectorFileWriter::ErrAttributeCreationFailed:
            return QStringLiteral( "attribute creation failed" );
        case QgsVectorFileWriter::ErrProjection:
            return QStringLiteral( "projection error" );
        case QgsVectorFileWriter::ErrFeatureWriteFailed:
            return QStringLiteral( "feature write failed" );
        case QgsVectorFileWriter::ErrInvalidLayer:
            return QStringLiteral( "invalid layer" );
        case QgsVectorFileWriter::ErrSavingMetadata:
            return QStringLiteral( "metadata saving failed" );
        case QgsVectorFileWriter::Canceled:
            return QStringLiteral( "export canceled" );
        default:
            return QStringLiteral( "writer error %1" ).arg( static_cast<int>( error ) );
    }
}

} // namespace

RsEditPersistence::ExportResult RsEditPersistence::exportLayer( QgsVectorLayer *layer,
                                                                const QString &targetPath,
                                                                const QString &layerName )
{
    ExportResult result;
    if ( !layer || !layer->isValid() )
    {
        result.error = QStringLiteral( "invalid or null source layer" );
        return result;
    }
    if ( targetPath.trimmed().isEmpty() )
    {
        result.error = QStringLiteral( "empty target path" );
        return result;
    }

    bool knownDriver = false;
    const QString driver = driverForSuffix( targetPath, &knownDriver );
    if ( !knownDriver )
    {
        result.error = QStringLiteral( "unsupported export format (use .gpkg or .geojson)" );
        return result;
    }

    // Atomic write: temp file in the target's directory (same filesystem),
    // then rename over the target only after the writer succeeded.
    const QFileInfo targetInfo( targetPath );
    const QString tempPath = QStringLiteral( "%1/%2.tmp-%3-%4" )
                               .arg( targetInfo.absolutePath(),
                                     targetInfo.fileName() )
                               .arg( QCoreApplication::applicationPid() )
                               .arg( QDateTime::currentMSecsSinceEpoch() );

    QgsVectorFileWriter::SaveVectorOptions options;
    options.driverName = driver;
    options.layerName = layerName;
    options.fileEncoding = QStringLiteral( "UTF-8" );
    options.actionOnExistingFile = QgsVectorFileWriter::CreateOrOverwriteFile;

    QString errorMessage;
    QString writtenName;
    const QgsVectorFileWriter::WriterError writeResult = QgsVectorFileWriter::writeAsVectorFormatV3(
      layer, tempPath, layer->transformContext(), options, &errorMessage, &writtenName );
    // Some drivers (e.g. GeoJSON) adjust the output file name — the writer
    // reports where it actually wrote through newFilename.
    const QString written = writtenName.isEmpty() ? tempPath : writtenName;

    if ( writeResult != QgsVectorFileWriter::NoError )
    {
        removeTemp( tempPath );
        removeTemp( written );
        result.error = QStringLiteral( "export failed (%1): %2" )
                       .arg( writerErrorName( writeResult ), errorMessage );
        return result;
    }

    if ( !QFile::exists( written ) )
    {
        removeTemp( tempPath );
        result.error = QStringLiteral( "writer reported success but produced no file" );
        return result;
    }

    // #1178: publish via atomic_fs (ReplaceFileW / MoveFileExW). Never
    // remove-then-rename — a locked target must fail closed.
    try
    {
        sicnu::geo::atomic_fs::publishStagedFile( written.toStdString(),
                                                  targetPath.toStdString() );
    }
    catch ( const sicnu::geo::GeoError &ex )
    {
        removeTemp( tempPath );
        removeTemp( written );
        result.error = QStringLiteral( "could not publish target file: %1" )
                         .arg( QString::fromUtf8( ex.what() ) );
        return result;
    }

    result.ok = true;
    result.writtenPath = targetPath;
    return result;
}

RsEditPersistence::ExportResult RsEditPersistence::exportLayerToGeoJson( QgsVectorLayer *layer,
                                                                         const QString &targetPath,
                                                                         const QString &layerName )
{
    return exportLayer( layer, targetPath, layerName );
}

RsEditPersistence::ExportResult RsEditPersistence::exportLayerToGpkg( QgsVectorLayer *layer,
                                                                      const QString &targetPath,
                                                                      const QString &layerName )
{
    return exportLayer( layer, targetPath, layerName );
}

int RsEditPersistence::commitReported( const QList<QgsVectorLayer *> &layers, QStringList *errors )
{
    int committed = 0;
    for ( QgsVectorLayer *layer : layers )
    {
        if ( !layer || !layer->isEditable() || !layer->isModified() )
            continue;
        if ( layer->commitChanges() )
        {
            ++committed;
        }
        else
        {
            QStringList layerErrors = layer->commitErrors();
            if ( layerErrors.isEmpty() )
                layerErrors << QStringLiteral( "commit rejected without a reported reason" );
            if ( errors )
                *errors << QStringLiteral( "%1: %2" )
                               .arg( layer->name(), layerErrors.join( QLatin1String( "; " ) ) );
        }
    }
    return committed;
}
