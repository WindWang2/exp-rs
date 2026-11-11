#include "virtual_raster_source_provider.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSet>
#include <QTextStream>

#include <cpl_conv.h>
#include <gdal.h>

#include "../data_asset.h"
#include "../raster_grid_compat.h"
#include "../virtual_raster_recipe.h"
#include "gdal_runtime.h"
#include "geospatial/convert/raster_convert.h"
#include "geospatial/crs/crs_policy.h"
#include "platform/durable_sidecar.h"

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
  // Attribute values (SourceFilename paths, dataType) are quoted with '"' -
  // a literal double quote in a POSIX path would otherwise terminate the
  // attribute and corrupt the VRT (#633).
  escaped.replace( QLatin1Char( '"' ), QStringLiteral( "&quot;" ) );
  escaped.replace( QLatin1Char( '\'' ), QStringLiteral( "&apos;" ) );
  return escaped;
}

// Full round-trip precision: QString::arg(double) / QString::number(double)
// default to 6 significant digits, which silently shifted the VRT GeoTransform
// origin by up to ~0.5 m (projected) / ~50 m (geographic) and rounded NoData
// sentinels (e.g. -3.4e38f) so GDAL's exact ComplexSource NODATA compare no
// longer masked them.
QString formatVrtDouble( double value )
{
  if ( !std::isfinite( value ) )
    return QString::number( value, 'g', 17 );
  for ( int precision = 15; precision <= 17; ++precision )
  {
    const QString candidate = QString::number( value, 'g', precision );
    if ( candidate.toDouble() == value )
      return candidate;
  }
  return QString::number( value, 'g', 17 );
}

/// Reads one dataset's RasterStructure (grid, extent bbox over the 4
/// transformed corners, band types/NoData/colorInterpretation). Shared by
/// resolve()'s VRT probe and the WP06 warp stage's intermediate probe.
RasterStructure structureFromDataset( GDALDatasetH dataset )
{
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
  return structure;
}

} // namespace

QString buildVirtualRasterXml( const VirtualRasterRecipe &recipe,
                               const QVector<AssetSnapshot> &inputSnapshots )
{
  if ( recipe.inputs.isEmpty() ||
       inputSnapshots.size() != recipe.inputs.size() )
    return QString();

  QVector<VirtualRasterSourceInput> inputs;
  inputs.reserve( inputSnapshots.size() );
  for ( const AssetSnapshot &snapshot : inputSnapshots )
  {
    const auto *raster = std::get_if<RasterStructure>( &snapshot.structure() );
    if ( !raster )
      return QString();
    inputs.append( { snapshot.source().canonicalSource, *raster } );
  }
  return buildVirtualRasterXml( recipe, inputs, nullptr );
}

