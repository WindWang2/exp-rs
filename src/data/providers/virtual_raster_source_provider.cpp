#include "virtual_raster_source_provider.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QHash>
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
#include "geospatial/identity/asset_identity.h"
#include "platform/durable_sidecar.h"
#include "runtime/observability/fault_point.h"

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

/// WP05 reuse gate: does the staged intermediate still realize @p grid
/// (dimensions, origin/resolution, CRS)? An unchanged identity whose file
/// probes as the pinned grid is reusable without re-warping; anything else
/// (missing bands, torn file, stale grid from an older layout) rebuilds
/// through the normal atomic warp publish.
bool stagedMatchesPinnedGrid( const QString &path, const VirtualRasterPinnedGrid &grid )
{
  ensureGdalRuntime();
  CPLPushErrorHandler( CPLQuietErrorHandler );
  GDALDatasetH dataset = GDALOpen( path.toUtf8().constData(), GA_ReadOnly );
  CPLPopErrorHandler();
  if ( dataset == nullptr )
    return false;
  const RasterStructure structure = structureFromDataset( dataset );
  GDALClose( dataset );

  const int expectedWidth = static_cast<int>(
    std::llround( ( grid.extent.maximumX - grid.extent.minimumX ) / grid.resX ) );
  const int expectedHeight = static_cast<int>(
    std::llround( ( grid.extent.maximumY - grid.extent.minimumY ) / grid.resY ) );
  if ( structure.width != expectedWidth || structure.height != expectedHeight
       || structure.bandCount < 1 || !structure.hasGeoTransform )
    return false;
  if ( !isSameCrs( structure.crsWkt, grid.crs ) )
    return false;
  const double resTolerance = 1e-6 * std::max( grid.resX, grid.resY );
  const auto near = [resTolerance]( double a, double b ) {
    return std::abs( a - b ) <= resTolerance;
  };
  return near( structure.geoTransform[0], grid.extent.minimumX )
         && near( structure.geoTransform[1], grid.resX )
         && near( structure.geoTransform[3], grid.extent.maximumY )
         && near( -structure.geoTransform[5], grid.resY );
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
                                QString *errorMessageOut,
                                QStringList *liveIntermediatesOut,
                                sicnu::geo::ConvertProgress *progress )
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

      // Recipe- AND input-identity-keyed, reusable intermediates (R29 R1/R2
      // review keys, R30-T03 WP03/WP05): one (recipe, input identity) pair
      // always addresses the same <vrt>.warp-<digest8>-<identity>.tif, so a
      // re-resolve atomically replaces ITS OWN intermediates and neither a
      // recipe change nor an input-identity change can mix generations under
      // the still-served VRT (a changed input addresses a NEW name; the old
      // file survives until the post-publish sweep). warpRaster publishes
      // each intermediate atomically itself. The name no longer carries the
      // input INDEX: several BandRefs of one asset share one prepared file
      // (one warp), and an unchanged identity whose staged file still probes
      // as the pinned grid is REUSED instead of re-warped (repeat resolve /
      // project reopen cost). The identity is the catalog revision joined
      // with the geospatial foundation's proven local identity — the same
      // "li1:v1:<hex>" vocabulary as the import preflight, never a second
      // identity scheme; a same-size/same-second replacement gets a distinct
      // prefix-hash and therefore a distinct name. Unprovable files degrade
      // to metadata facts (labelled "m-", ms-resolution mtime) — weaker,
      // honestly so, and still revision-separated.
      const QString generation = recipe.contentDigest().left( 8 );
      // R1 review P0-1: the identity must be the foundation's UNIFIED
      // vocabulary, not a plain-file stat — a container-member spelling
      // (`NETCDF:"f.nc":var`, registerable since WP07) or a VSI handle
      // fails a plain stat and used to collapse onto a bare "r<rev>-x"
      // that was IDENTICAL for every unprovable input, letting two
      // distinct inputs share one staged warp file (band 2 silently served
      // band 1's pixels). assetIdentityToken gives members the content-
      // proven sd1: token (container's li1 basis + selector) and local
      // files their li1: token. Network-backed spellings are NOT probed
      // (offline doctrine — no packets from layout); they and anything
      // still unprovable take a collision-free path-hash fallback: content
      // is then unproven, but two distinct inputs can never address the
      // same staged file, and the catalog revision in the key still
      // separates re-registered generations.
      const auto isNetworkSpelling = []( const QString &path ) {
        static const QStringList kNetworkPrefixes = {
          QStringLiteral( "http://" ), QStringLiteral( "https://" ),
          QStringLiteral( "/vsicurl/" ), QStringLiteral( "/vsicurl_streaming/" ),
          QStringLiteral( "/vsis3/" ), QStringLiteral( "/vsigs/" ),
          QStringLiteral( "/vsiaz/" ), QStringLiteral( "/vsiadls/" ),
          QStringLiteral( "/vsihdfs/" ), QStringLiteral( "/vsioss/" ),
          QStringLiteral( "/vsiswift/" ),
        };
        const QString lowered = path.toLower();
        for ( const QString &prefix : kNetworkPrefixes )
          if ( lowered.startsWith( prefix ) )
            return true;
        return false;
      };
      const auto inputIdentityToken = [isNetworkSpelling]( const AssetSnapshot &snapshot ) {
        const QString path = snapshot.source().canonicalSource;
        const QString revision = QString::number( snapshot.revision().value() );
        if ( !isNetworkSpelling( path ) )
        {
          const sicnu::geo::AssetIdentity identity =
            sicnu::geo::assetIdentityToken( path.toStdString() );
          if ( identity.provable() )
          {
            // ':' is legal in POSIX paths but not Windows filenames — keep
            // the staged name portable by flattening the separators.
            QString token = QString::fromStdString( identity.token );
            token.replace( QLatin1Char( ':' ), QLatin1Char( '.' ) );
            return QStringLiteral( "r%1-%2" ).arg( revision, token );
          }
        }
        const QByteArray pathHash = QCryptographicHash::hash(
          path.toUtf8(), QCryptographicHash::Sha256 );
        return QStringLiteral( "r%1-u-%2" )
          .arg( revision, QString::fromUtf8( pathHash.toHex().left( 16 ) ) );
      };
      const auto reportProgress = [progress]( double fraction, const QString &message ) {
        if ( progress != nullptr && progress->report )
          progress->report( fraction, message.toStdString() );
      };
      const QString plannedGrid =
        QStringLiteral( "target grid planned: %1x%2 @ %3/%4 in %5" )
          .arg( QString::number( width ),
                QString::number( height ),
                QString::number( resX ),
                QString::number( resY ),
                effectiveTargetCrs );
      reportProgress( 0.05, plannedGrid );

      ensureGdalRuntime();
      struct PreparedInput
      {
        QString stagedPath;
        RasterStructure structure;
      };
      QHash<QString, PreparedInput> preparedInputs;
      QVector<int> warpInputs; // indexes needing warp, for staged reporting
      for ( int i = 0; i < layoutInputs.size(); ++i )
        if ( inputNeedsWarp( layoutInputs[i].structure, effectiveTargetCrs ) )
          warpInputs.append( i );
      const int uniqueWarpSources = static_cast<int>(
        [ &warpInputs, &inputSnapshots, &inputIdentityToken ]() {
          QSet<QString> identities;
          for ( int i : warpInputs )
            identities.insert( inputIdentityToken( inputSnapshots.at( i ) ) );
          return identities.size();
        }() );
      int preparedCount = 0;
      for ( int inputIndex : warpInputs )
      {
        const QString identity = inputIdentityToken( inputSnapshots.at( inputIndex ) );
        const auto preparedIt = preparedInputs.constFind( identity );
        if ( preparedIt != preparedInputs.constEnd() )
        {
          // Same input asset referenced by several BandRefs: the file is
          // already prepared for THIS layout — reference it, warp nothing.
          if ( !liveIntermediates.contains( QFileInfo( preparedIt->stagedPath ).fileName() ) )
            liveIntermediates << QFileInfo( preparedIt->stagedPath ).fileName();
          layoutInputs[inputIndex] = { preparedIt->stagedPath, preparedIt->structure };
          continue;
        }

        const QString stagedPath = vrtPath + QStringLiteral( ".warp-%1-%2.tif" )
                                            .arg( generation, identity );
        bool reusedExisting = false;
        if ( QFileInfo::exists( stagedPath )
             && stagedMatchesPinnedGrid( stagedPath, *pinned ) )
        {
          // An unchanged identity whose intermediate still probes as the
          // pinned grid: reuse it (WP05). A changed identity addresses a
          // different name and rebuilds; a corrupt/partial file of the SAME
          // name fails the probe and is re-warped through the same atomic
          // publish authority.
          reusedExisting = true;
        }
        else
        {
          if ( progress != nullptr && progress->cancelFlag
               && progress->cancelFlag->load() )
            throw sicnu::geo::GeoError( sicnu::geo::ErrorCode::Cancelled,
                                        "virtual raster preparation cancelled" );
          // Rescale the warp's own 0→1 callbacks into this input's slice of
          // the layout band (R1 review P3-6): a consumer observes one
          // monotonic 0.05→0.90 sequence instead of interleaved restarts.
          sicnu::geo::ConvertProgress scaledProgress;
          sicnu::geo::ConvertProgress *warpProgress = nullptr;
          if ( progress != nullptr )
          {
            const double sliceBase =
              0.05 + 0.85 * static_cast<double>( preparedCount )
                       / static_cast<double>( uniqueWarpSources );
            const double sliceSpan =
              0.85 / static_cast<double>( uniqueWarpSources );
            scaledProgress.cancelFlag = progress->cancelFlag;
            scaledProgress.report =
              [progress, sliceBase, sliceSpan]( double fraction,
                                                const std::string &message ) {
                if ( progress->report )
                  progress->report( sliceBase + sliceSpan * fraction, message );
              };
            warpProgress = &scaledProgress;
          }
          sicnu::geo::WarpOptions warpOptions;
          warpOptions.targetCrs = effectiveTargetCrs.toStdString();
          warpOptions.resampling = resamplingNameWarp;
          warpOptions.targetResolutionX = resX;
          warpOptions.targetResolutionY = resY;
          warpOptions.targetBounds = { extent.minimumX, extent.minimumY,
                                       extent.maximumX, extent.maximumY };
          // R30-T03 WP06: pin the warp's void contract for FillValue recipes.
          // Unpinned, a source without its own declaration leaves warp voids
          // as raw 0 — pixels the composed VRT then serves as VALID zeros
          // (its NODATA mask equals the fill, not 0). Declaring the fill as
          // both -srcnodata and -dstnodata makes the contract single and
          // machine-readable: source values equal to the fill AND uncovered
          // target areas both realize as the declared fill. Preserve recipes
          // keep GDAL's implicit behavior (the source's own declarations are
          // honored, masked and copied to the output — proven by the rotated
          // NoData oracle); a Preserve recipe over undeclared sources keeps
          // the documented metadata-only voids (0, undeclared), surfaced by
          // the preflight's warp-void warning.
          if ( recipe.noDataPolicy == NoDataPolicy::FillValue )
          {
            warpOptions.sourceNodata = { recipe.noDataFillValue };
            warpOptions.targetNodata = { recipe.noDataFillValue };
          }
          sicnu::geo::warpRaster( layoutInputs[inputIndex].sourcePath.toStdString(),
                                  stagedPath.toStdString(), warpOptions,
                                  warpProgress );
        }
        ++preparedCount;
        reportProgress(
          0.05 + 0.85 * preparedCount / uniqueWarpSources,
          QStringLiteral( "%1 input %2/%3 (%4)" )
            .arg( reusedExisting ? QStringLiteral( "reusing prepared" )
                                 : QStringLiteral( "warped" ),
                  QString::number( preparedCount ),
                  QString::number( uniqueWarpSources ),
                  QFileInfo( layoutInputs[inputIndex].sourcePath ).fileName() ) );
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
        PreparedInput prepared;
        prepared.stagedPath = stagedPath;
        prepared.structure = structureFromDataset( warped );
        GDALClose( warped );
        preparedInputs.insert( identity, prepared );
        layoutInputs[inputIndex] = { prepared.stagedPath, prepared.structure };
      }
    }
    catch ( const sicnu::geo::GeoError &error )
    {
      // Cancellation is typed distinctly from a warp failure: the previous
      // generation is untouched (nothing swept, nothing published) and a late
      // progress callback must not flip a cancelled run back to Ready — the
      // failure return below is the terminal state for this resolve.
      const bool cancelled =
        error.code() == sicnu::geo::ErrorCode::Cancelled;
      return fail( cancelled ? QStringLiteral( "virtual_raster.cancelled" )
                             : QStringLiteral( "virtual_raster.warp_failed" ),
                   QStringLiteral( "Reprojecting a virtual raster input onto "
                                   "the target grid %1: %2" )
                     .arg( cancelled ? QStringLiteral( "was cancelled" )
                                     : QStringLiteral( "failed" ),
                           QString::fromUtf8( error.what() ) ) );
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

  // R30-T03 WP02: report THIS generation's intermediates and sweep nothing.
  // The previous sweep here deleted the still-published VRT's dependencies
  // before the caller committed the new artifact — any later publish failure
  // left the old VRT referencing deleted files. Sweeping is now the caller's
  // post-publish step (sweepStaleWarpIntermediates), so on every failure
  // path (warp failure, grid refusal, publish failure, typed VerifyFailed)
  // each VRT on disk keeps referencing existing files.
  if ( liveIntermediatesOut != nullptr )
    *liveIntermediatesOut = liveIntermediates;
  return xml;
}

