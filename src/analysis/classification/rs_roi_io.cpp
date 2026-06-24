// rs_roi_io.cpp — Phase 10A: ESRI Shapefile (via QgsVectorFileWriter) + sidecar JSON
#include "rs_roi_io.h"
#include "rs_roi_collection.h"
#include "rs_class_def.h"
#include "rs_roi.h"

#include "qgsvectorfilewriter.h"
#include "qgsvectorlayer.h"
#include "qgsfeature.h"
#include "qgsfeatureiterator.h"
#include "qgsfields.h"
#include "qgsfield.h"
#include "qgscoordinatereferencesystem.h"
#include "qgscoordinatetransformcontext.h"
#include "qgscoordinatetransform.h"
#include "qgsproject.h"
#include "qgis.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariant>

namespace
{
  QString sidecarPath( const QString &shp )
  {
    QFileInfo fi( shp );
    return fi.absolutePath() + QStringLiteral( "/" ) + fi.completeBaseName() + QStringLiteral( ".classes.json" );
  }

  bool writeSidecar( const QString &shp, const RsRoiCollection &col )
  {
    QJsonArray arr;
    const QHash<int, RsClassDef> defs = col.classDefs();
    // Sort by id for stable on-disk order.
    QList<int> ids = defs.keys();
    std::sort( ids.begin(), ids.end() );
    for ( int id : ids )
    {
      const RsClassDef &d = defs.value( id );
      QJsonObject o;
      o[QStringLiteral( "id" )] = d.id();
      o[QStringLiteral( "name" )] = d.name();
      o[QStringLiteral( "color" )] = d.color().name();
      arr.append( o );
    }
    QJsonObject root;
    root[QStringLiteral( "version" )] = 1;
    root[QStringLiteral( "classes" )] = arr;

    QFile f( sidecarPath( shp ) );
    if ( !f.open( QIODevice::WriteOnly | QIODevice::Truncate ) ) {
      qWarning() << "RsRoiIO: sidecar write failed for" << sidecarPath( shp );
      return false;
    }
    f.write( QJsonDocument( root ).toJson() );
    return true;
  }

  bool readSidecar( const QString &shp, RsRoiCollection &col )
  {
    QFile f( sidecarPath( shp ) );
    if ( !f.exists() )
      return true; // missing sidecar is OK; classes stay empty
    if ( !f.open( QIODevice::ReadOnly ) ) {
      qWarning() << "RsRoiIO: sidecar read failed for" << sidecarPath( shp );
      return false;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson( f.readAll(), &err );
    if ( err.error != QJsonParseError::NoError || !doc.isObject() ) {
      qWarning() << "RsRoiIO: sidecar parse error:" << err.errorString();
      return false;
    }
    const QJsonArray arr = doc.object().value( QStringLiteral( "classes" ) ).toArray();
    for ( const QJsonValue &v : arr )
    {
      const QJsonObject o = v.toObject();
      col.setClassDef( RsClassDef(
        o.value( QStringLiteral( "id" ) ).toInt(),
        o.value( QStringLiteral( "name" ) ).toString(),
        QColor( o.value( QStringLiteral( "color" ) ).toString() )
      ) );
    }
    return true;
  }
} // namespace

bool RsRoiIO::save( const QString &shp, const RsRoiCollection &col, const QgsCoordinateReferenceSystem &crs )
{
  // Field schema — `cls_id` chosen to avoid OGR reserved keyword risk (spec §4.7).
  QgsFields fields;
  fields.append( QgsField( QStringLiteral( "cls_id" ), QMetaType::Type::Int ) );
  fields.append( QgsField( QStringLiteral( "px_count" ), QMetaType::Type::LongLong ) );

  QgsVectorFileWriter::SaveVectorOptions opts;
  opts.driverName = QStringLiteral( "ESRI Shapefile" );
  opts.fileEncoding = QStringLiteral( "UTF-8" );

  const QgsCoordinateReferenceSystem destCrs( QStringLiteral( "EPSG:4326" ) );

  QgsVectorFileWriter *writer = QgsVectorFileWriter::create(
    shp, fields, Qgis::WkbType::Polygon, destCrs,
    QgsCoordinateTransformContext(), opts );

  if ( !writer )
    return false;
  if ( writer->hasError() != QgsVectorFileWriter::NoError )
  {
    delete writer;
    return false;
  }

  QgsCoordinateTransform trans;
  bool doTransform = false;
  if ( crs.isValid() && crs != destCrs )
  {
    trans = QgsCoordinateTransform( crs, destCrs, QgsProject::instance() );
    doTransform = true;
  }

  for ( int i = 0; i < col.size(); ++i )
  {
    const RsRoi &roi = col.at( i );
    QgsFeature feat( fields );
    QgsGeometry geom = roi.geometry();
    if ( doTransform )
    {
      geom.transform( trans );
    }
    feat.setGeometry( geom );
    feat.setAttribute( QStringLiteral( "cls_id" ), roi.classId() );
    feat.setAttribute( QStringLiteral( "px_count" ),
                       static_cast<qint64>( roi.pixelIndices().size() ) );
    if ( !writer->addFeature( feat ) )
    {
      delete writer;
      return false;
    }
  }
  delete writer; // flushes the file

  return writeSidecar( shp, col );
}

bool RsRoiIO::load( const QString &shp, RsRoiCollection &col, const QgsCoordinateReferenceSystem &crs )
{
  if ( !QFile::exists( shp ) )
    return false;

  QgsVectorLayer layer( shp, QStringLiteral( "rois" ), QStringLiteral( "ogr" ) );
  if ( !layer.isValid() )
    return false;

  // Restore class defs first (sidecar is best-effort — missing is OK).
  if ( !readSidecar( shp, col ) )
    return false;

  const QgsCoordinateReferenceSystem srcCrs( QStringLiteral( "EPSG:4326" ) );
  QgsCoordinateTransform trans;
  bool doTransform = false;
  if ( crs.isValid() && crs != srcCrs )
  {
    trans = QgsCoordinateTransform( srcCrs, crs, QgsProject::instance() );
    doTransform = true;
  }

  QgsFeature feat;
  QgsFeatureIterator it = layer.getFeatures();
  while ( it.nextFeature( feat ) )
  {
    const int clsId = feat.attribute( QStringLiteral( "cls_id" ) ).toInt();
    QgsGeometry geom = feat.geometry();
    if ( doTransform )
    {
      geom.transform( trans );
    }
    // Pixel indices are NOT persisted — caller must recompute against current raster.
    col.appendRoi( RsRoi( clsId, geom, QVector<quint64>() ) );
  }
  return true;
}
