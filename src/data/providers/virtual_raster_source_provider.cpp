#include "virtual_raster_source_provider.h"

#include <cmath>

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTextStream>

#include <cpl_conv.h>
#include <gdal.h>

#include "../data_asset.h"
#include "../virtual_raster_recipe.h"
#include "gdal_runtime.h"

namespace sicnu::data::providers
{

namespace
{

Diagnostic diagnostic( const QString &code, const QString &message )
{
  return Diagnostic{ code, message, DiagnosticSeverity::Error };
}

QString resamplingName( ResamplingMethod method )
{
  switch ( method )
  {
    case ResamplingMethod::NearestNeighbour:
      return QStringLiteral( "NearestNeighbour" );
    case ResamplingMethod::Bilinear:
      return QStringLiteral( "Bilinear" );
    case ResamplingMethod::Cubic:
      return QStringLiteral( "Cubic" );
  }
  Q_UNREACHABLE();
}

QString escapeXml( const QString &text )
{
  QString escaped = text;
  escaped.replace( QLatin1Char( '&' ), QStringLiteral( "&amp;" ) );
  escaped.replace( QLatin1Char( '<' ), QStringLiteral( "&lt;" ) );
  escaped.replace( QLatin1Char( '>' ), QStringLiteral( "&gt;" ) );
  return escaped;
}

} // namespace

QString buildVirtualRasterXml( const VirtualRasterRecipe &recipe,
                               const QVector<AssetSnapshot> &inputSnapshots )
{
  if ( recipe.inputs.isEmpty() ||
       inputSnapshots.size() != recipe.inputs.size() )
    return QString();

  QVector<const RasterStructure *> inputs;
  inputs.reserve( inputSnapshots.size() );
  for ( const AssetSnapshot &snapshot : inputSnapshots )
  {
    const auto *raster = std::get_if<RasterStructure>( &snapshot.structure() );
    if ( !raster )
      return QString();
    inputs.append( raster );
  }

  // Target CRS: explicit recipe target, else the first input's.
  const QString targetCrs =
    !recipe.targetCrs.isEmpty() ? recipe.targetCrs : inputs.first()->crsWkt;

  // Target resolution: explicit recipe target, else the first input's.
  double resX = recipe.targetResolutionX;
  double resY = recipe.targetResolutionY;
  if ( resX <= 0.0 )
    resX = std::abs( inputs.first()->geoTransform[1] );
  if ( resY <= 0.0 )
    resY = std::abs( inputs.first()->geoTransform[5] );
  if ( resX <= 0.0 || resY <= 0.0 )
    return QString();

  // Target extent: intersection (default) or union of the input extents.
  bool haveExtent = false;
  SpatialExtent extent;
  for ( const RasterStructure *raster : inputs )
  {
    if ( !raster->hasGeoTransform || !raster->extent.valid )
      continue;
    if ( !haveExtent )
    {
      extent = raster->extent;
      haveExtent = true;
    }
    else if ( recipe.extentPolicy == ExtentPolicy::Intersection )
    {
      extent.minimumX = std::max( extent.minimumX, raster->extent.minimumX );
      extent.minimumY = std::max( extent.minimumY, raster->extent.minimumY );
      extent.maximumX = std::min( extent.maximumX, raster->extent.maximumX );
      extent.maximumY = std::min( extent.maximumY, raster->extent.maximumY );
    }
    else
    {
      extent.minimumX = std::min( extent.minimumX, raster->extent.minimumX );
      extent.minimumY = std::min( extent.minimumY, raster->extent.minimumY );
      extent.maximumX = std::max( extent.maximumX, raster->extent.maximumX );
      extent.maximumY = std::max( extent.maximumY, raster->extent.maximumY );
    }
  }
  if ( !haveExtent ||
       extent.maximumX <= extent.minimumX ||
       extent.maximumY <= extent.minimumY )
    return QString();

  const int width =
    std::max( 1, static_cast<int>( std::llround(
                   ( extent.maximumX - extent.minimumX ) / resX ) ) );
  const int height =
    std::max( 1, static_cast<int>( std::llround(
                   ( extent.maximumY - extent.minimumY ) / resY ) ) );

  QString xml;
  QTextStream out( &xml );
  out << QStringLiteral( "<VRTDataset rasterXSize=\"%1\" rasterYSize=\"%2\">\n" )
           .arg( width )
           .arg( height );
  if ( !targetCrs.isEmpty() )
    out << QStringLiteral( "  <SRS>%1</SRS>\n" ).arg( escapeXml( targetCrs ) );
  out << QStringLiteral(
           "  <GeoTransform>%1, %2, 0, %3, 0, %4</GeoTransform>\n" )
           .arg( extent.minimumX )
           .arg( resX )
           .arg( extent.maximumY )
           .arg( -resY );

  for ( int i = 0; i < recipe.inputs.size(); ++i )
  {
    const RasterStructure *raster = inputs[i];
    const AssetSnapshot &snapshot = inputSnapshots[i];
    const BandRef &bandRef = recipe.inputs[i];

    // The recipe records input bands but not an output type; the VRT band type
    // follows the referenced input band's native type (Byte, Int16, Float32,
    // ...) so the virtual asset preserves identity instead of silently coercing
    // every band to Float32.
    QString bandDataType = QStringLiteral( "Float32" );
    for ( const RasterBandStructure &band : raster->bands )
    {
      if ( band.number == bandRef.bandNumber && !band.dataType.isEmpty() )
      {
        bandDataType = band.dataType;
        break;
      }
    }

    out << QStringLiteral(
             "  <VRTRasterBand dataType=\"%1\" band=\"%2\">\n" )
             .arg( escapeXml( bandDataType ) )
             .arg( i + 1 );
    if ( recipe.noDataPolicy == NoDataPolicy::FillValue )
      out << QStringLiteral( "    <NoDataValue>%1</NoDataValue>\n" )
               .arg( recipe.noDataFillValue );
    out << QStringLiteral(
             "    <SimpleSource resampling=\"%1\">\n" )
             .arg( resamplingName( recipe.resampling ) );
    out << QStringLiteral(
             "      <SourceFilename relativeToVRT=\"0\">%1</SourceFilename>\n" )
             .arg( escapeXml( snapshot.source().canonicalSource ) );
    out << QStringLiteral( "      <SourceBand>%1</SourceBand>\n" )
             .arg( bandRef.bandNumber );

    if ( raster->hasGeoTransform && raster->extent.valid )
    {
      // Map the output-extent region covered by this input into source and
      // destination pixel rectangles (same-CRS resampling).
      const double oMinX = std::max( extent.minimumX, raster->extent.minimumX );
      const double oMinY = std::max( extent.minimumY, raster->extent.minimumY );
      const double oMaxX = std::min( extent.maximumX, raster->extent.maximumX );
      const double oMaxY = std::min( extent.maximumY, raster->extent.maximumY );
      if ( oMaxX > oMinX && oMaxY > oMinY )
      {
        const double inResX = std::abs( raster->geoTransform[1] );
        const double inResY = std::abs( raster->geoTransform[5] );
        out << QStringLiteral(
                 "      <SrcRect xOff=\"%1\" yOff=\"%2\" xSize=\"%3\" ySize=\"%4\"/>\n" )
                 .arg( ( oMinX - raster->geoTransform[0] ) / inResX )
                 .arg( ( raster->geoTransform[3] - oMaxY ) / inResY )
                 .arg( ( oMaxX - oMinX ) / inResX )
                 .arg( ( oMaxY - oMinY ) / inResY );
        out << QStringLiteral(
                 "      <DstRect xOff=\"%1\" yOff=\"%2\" xSize=\"%3\" ySize=\"%4\"/>\n" )
                 .arg( ( oMinX - extent.minimumX ) / resX )
                 .arg( ( extent.maximumY - oMaxY ) / resY )
                 .arg( ( oMaxX - oMinX ) / resX )
                 .arg( ( oMaxY - oMinY ) / resY );
      }
    }

    out << QStringLiteral( "    </SimpleSource>\n" );
    out << QStringLiteral( "  </VRTRasterBand>\n" );
  }

  out << QStringLiteral( "</VRTDataset>\n" );
  return xml;
}

VirtualRasterSourceProvider::VirtualRasterSourceProvider( AssetLookup assetLookup )
  : m_assetLookup( std::move( assetLookup ) )
{
}

bool VirtualRasterSourceProvider::supports( const SourceDescriptor &source ) const
{
  return source.providerKey == QStringLiteral( "vrt" );
}

Result<internal::ResolvedSource> VirtualRasterSourceProvider::resolve(
  const SourceDescriptor &source ) const
{
  // The recipe travels in the descriptor (internal seam; never exposed to
  // callers of the DataManager interface).
  const QString recipeText = source.dataOptions.value( QStringLiteral( "recipe" ) );
  const QJsonDocument recipeDoc =
    QJsonDocument::fromJson( recipeText.toUtf8() );
  if ( recipeDoc.isNull() || !recipeDoc.isObject() )
  {
    return Result<internal::ResolvedSource>::failure(
      diagnostic( QStringLiteral( "virtual_raster.recipe_invalid" ),
                  QStringLiteral( "The virtual raster descriptor carries no valid "
                                  "recipe JSON" ) ) );
  }
  const Result<VirtualRasterRecipe> recipeResult =
    VirtualRasterRecipe::fromJson( recipeDoc.object() );
  if ( !recipeResult )
  {
    return Result<internal::ResolvedSource>::failure(
      diagnostic( QStringLiteral( "virtual_raster.recipe_invalid" ),
                  QStringLiteral( "The virtual raster descriptor carries no valid "
                                  "recipe" ) ) );
  }
  const VirtualRasterRecipe &recipe = recipeResult.value();

  QVector<AssetSnapshot> inputSnapshots;
  inputSnapshots.reserve( recipe.inputs.size() );
  for ( const BandRef &bandRef : recipe.inputs )
  {
    const std::optional<AssetSnapshot> snapshot = m_assetLookup( bandRef.asset );
    if ( !snapshot.has_value() )
    {
      return Result<internal::ResolvedSource>::failure(
        diagnostic( QStringLiteral( "virtual_raster.input_unavailable" ),
                    QStringLiteral( "A virtual raster input asset is no longer "
                                    "registered" ) ) );
    }
    inputSnapshots.append( *snapshot );
  }

  const QString xml = buildVirtualRasterXml( recipe, inputSnapshots );
  if ( xml.isEmpty() )
  {
    return Result<internal::ResolvedSource>::failure(
      diagnostic( QStringLiteral( "virtual_raster.grid_invalid" ),
                  QStringLiteral( "The virtual raster target grid could not be "
                                  "derived from the recipe and its inputs" ) ) );
  }

  // Write the artifact at the managed scratch path and resolve structure from
  // it. The path is deterministic per recipe (the DataManager derives it from
  // the recipe hash), so re-resolution regenerates in place.
  QFile file( source.canonicalSource );
  if ( !file.open( QIODevice::WriteOnly | QIODevice::Text ) )
  {
    return Result<internal::ResolvedSource>::failure(
      diagnostic( QStringLiteral( "virtual_raster.write_failed" ),
                  QStringLiteral( "Could not write the virtual raster artifact to "
                                  "%1" ).arg( source.canonicalSource ) ) );
  }
  file.write( xml.toUtf8() );
  file.close();

  ensureGdalRuntime();
  CPLPushErrorHandler( CPLQuietErrorHandler );
  GDALDatasetH dataset =
    GDALOpen( source.canonicalSource.toUtf8().constData(), GA_ReadOnly );
  CPLPopErrorHandler();
  if ( dataset == nullptr )
  {
    return Result<internal::ResolvedSource>::failure(
      diagnostic( QStringLiteral( "virtual_raster.unreadable" ),
                  QStringLiteral( "GDAL could not open the generated virtual "
                                  "raster" ) ) );
  }

  internal::ResolvedSource resolved;
  resolved.kind = AssetKind::VirtualRaster;
  resolved.state = AssetState::Ready;
  resolved.storageKind = StorageKind::File;
  // Per spec, the closed capability set for a virtual raster is
  // Renderable | ReadablePixels. Statistics/histogram are explicitly a deferred
  // derived-cache item, so we do not advertise them for the managed VRT.
  resolved.capabilities = AssetCapability::Renderable |
                          AssetCapability::ReadablePixels;
  resolved.canonicalSource = source.canonicalSource;
  resolved.canonicalProviderKey = QStringLiteral( "vrt" );
  resolved.displayName = QStringLiteral( "VRT(%1 bands)" ).arg( recipe.inputs.size() );

  RasterStructure structure;
  if ( GDALDriverH driver = GDALGetDatasetDriver( dataset ) )
    structure.driverName = QString::fromUtf8( GDALGetDriverShortName( driver ) );
  structure.width = GDALGetRasterXSize( dataset );
  structure.height = GDALGetRasterYSize( dataset );
  structure.bandCount = GDALGetRasterCount( dataset );
  structure.crsWkt = QString::fromUtf8( GDALGetProjectionRef( dataset ) );
  structure.hasGeoTransform =
    GDALGetGeoTransform( dataset, structure.geoTransform.data() ) == CE_None;
  if ( structure.hasGeoTransform )
  {
    const auto &gt = structure.geoTransform;
    const auto corner = [&]( double px, double py ) {
      return std::array<double, 2>{ gt[0] + px * gt[1] + py * gt[2],
                                    gt[3] + px * gt[4] + py * gt[5] };
    };
    const std::array<std::array<double, 2>, 4> corners{
      corner( 0.0, 0.0 ), corner( structure.width, 0.0 ),
      corner( 0.0, structure.height ),
      corner( structure.width, structure.height ) };
    structure.extent.minimumX = corners.front()[0];
    structure.extent.maximumX = corners.front()[0];
    structure.extent.minimumY = corners.front()[1];
    structure.extent.maximumY = corners.front()[1];
    for ( const auto &point : corners )
    {
      structure.extent.minimumX = std::min( structure.extent.minimumX, point[0] );
      structure.extent.maximumX = std::max( structure.extent.maximumX, point[0] );
      structure.extent.minimumY = std::min( structure.extent.minimumY, point[1] );
      structure.extent.maximumY = std::max( structure.extent.maximumY, point[1] );
    }
    structure.extent.valid = true;
  }

  structure.bands.reserve( structure.bandCount );
  for ( int bandNumber = 1; bandNumber <= structure.bandCount; ++bandNumber )
  {
    GDALRasterBandH band = GDALGetRasterBand( dataset, bandNumber );
    if ( band == nullptr )
      continue;
    RasterBandStructure bandStructure;
    bandStructure.number = bandNumber;
    bandStructure.dataType =
      QString::fromUtf8( GDALGetDataTypeName( GDALGetRasterDataType( band ) ) );
    int hasNoData = 0;
    const double noData = GDALGetRasterNoDataValue( band, &hasNoData );
    if ( hasNoData )
      bandStructure.noDataValue = noData;
    bandStructure.colorInterpretation = QString::fromUtf8(
      GDALGetColorInterpretationName( GDALGetRasterColorInterpretation( band ) ) );
    structure.bands.append( std::move( bandStructure ) );
  }
  resolved.structure = std::move( structure );

  GDALClose( dataset );
  return Result<internal::ResolvedSource>::success( std::move( resolved ) );
}

} // namespace sicnu::data::providers