void sweepStaleWarpIntermediates( const QString &vrtPath,
                                  const QStringList &liveNames )
{
  const QString base = QFileInfo( vrtPath ).fileName() + QStringLiteral( ".warp" );
  const QSet<QString> live = QSet<QString>( liveNames.begin(), liveNames.end() );
  for ( const QFileInfo &entry : QFileInfo( vrtPath ).dir().entryInfoList( QDir::Files ) )
    if ( entry.fileName().startsWith( base ) && !live.contains( entry.fileName() ) )
      QFile::remove( entry.absoluteFilePath() );
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
  return resolve( source, nullptr );
}

Result<internal::ResolvedSource> VirtualRasterSourceProvider::resolve(
  const SourceDescriptor &source, sicnu::geo::ConvertProgress *progress ) const
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
  QStringList liveIntermediates;
  const QString xml = layoutVirtualRasterXml( recipe, inputSnapshots,
                                              source.canonicalSource,
                                              &layoutErrorCode,
                                              &layoutErrorMessage,
                                              &liveIntermediates, progress );
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
  // Test-only seam (#1473 pattern): the fault fires BEFORE the write, so the
  // target keeps its previous complete bytes exactly like a real pre-publish
  // failure (temp-write/durability refusal) — never a post-hoc status flip
  // that would have already committed new bytes. The typed failure return
  // below IS the production failure branch.
  const bool injectPublishFailure = SICNU_FAULT_POINT( "virtual_raster.publish" );
  sicnu::platform::sidecar::WriteResult published;
  if ( injectPublishFailure )
  {
    published.status = sicnu::platform::sidecar::WriteStatus::WriteFailed;
    published.error = "injected publish fault";
  }
  else
  {
    published = sicnu::platform::sidecar::write(
      { source.canonicalSource.toUtf8().constData(),
        std::string( xmlBytes.constData(),
                     static_cast<std::size_t>( xmlBytes.size() ) ),
        "", kVrtPublishCap } );
  }
  if ( !published )
  {
    // Pre-publish failures (EmptyPath/TooLarge/ClaimFailed/WriteFailed/
    // DurabilityFailed/PublishFailed/StagedCleanupFailed) leave the previous
    // artifact untouched — the caller keeps whatever was there before. The
    // previous generation's warp intermediates equally stay: sweeping
    // happens only after a committed publish (R30-T03 WP02), so the
    // still-served VRT keeps referencing existing, readable files.
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
    // path surfaces the same state as an error diagnostic, #1473). No sweep
    // either: the committed-but-suspect VRT references THIS generation's
    // intermediates — deleting them would guarantee an unreadable artifact.
    return Result<internal::ResolvedSource>::failure(
      diagnostic( QStringLiteral( "virtual_raster.verify_failed" ),
                  QStringLiteral( "Virtual raster published but read-back verification "
                                  "failed — the target holds suspect bytes: %1" )
                    .arg( source.canonicalSource ) ) );
  }

  // The new generation is committed: only now may previous generations'
  // intermediates go — every VRT that can still be opened references files
  // that exist (the old VRT is already replaced; any crash before this
  // sweep merely leaves inert orphan files, cleaned on the next success).
  sweepStaleWarpIntermediates( source.canonicalSource, liveIntermediates );

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