QString buildVirtualRasterXml( const VirtualRasterRecipe &recipe,
                               const QVector<VirtualRasterSourceInput> &inputs,
                               const VirtualRasterPinnedGrid *pinnedGrid )
{
  if ( recipe.inputs.isEmpty() || inputs.size() != recipe.inputs.size() )
    return QString();

  // Target CRS: pinned grid, explicit recipe target, else the first input's.
  const QString targetCrs = pinnedGrid   ? pinnedGrid->crs
                            : !recipe.targetCrs.isEmpty() ? recipe.targetCrs
                                                          : inputs.first().structure.crsWkt;

  // Target resolution: pinned grid, explicit recipe target, else the first
  // input's.
  double resX = pinnedGrid ? pinnedGrid->resX : recipe.targetResolutionX;
  double resY = pinnedGrid ? pinnedGrid->resY : recipe.targetResolutionY;
  if ( resX <= 0.0 )
    resX = std::abs( inputs.first().structure.geoTransform[1] );
  if ( resY <= 0.0 )
    resY = std::abs( inputs.first().structure.geoTransform[5] );
  if ( resX <= 0.0 || resY <= 0.0 )
    return QString();

  // Target extent: pinned grid, or intersection (default) / union of the
  // input extents.
  bool haveExtent = false;
  SpatialExtent extent;
  if ( pinnedGrid )
  {
    extent = pinnedGrid->extent;
    haveExtent = extent.valid;
  }
  else
  {
    for ( const VirtualRasterSourceInput &input : inputs )
    {
      const RasterStructure &raster = input.structure;
      if ( !raster.hasGeoTransform || !raster.extent.valid )
        continue;
      if ( !haveExtent )
      {
        extent = raster.extent;
        haveExtent = true;
      }
      else if ( recipe.extentPolicy == ExtentPolicy::Intersection )
      {
        extent.minimumX = std::max( extent.minimumX, raster.extent.minimumX );
        extent.minimumY = std::max( extent.minimumY, raster.extent.minimumY );
        extent.maximumX = std::min( extent.maximumX, raster.extent.maximumX );
        extent.maximumY = std::min( extent.maximumY, raster.extent.maximumY );
      }
      else
      {
        extent.minimumX = std::min( extent.minimumX, raster.extent.minimumX );
        extent.minimumY = std::min( extent.minimumY, raster.extent.minimumY );
        extent.maximumX = std::max( extent.maximumX, raster.extent.maximumX );
        extent.maximumY = std::max( extent.maximumY, raster.extent.maximumY );
      }
    }
  }
  if ( !haveExtent ||
       extent.maximumX <= extent.minimumX ||
       extent.maximumY <= extent.minimumY )
    return QString();

  // Validate the quotient in double before the int cast: a unit-mismatched
  // recipe (e.g. metre extent with a degree target resolution) yields
  // quotients far beyond INT_MAX, where llround -> int wraps into garbage
  // (possibly negative) dimensions (#633). Fail closed via the empty-XML
  // path ("virtual_raster.grid_invalid").
  constexpr double kMaxVrtDimension = 1000000000.0;
  const double widthF = ( extent.maximumX - extent.minimumX ) / resX;
  const double heightF = ( extent.maximumY - extent.minimumY ) / resY;
  if ( widthF <= 0.0 || heightF <= 0.0 ||
       widthF > kMaxVrtDimension || heightF > kMaxVrtDimension )
    return QString();
  const int width =
    std::max( 1, static_cast<int>( std::llround( widthF ) ) );
  const int height =
    std::max( 1, static_cast<int>( std::llround( heightF ) ) );

  QString xml;
  QTextStream out( &xml );
  out << QStringLiteral( "<VRTDataset rasterXSize=\"%1\" rasterYSize=\"%2\">\n" )
           .arg( width )
           .arg( height );
  if ( !targetCrs.isEmpty() )
    out << QStringLiteral( "  <SRS>%1</SRS>\n" ).arg( escapeXml( targetCrs ) );
  out << QStringLiteral(
           "  <GeoTransform>%1, %2, 0, %3, 0, %4</GeoTransform>\n" )
           .arg( formatVrtDouble( extent.minimumX ) )
           .arg( formatVrtDouble( resX ) )
           .arg( formatVrtDouble( extent.maximumY ) )
           .arg( formatVrtDouble( -resY ) );

  for ( int i = 0; i < recipe.inputs.size(); ++i )
  {
    const RasterStructure &raster = inputs[i].structure;
    const BandRef &bandRef = recipe.inputs[i];

    // The recipe records input bands but not an output type; the VRT band type
    // follows the referenced input band's native type (Byte, Int16, Float32,
    // ...) so the virtual asset preserves identity instead of silently coercing
    // every band to Float32.
    QString bandDataType = QStringLiteral( "Float32" );
    for ( const RasterBandStructure &band : raster.bands )
    {
      if ( band.number == bandRef.bandNumber && !band.dataType.isEmpty() )
      {
        bandDataType = band.dataType;
        break;
      }
    }

    std::optional<double> inputNoData;
    for ( const RasterBandStructure &band : raster.bands )
    {
      if ( band.number == bandRef.bandNumber && band.noDataValue.has_value() )
      {
        inputNoData = band.noDataValue;
        break;
      }
    }

    out << QStringLiteral(
             "  <VRTRasterBand dataType=\"%1\" band=\"%2\">\n" )
             .arg( escapeXml( bandDataType ) )
             .arg( i + 1 );
    if ( recipe.noDataPolicy == NoDataPolicy::FillValue )
    {
      out << QStringLiteral( "    <NoDataValue>%1</NoDataValue>\n" )
               .arg( formatVrtDouble( recipe.noDataFillValue ) );
    }
    else if ( inputNoData.has_value() )
    {
      out << QStringLiteral( "    <NoDataValue>%1</NoDataValue>\n" )
               .arg( formatVrtDouble( *inputNoData ) );
    }

    const bool useComplex = ( recipe.noDataPolicy == NoDataPolicy::FillValue ) || inputNoData.has_value();
    const QString sourceType = useComplex ? QStringLiteral( "ComplexSource" ) : QStringLiteral( "SimpleSource" );

    out << QStringLiteral(
             "    <%1 resampling=\"%2\">\n" )
             .arg( sourceType )
             .arg( resamplingName( recipe.resampling ) );
    out << QStringLiteral(
             "      <SourceFilename relativeToVRT=\"0\">%1</SourceFilename>\n" )
             .arg( escapeXml( inputs[i].sourcePath ) );
    out << QStringLiteral( "      <SourceBand>%1</SourceBand>\n" )
             .arg( bandRef.bandNumber );
    if ( recipe.noDataPolicy == NoDataPolicy::FillValue )
    {
      out << QStringLiteral( "      <NODATA>%1</NODATA>\n" ).arg( formatVrtDouble( recipe.noDataFillValue ) );
    }
    else if ( inputNoData.has_value() )
    {
      out << QStringLiteral( "      <NODATA>%1</NODATA>\n" ).arg( formatVrtDouble( *inputNoData ) );
    }

    if ( raster.hasGeoTransform && raster.extent.valid )
    {
      // Map the output-extent region covered by this input into source and
      // destination pixel rectangles (same-CRS resampling).
      const double oMinX = std::max( extent.minimumX, raster.extent.minimumX );
      const double oMinY = std::max( extent.minimumY, raster.extent.minimumY );
      const double oMaxX = std::min( extent.maximumX, raster.extent.maximumX );
      const double oMaxY = std::min( extent.maximumY, raster.extent.maximumY );
      if ( oMaxX > oMinX && oMaxY > oMinY )
      {
        double invGT[6];
        double gt[6] = { raster.geoTransform[0], raster.geoTransform[1], raster.geoTransform[2],
                         raster.geoTransform[3], raster.geoTransform[4], raster.geoTransform[5] };
        if ( GDALInvGeoTransform( gt, invGT ) )
        {
          double srcX0, srcY0, srcX1, srcY1;
          GDALApplyGeoTransform( invGT, oMinX, oMaxY, &srcX0, &srcY0 );
          GDALApplyGeoTransform( invGT, oMaxX, oMinY, &srcX1, &srcY1 );
          const double srcXMin = std::min( srcX0, srcX1 );
          const double srcYMin = std::min( srcY0, srcY1 );
          const double srcXMax = std::max( srcX0, srcX1 );
          const double srcYMax = std::max( srcY0, srcY1 );

          out << QStringLiteral(
                   "      <SrcRect xOff=\"%1\" yOff=\"%2\" xSize=\"%3\" ySize=\"%4\"/>\n" )
                   .arg( formatVrtDouble( srcXMin ) )
                   .arg( formatVrtDouble( srcYMin ) )
                   .arg( formatVrtDouble( srcXMax - srcXMin ) )
                   .arg( formatVrtDouble( srcYMax - srcYMin ) );
          out << QStringLiteral(
                   "      <DstRect xOff=\"%1\" yOff=\"%2\" xSize=\"%3\" ySize=\"%4\"/>\n" )
                   .arg( formatVrtDouble( ( oMinX - extent.minimumX ) / resX ) )
                   .arg( formatVrtDouble( ( extent.maximumY - oMaxY ) / resY ) )
                   .arg( formatVrtDouble( ( oMaxX - oMinX ) / resX ) )
                   .arg( formatVrtDouble( ( oMaxY - oMinY ) / resY ) );
        }
      }
    }

    out << QStringLiteral( "    </%1>\n" ).arg( sourceType );
    out << QStringLiteral( "  </VRTRasterBand>\n" );
  }

  out << QStringLiteral( "</VRTDataset>\n" );
  return xml;
}

