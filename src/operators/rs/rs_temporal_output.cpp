// src/operators/rs/rs_temporal_output.cpp
#include "rs_temporal_output.h"

#include <QFile>

namespace sicnu::operators::rs::temporal_output
{

void writeTemporalDatasetMetadata( const GdalDatasetWrapper &out,
                                   const sicnu::temporal::TemporalCollection &collection,
                                   const QString &algorithmId,
                                   const QString &parameterSummary )
{
  if ( !out.isValid() )
    return;
  GDALDatasetH h = static_cast<GDALDatasetH>( out.dataset() );
  GDALSetMetadataItem( h, "SICNU_TEMPORAL_ALGORITHM", algorithmId.toUtf8().constData(), nullptr );
  GDALSetMetadataItem( h, "SICNU_TEMPORAL_SCENE_COUNT",
                       QByteArray::number( collection.sceneCount() ).constData(), nullptr );
  const QString start = collection.timeRangeStartIso();
  const QString end = collection.timeRangeEndIso();
  if ( !start.isEmpty() )
    GDALSetMetadataItem( h, "SICNU_TEMPORAL_TIME_START", start.toUtf8().constData(), nullptr );
  if ( !end.isEmpty() )
    GDALSetMetadataItem( h, "SICNU_TEMPORAL_TIME_END", end.toUtf8().constData(), nullptr );
  if ( !parameterSummary.isEmpty() )
    GDALSetMetadataItem( h, "SICNU_TEMPORAL_PARAMETERS",
                         parameterSummary.left( 1024 ).toUtf8().constData(), nullptr );

  // Deterministic scene manifest (chronological, ";"-separated paths).
  QString scenes;
  const auto &list = collection.scenes();
  for ( int i = 0; i < list.size(); ++i )
  {
    if ( i > 0 )
      scenes += QLatin1Char( '\n' );
    scenes += list.at( i ).path;
  }
  GDALSetMetadataItem( h, "SICNU_TEMPORAL_SCENES",
                       scenes.left( 32000 ).toUtf8().constData(), nullptr );
}

void writeBandAcquisitionMetadata( const GdalDatasetWrapper &out, int band,
                                   const sicnu::temporal::TemporalSceneRef &scene,
                                   const QString &bandLabel )
{
  if ( !out.isValid() )
    return;
  GDALDatasetH h = static_cast<GDALDatasetH>( out.dataset() );
  GDALRasterBandH rasterBand = GDALGetRasterBand( h, band );
  if ( !rasterBand )
    return;
  if ( scene.time.valid )
    GDALSetMetadataItem( rasterBand, "SICNU_ACQUISITION_DATE",
                         scene.time.iso.toUtf8().constData(), nullptr );
  GDALSetMetadataItem( rasterBand, "SICNU_TEMPORAL_SCENE",
                       scene.path.toUtf8().constData(), nullptr );
  const QString desc = bandLabel + ( scene.time.valid ? ( QStringLiteral( " " ) + scene.time.dateString() )
                                                      : QString() );
  GDALSetDescription( rasterBand, desc.toUtf8().constData() );
}

} // namespace sicnu::operators::rs::temporal_output