QString layoutVirtualRasterXml( const VirtualRasterRecipe &recipe,
                                const QVector<AssetSnapshot> &inputSnapshots,
                                const QString &vrtPath,
                                QString *errorCodeOut,
                                QString *errorMessageOut )
{
  const auto fail = [errorCodeOut, errorMessageOut]( const QString &code,
                                                     const QString &message ) {
    if ( errorCodeOut )
      *errorCodeOut = code;
    if ( errorMessageOut )
      *errorMessageOut = message;
    return QString();
  };

  // R29-T02 WP05/WP06: a recipe whose effective target CRS differs from an
  // input's own CRS (differing inputs, or shared inputs + an explicit target),
  // or whose input grid is rotated/sheared, cannot be laid out by the
  // axis-aligned SrcRect/DstRect rectangles the VRT composes — the previous
  // path re-tagged the pixels with the target SRS label while leaving them
  // laid out on the input grid (spatially wrong output). Warp every such
  // input onto the target grid first, through the sanctioned geospatial warp
  // seam (GDALWarp, staged + validated + atomically published per input),
  // and compose the VRT over the warped intermediates.
  QVector<VirtualRasterSourceInput> layoutInputs;
  layoutInputs.reserve( inputSnapshots.size() );
  for ( const AssetSnapshot &snapshot : inputSnapshots )
  {
    const auto *raster = std::get_if<RasterStructure>( &snapshot.structure() );
    if ( !raster )
      return fail( QStringLiteral( "virtual_raster.input_unavailable" ),
                   QStringLiteral( "A virtual raster input asset has no resolved "
                                   "raster structure" ) );
    layoutInputs.append( { snapshot.source().canonicalSource, *raster } );
  }

  const auto inputNeedsWarp = []( const RasterStructure &structure,
                                  const QString &targetCrs ) {
    if ( !structure.hasGeoTransform )
      return false; // no grid to transform; the VRT places it as today
    if ( structure.geoTransform[2] != 0.0 || structure.geoTransform[4] != 0.0 )
      return true; // rotation/shear: an axis-aligned rect cannot express it
    if ( structure.crsWkt.isEmpty() )
      return false; // CRS-less inputs are treated as being in the target CRS
    return !isSameCrs( structure.crsWkt, targetCrs );
  };

  QString effectiveTargetCrs = recipe.targetCrs;
  if ( effectiveTargetCrs.isEmpty() )
  {
    for ( const VirtualRasterSourceInput &input : layoutInputs )
      if ( !input.structure.crsWkt.isEmpty() )
      {
        effectiveTargetCrs = input.structure.crsWkt;
        break;
      }
  }

  bool anyWarp = false;
  if ( !effectiveTargetCrs.isEmpty() )
  {
    for ( const VirtualRasterSourceInput &input : layoutInputs )
      if ( inputNeedsWarp( input.structure, effectiveTargetCrs ) )
      {
        anyWarp = true;
        break;
      }
  }

  VirtualRasterPinnedGrid pinnedGrid;
  const VirtualRasterPinnedGrid *pinned = nullptr;
  QStringList liveIntermediates;
  if ( anyWarp )
  {
    try
    {
      // Resolve the target grid ONCE: resolution (recipe or first input),
      // extent policy over per-input extents TRANSFORMED into the target CRS
      // (raw numbers across differing CRSs are not comparable), then align
      // the extent to whole pixels — the warp -te bounds and the VRT grid
      // must be exactly the same grid.
      double resX = recipe.targetResolutionX;
      double resY = recipe.targetResolutionY;
      if ( resX <= 0.0 )
        resX = std::abs( layoutInputs.first().structure.geoTransform[1] );
      if ( resY <= 0.0 )
        resY = std::abs( layoutInputs.first().structure.geoTransform[5] );
      if ( resX <= 0.0 || resY <= 0.0 )
        throw sicnu::geo::GeoError( sicnu::geo::ErrorCode::InvalidArgument,
                                    "target resolution is zero" );

      const sicnu::geo::Crs targetCrs =
        sicnu::geo::Crs::fromUserInput( effectiveTargetCrs.toStdString() );

      bool haveExtent = false;
      SpatialExtent extent;
      for ( const VirtualRasterSourceInput &input : layoutInputs )
      {
        const RasterStructure &structure = input.structure;
        if ( !structure.hasGeoTransform || !structure.extent.valid )
          continue;
        SpatialExtent contribution = structure.extent;
        if ( !structure.crsWkt.isEmpty()
             && !isSameCrs( structure.crsWkt, effectiveTargetCrs ) )
        {
          const sicnu::geo::Crs sourceCrs =
            sicnu::geo::Crs::fromWkt( structure.crsWkt.toStdString() );
          const sicnu::geo::CrsTransform transform =
            sicnu::geo::CrsTransform::create( sourceCrs, targetCrs );
          const sicnu::geo::CrsBoundingBox bounds = transform.forwardBounds(
            { structure.extent.minimumX, structure.extent.minimumY,
              structure.extent.maximumX, structure.extent.maximumY } );
          contribution.minimumX = bounds.minX;
          contribution.minimumY = bounds.minY;
          contribution.maximumX = bounds.maxX;
          contribution.maximumY = bounds.maxY;
        }
        if ( !haveExtent )
        {
          extent = contribution;
          haveExtent = true;
        }
        else if ( recipe.extentPolicy == ExtentPolicy::Intersection )
        {
          extent.minimumX = std::max( extent.minimumX, contribution.minimumX );
          extent.minimumY = std::max( extent.minimumY, contribution.minimumY );
          extent.maximumX = std::min( extent.maximumX, contribution.maximumX );
          extent.maximumY = std::min( extent.maximumY, contribution.maximumY );
        }
        else
        {
          extent.minimumX = std::min( extent.minimumX, contribution.minimumX );
          extent.minimumY = std::min( extent.minimumY, contribution.minimumY );
          extent.maximumX = std::max( extent.maximumX, contribution.maximumX );
          extent.maximumY = std::max( extent.maximumY, contribution.maximumY );
        }
      }
      if ( !haveExtent || extent.maximumX <= extent.minimumX
           || extent.maximumY <= extent.minimumY )
        throw sicnu::geo::GeoError( sicnu::geo::ErrorCode::InvalidArgument,
                                    "transformed target extent is empty" );

      const double widthF = ( extent.maximumX - extent.minimumX ) / resX;
      const double heightF = ( extent.maximumY - extent.minimumY ) / resY;
      // Degenerate-grid refusal (R1 review P2): a cross-unit resolution
      // fallback (metres reprojected to degrees) rounds to <1 pixel —
      // refuse instead of publishing a 1x1 product that "succeeded".
      // Upper bound mirrors buildVirtualRasterXml's #633 guard: a quotient
      // beyond INT_MAX must fail typed, never wrap through the int cast.
      constexpr double kMaxWarpDimension = 1000000000.0;
      if ( widthF < 1.0 || heightF < 1.0 )
        throw sicnu::geo::GeoError(
          sicnu::geo::ErrorCode::InvalidArgument,
          "target grid is degenerate (extent/resolution rounds below one "
          "pixel) — set an explicit target resolution in the target CRS's "
          "units" );
      if ( widthF > kMaxWarpDimension || heightF > kMaxWarpDimension )
        throw sicnu::geo::GeoError(
          sicnu::geo::ErrorCode::InvalidArgument,
          "target grid dimensions exceed the supported maximum — set an "
          "explicit target resolution in the target CRS's units" );
      const int width = static_cast<int>( std::llround( widthF ) );
      const int height = static_cast<int>( std::llround( heightF ) );
      extent.maximumX = extent.minimumX + width * resX;
      extent.minimumY = extent.maximumY - height * resY;
      extent.valid = true;

      pinnedGrid.crs = effectiveTargetCrs;
      pinnedGrid.resX = resX;
      pinnedGrid.resY = resY;
      pinnedGrid.extent = extent;
      pinned = &pinnedGrid;

      const char *resamplingNameWarp = "near";
      switch ( recipe.resampling )
      {
        case ResamplingMethod::NearestNeighbour:
          resamplingNameWarp = "near";
          break;
        case ResamplingMethod::Bilinear:
          resamplingNameWarp = "bilinear";
          break;
        case ResamplingMethod::Cubic:
          resamplingNameWarp = "cubic";
          break;
      }

      // Recipe- AND input-content-keyed intermediate names (R1 review P2-7,
      // R2 review P2): one (recipe, input content) pair always addresses the
      // same <vrt>.warp<i>-<digest8>-<token>.tif, so a re-resolve atomically
      // replaces ITS OWN intermediates and neither a recipe change nor an
      // input-content change can mix generations under the still-served VRT
      // (a changed input addresses a NEW name; the old file survives until
      // the post-success sweep). warpRaster publishes each intermediate
      // atomically itself.
      const QString generation = recipe.contentDigest().left( 8 );
      const auto inputContentToken = []( const QString &path ) {
        const QFileInfo info( path );
        if ( !info.exists() )
          return QStringLiteral( "x" );
        return QStringLiteral( "%1-%2" )
          .arg( info.size() )
          .arg( info.lastModified().toSecsSinceEpoch() );
      };
      ensureGdalRuntime();
      for ( int i = 0; i < layoutInputs.size(); ++i )
      {
        if ( !inputNeedsWarp( layoutInputs[i].structure, effectiveTargetCrs ) )
          continue;
        const QString stagedPath =
          vrtPath + QStringLiteral( ".warp%1-%2-%3.tif" )
                      .arg( i )
                      .arg( generation )
                      .arg( inputContentToken( layoutInputs[i].sourcePath ) );
        sicnu::geo::WarpOptions warpOptions;
        warpOptions.targetCrs = effectiveTargetCrs.toStdString();
        warpOptions.resampling = resamplingNameWarp;
        warpOptions.targetResolutionX = resX;
        warpOptions.targetResolutionY = resY;
        warpOptions.targetBounds = { extent.minimumX, extent.minimumY,
                                     extent.maximumX, extent.maximumY };
        sicnu::geo::warpRaster( layoutInputs[i].sourcePath.toStdString(),
                                stagedPath.toStdString(), warpOptions );
        liveIntermediates << QFileInfo( stagedPath ).fileName();

        // The VRT must lay out the WARPED file's actual grid/bands, not the
        // original input's: reopen and re-read its structure.
        CPLPushErrorHandler( CPLQuietErrorHandler );
        GDALDatasetH warped =
          GDALOpen( stagedPath.toUtf8().constData(), GA_ReadOnly );
        CPLPopErrorHandler();
        if ( warped == nullptr )
          throw sicnu::geo::GeoError(
            sicnu::geo::ErrorCode::InvalidArgument,
            "warped intermediate cannot be reopened: "
              + stagedPath.toStdString() );
        VirtualRasterSourceInput warpedInput;
        warpedInput.sourcePath = stagedPath;
        warpedInput.structure = structureFromDataset( warped );
        GDALClose( warped );
        layoutInputs[i] = std::move( warpedInput );
      }
    }
    catch ( const sicnu::geo::GeoError &error )
    {
      return fail( QStringLiteral( "virtual_raster.warp_failed" ),
                   QStringLiteral( "Reprojecting a virtual raster input onto "
                                   "the target grid failed: %1" )
                     .arg( QString::fromUtf8( error.what() ) ) );
    }
  }

  const QString xml =
    pinned != nullptr
      ? buildVirtualRasterXml( recipe, layoutInputs, pinned )
      : buildVirtualRasterXml( recipe, inputSnapshots );
  if ( xml.isEmpty() )
    return fail( QStringLiteral( "virtual_raster.grid_invalid" ),
                 QStringLiteral( "The virtual raster target grid could not be "
                                 "derived from the recipe and its inputs" ) );

  // Sweep stale warp generations (best-effort; the live names belong to THIS
  // recipe generation and were just atomically republished).
  if ( !liveIntermediates.isEmpty() )
  {
    const QDir vrtDir = QFileInfo( vrtPath ).dir();
    const QString base = QFileInfo( vrtPath ).fileName() + QStringLiteral( ".warp" );
    const QSet<QString> live = QSet<QString>( liveIntermediates.begin(),
                                              liveIntermediates.end() );
    for ( const QFileInfo &entry : vrtDir.entryInfoList( QDir::Files ) )
      if ( entry.fileName().startsWith( base ) && !live.contains( entry.fileName() ) )
        QFile::remove( entry.absoluteFilePath() );
  }
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

  // R29-T02 WP05/WP06: the whole layout (warp staging included) lives in the
  // shared layoutVirtualRasterXml entry point — resolve() and the
  // DataManager's relocate rewrite MUST produce the same artifact for one
  // recipe (R1 review P2-6: a relocate that bypassed the warp silently
  // regressed warp recipes back to the pre-warp relabel layout).
  QString layoutErrorCode, layoutErrorMessage;
  const QString xml = layoutVirtualRasterXml( recipe, inputSnapshots,
                                              source.canonicalSource,
                                              &layoutErrorCode,
                                              &layoutErrorMessage );
  if ( xml.isEmpty() )
  {
    return Result<internal::ResolvedSource>::failure(
      diagnostic( layoutErrorCode, layoutErrorMessage ) );
  }

  // Publish through the single sidecar write authority (#1457/WP09+WP11, the
  // same authority the DataManager's relocate rewrite already uses): the old
  // QFile path truncated the previous VRT on open and ignored write()'s
  // return, so a full disk or an interrupted run left a torn file that the
  // structure probe below then read back as a "valid" dataset. temp+fsync+
  // rename keeps the old artifact intact on any failure. The explicit cap
  // keeps the sidecar-class 32 MiB default from silently narrowing VRT
  // payloads: composed-band VRT XML sits far below this, but a runaway
  // generator must still be refused instead of filling the disk.
  // swarm-review DAT-2: resolve() no longer writes the VRT with a truncating
  // QFile; last-good rotation stays off (the VRT is a disposable build
  // artifact regenerated from the recipe, matching the relocate call site).
  const QByteArray xmlBytes = xml.toUtf8();
  constexpr std::size_t kVrtPublishCap = std::size_t( 256 ) * 1024 * 1024;
  const sicnu::platform::sidecar::WriteResult published =
    sicnu::platform::sidecar::write(
      { source.canonicalSource.toUtf8().constData(),
        std::string( xmlBytes.constData(),
                     static_cast<std::size_t>( xmlBytes.size() ) ),
        "", kVrtPublishCap } );
  if ( !published )
  {
    // Pre-publish failures (EmptyPath/TooLarge/ClaimFailed/WriteFailed/
    // DurabilityFailed/PublishFailed/StagedCleanupFailed) leave the previous
    // artifact untouched — the caller keeps whatever was there before.
    return Result<internal::ResolvedSource>::failure(
      diagnostic( QStringLiteral( "virtual_raster.write_failed" ),
                  QStringLiteral( "Could not publish the virtual raster artifact to "
                                  "%1: %2" )
                    .arg( source.canonicalSource,
                          QString::fromUtf8( published.error.c_str() ) ) ) );
  }
  if ( published.status == sicnu::platform::sidecar::WriteStatus::VerifyFailed )
  {
    // The rename already committed but the read-back byte compare failed:
    // the target holds suspect bytes. Fail closed instead of resolving a
    // structure from an artifact that may not match the recipe (the relocate
    // path surfaces the same state as an error diagnostic, #1473).
    return Result<internal::ResolvedSource>::failure(
      diagnostic( QStringLiteral( "virtual_raster.verify_failed" ),
                  QStringLiteral( "Virtual raster published but read-back verification "
                                  "failed — the target holds suspect bytes: %1" )
                    .arg( source.canonicalSource ) ) );
  }

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

  resolved.structure = structureFromDataset( dataset );

  GDALClose( dataset );
  return Result<internal::ResolvedSource>::success( std::move( resolved ) );
}

} // namespace sicnu::data::providers
