#include "virtual_raster_preflight.h"

#include <cmath>

#include "data_manager.h"
#include "raster_grid_compat.h"

namespace sicnu::data
{

namespace
{

Diagnostic errorDiagnostic( const QString &code, const QString &message )
{
  return Diagnostic{ code, message, DiagnosticSeverity::Error };
}

Diagnostic warningDiagnostic( const QString &code, const QString &message )
{
  return Diagnostic{ code, message, DiagnosticSeverity::Warning };
}

/// Relative tolerance for grid (pixel size) comparison.
constexpr double kGridTolerance = 1e-9;

bool samePixelSize( double a, double b )
{
  const double scale = std::max( { std::abs( a ), std::abs( b ), 1.0 } );
  return std::abs( a - b ) <= kGridTolerance * scale;
}

/// The intersection of two extents; `valid` is false when they are disjoint.
SpatialExtent intersect( const SpatialExtent &a, const SpatialExtent &b )
{
  SpatialExtent result;
  result.minimumX = std::max( a.minimumX, b.minimumX );
  result.minimumY = std::max( a.minimumY, b.minimumY );
  result.maximumX = std::min( a.maximumX, b.maximumX );
  result.maximumY = std::min( a.maximumY, b.maximumY );
  result.valid = result.maximumX > result.minimumX &&
                 result.maximumY > result.minimumY;
  return result;
}

bool covers( const SpatialExtent &outer, const SpatialExtent &inner )
{
  return inner.minimumX >= outer.minimumX && inner.minimumY >= outer.minimumY &&
         inner.maximumX <= outer.maximumX && inner.maximumY <= outer.maximumY;
}

/// True when a color-interpretation string marks the band categorical.
bool isCategorical( const QString &colorInterpretation )
{
  return colorInterpretation.contains( QStringLiteral( "Palette" ),
                                       Qt::CaseInsensitive );
}

} // namespace

// R30-T03 WP06: a warp is required but an input band declares no NoData and
// the recipe pins no fill value — pixels outside that input's reprojected
// footprint read as raw, UNDECLARED zeros and cannot be distinguished from
// valid zeros. Surface one typed warning (creatable, honestly labelled;
// never a refusal and never an invented sentinel).
static void appendWarpVoidWarning( PreflightResult &result,
                                   const VirtualRasterRecipe &recipe,
                                   const QVector<RasterStructure> &structures,
                                   const QVector<QString> &displayNames )
{
  if ( recipe.noDataPolicy == NoDataPolicy::FillValue )
    return;
  for ( int i = 0; i < recipe.inputs.size(); ++i )
  {
    bool declaresNoData = false;
    for ( const RasterBandStructure &band : structures[i].bands )
      if ( band.number == recipe.inputs[i].bandNumber
           && band.noDataValue.has_value() )
        declaresNoData = true;
    if ( !declaresNoData )
    {
      result.diagnostics.append( Diagnostic{
        QStringLiteral( "preflight.warp_void_undeclared" ),
        QStringLiteral( "Input %1 band %2 declares no NoData and the recipe sets "
                        "no fill value; pixels outside its reprojected footprint "
                        "will read as raw 0 and cannot be distinguished from "
                        "valid zeros — declare NoData on the source or set a "
                        "fill value" )
          .arg( displayNames[i] )
          .arg( recipe.inputs[i].bandNumber ),
        DiagnosticSeverity::Warning } );
      return; // one warning is enough to act on
    }
  }
}

PreflightResult preflightVirtualRaster( const VirtualRasterRecipe &recipe,
                                        const DataManager &manager )
{
  const auto hardFailure = []( PreflightVerdict verdict, const QString &code,
                               const QString &message ) {
    PreflightResult result;
    result.verdict = verdict;
    result.canCreate = false;
    result.diagnostics = { errorDiagnostic( code, message ) };
    return result;
  };

  // An empty recipe cannot be classified at all (every later step dereferences
  // the first input's structure).
  if ( recipe.inputs.isEmpty() )
  {
    return hardFailure(
      PreflightVerdict::UnavailableSource,
      QStringLiteral( "preflight.unavailable_source" ),
      QStringLiteral( "The recipe has no input bands" ) );
  }

  // --- UnavailableSource: unknown, non-raster, Missing/Error, band out of
  // range. Checked first: every later classification needs the structure.
  QVector<RasterStructure> structures;
  QVector<QString> displayNames;
  structures.reserve( recipe.inputs.size() );
  displayNames.reserve( recipe.inputs.size() );

  for ( const BandRef &input : recipe.inputs )
  {
    const std::optional<AssetSnapshot> snapshot = manager.asset( input.asset );
    if ( !snapshot.has_value() )
    {
      return hardFailure(
        PreflightVerdict::UnavailableSource,
        QStringLiteral( "preflight.unavailable_source" ),
        QStringLiteral( "Input asset %1 is not registered" )
          .arg( input.asset.toString() ) );
    }
    if ( snapshot->kind() != AssetKind::Raster &&
         snapshot->kind() != AssetKind::VirtualRaster )
    {
      return hardFailure(
        PreflightVerdict::UnavailableSource,
        QStringLiteral( "preflight.unavailable_source" ),
        QStringLiteral( "Input asset %1 is not a raster asset" )
          .arg( snapshot->displayName() ) );
    }
    if ( snapshot->state() == AssetState::Missing ||
         snapshot->state() == AssetState::Error )
    {
      return hardFailure(
        PreflightVerdict::UnavailableSource,
        QStringLiteral( "preflight.unavailable_source" ),
        QStringLiteral( "Input asset %1 is unavailable (state: %2)" )
          .arg( snapshot->displayName() )
          .arg( snapshot->state() == AssetState::Missing
                  ? QStringLiteral( "Missing" )
                  : QStringLiteral( "Error" ) ) );
    }
    const auto *raster = std::get_if<RasterStructure>( &snapshot->structure() );
    if ( !raster )
    {
      return hardFailure(
        PreflightVerdict::UnavailableSource,
        QStringLiteral( "preflight.unavailable_source" ),
        QStringLiteral( "Input asset %1 has no resolved raster structure" )
          .arg( snapshot->displayName() ) );
    }
    if ( input.bandNumber < 1 || input.bandNumber > raster->bandCount )
    {
      return hardFailure(
        PreflightVerdict::UnavailableSource,
        QStringLiteral( "preflight.unavailable_source" ),
        QStringLiteral( "Input asset %1 has no band %2 (it has %3 band(s))" )
          .arg( snapshot->displayName() )
          .arg( input.bandNumber )
          .arg( raster->bandCount ) );
    }
    structures.append( *raster );
    displayNames.append( snapshot->displayName() );
  }

  // --- MissingCRS: an input lacks a CRS and the recipe pins no target. The
  // geotransform-less case folds in here: without a grid the band cannot be
  // placed on the target CRS at all unless the recipe pins both.
  for ( int i = 0; i < structures.size(); ++i )
  {
    if ( structures[i].crsWkt.isEmpty() && recipe.targetCrs.isEmpty() )
    {
      return hardFailure(
        PreflightVerdict::MissingCRS,
        QStringLiteral( "preflight.missing_crs" ),
        QStringLiteral( "Input %1 has no CRS; set an explicit target CRS in the "
                        "recipe to treat it as the target" )
          .arg( displayNames[i] ) );
    }
  }

  // --- CRS comparison is computed up front: extent overlap is only
  // meaningful in a common CRS, and the RequiresReprojection verdict reuses
  // this below. An input without a CRS is treated as being in the recipe's
  // target CRS (the MissingCRS check above guarantees targetCrs is set then).
  // Comparison is semantic (GDAL IsSame, via raster_grid_compat's shared
  // helper): equivalent-but-differently-encoded WKTs of one CRS must not
  // force a spurious RequiresReprojection verdict.
  QString commonCrs;
  bool crsDiffer = false;
  for ( const RasterStructure &structure : structures )
  {
    const QString crs = structure.crsWkt.isEmpty() ? recipe.targetCrs
                                                   : structure.crsWkt;
    if ( commonCrs.isEmpty() )
      commonCrs = crs;
    else if ( !isSameCrs( crs, commonCrs ) )
      crsDiffer = true;
  }

  // --- NoOverlap: only meaningful when every input has a valid extent AND
  // the inputs share a CRS (raw extent numbers across differing CRSs are not
  // comparable; reprojection may make them overlap). A Union extent policy is
  // the explicit advanced choice that tolerates disjoint inputs (uncovered
  // pixels are NoData-filled), so the overlap checks are skipped entirely.
  // PartialOverlap is a lower-priority warning and is deferred to the end.
  bool allExtentsValid = true;
  for ( const RasterStructure &structure : structures )
  {
    if ( !structure.hasGeoTransform || !structure.extent.valid )
      allExtentsValid = false;
  }

  bool partialOverlap = false;
  if ( allExtentsValid && !crsDiffer &&
       recipe.extentPolicy == ExtentPolicy::Intersection )
  {
    SpatialExtent intersection = structures.first().extent;
    for ( const RasterStructure &structure : structures )
      intersection = intersect( intersection, structure.extent );

    if ( !intersection.valid )
    {
      return hardFailure(
        PreflightVerdict::NoOverlap,
        QStringLiteral( "preflight.no_overlap" ),
        QStringLiteral( "The input extents do not intersect; an empty "
                        "intersection rejects creation" ) );
    }

    for ( const RasterStructure &structure : structures )
    {
      if ( !covers( intersection, structure.extent ) )
        partialOverlap = true;
    }
  }

  // --- UnsupportedDataType: categorical input with continuous resampling.
  if ( recipe.resampling != ResamplingMethod::NearestNeighbour )
  {
    for ( int i = 0; i < recipe.inputs.size(); ++i )
    {
      const RasterStructure &structure = structures[i];
      for ( const RasterBandStructure &band : structure.bands )
      {
        if ( band.number == recipe.inputs[i].bandNumber &&
             isCategorical( band.colorInterpretation ) )
        {
          return hardFailure(
            PreflightVerdict::UnsupportedDataType,
            QStringLiteral( "preflight.unsupported_data_type" ),
            QStringLiteral( "Input %1 band %2 is categorical; continuous "
                            "resampling cannot be applied silently - use "
                            "nearest neighbour" )
              .arg( displayNames[i] )
              .arg( band.number ) );
        }
      }
    }
  }

  // --- RequiresReprojection: differing CRSs, resolvable by an explicit target.
  // R29-T02 WP05: the check is target-relative, not only input-relative —
  // inputs that all share one CRS but differ from the recipe's EXPLICIT
  // target also need a real reprojection. Without this the verdict fell
  // through to Compatible and the provider re-tagged the pixels with the
  // target SRS label while leaving them laid out on the input grid (an SRS
  // relabel, spatially wrong output). The warp itself is delivered by the
  // provider (WP06: per-input staged GDALWarp onto the target grid).
  const bool targetDiffersFromInputs = !recipe.targetCrs.isEmpty()
                                       && !commonCrs.isEmpty()
                                       && !isSameCrs( recipe.targetCrs, commonCrs );
  if ( crsDiffer || targetDiffersFromInputs )
  {
    PreflightResult result;
    result.verdict = PreflightVerdict::RequiresReprojection;
    // canCreate gates on what the warp can actually pin down: a target CRS
    // always, and — when the target differs from the inputs' CRS, whose
    // units may differ from the inputs' — an explicit target resolution.
    // Without it the provider would fall back to the first input's pixel
    // size in ITS units (e.g. metres reprojected to degrees), which rounds
    // to a degenerate ~1x1 grid (R1 review P2).
    result.canCreate = !recipe.targetCrs.isEmpty()
                       && ( crsDiffer || ( recipe.targetResolutionX > 0.0
                                           && recipe.targetResolutionY > 0.0 ) );
    QString message;
    if ( crsDiffer )
    {
      message = result.canCreate
                  ? QStringLiteral( "The input CRSs differ; inputs will be "
                                    "reprojected to the recipe's target CRS" )
                  : QStringLiteral( "The input CRSs differ; set an explicit "
                                    "target CRS in the recipe" );
    }
    else
    {
      message = result.canCreate
                  ? QStringLiteral( "The inputs share one CRS but the recipe's "
                                    "explicit target CRS differs; the inputs "
                                    "will be reprojected onto the target grid "
                                    "(a real warp, not an SRS relabel)" )
                  : QStringLiteral( "The inputs share one CRS but the recipe's "
                                    "explicit target CRS differs; set an "
                                    "explicit target resolution so the warp "
                                    "grid is pinned in the target CRS's units" );
    }
    result.diagnostics = { warningDiagnostic(
      QStringLiteral( "preflight.requires_reprojection" ), message ) };
    appendWarpVoidWarning( result, recipe, structures, displayNames );
    return result;
  }

  // --- RequiresResampling: differing grids, resolvable by an explicit target
  // resolution. Inputs without a geotransform always need resampling onto the
  // target grid; any rotation/shear (gt[2]/gt[4] non-zero) counts as a grid
  // difference - the simple grid comparison cannot express it.
  {
    bool gridsDiffer = false;
    const RasterStructure &first = structures.first();
    const double referenceX = std::abs( first.geoTransform[1] );
    const double referenceY = std::abs( first.geoTransform[5] );
    for ( const RasterStructure &structure : structures )
    {
      if ( !structure.hasGeoTransform )
      {
        gridsDiffer = true;
        break;
      }
      if ( structure.geoTransform[2] != 0.0 || structure.geoTransform[4] != 0.0 )
        gridsDiffer = true;
      if ( !samePixelSize( std::abs( structure.geoTransform[1] ), referenceX ) ||
           !samePixelSize( std::abs( structure.geoTransform[5] ), referenceY ) )
        gridsDiffer = true;
    }
    if ( gridsDiffer )
    {
      PreflightResult result;
      result.verdict = PreflightVerdict::RequiresResampling;
      result.canCreate =
        recipe.targetResolutionX > 0.0 && recipe.targetResolutionY > 0.0;
      result.diagnostics = { warningDiagnostic(
        QStringLiteral( "preflight.requires_resampling" ),
        result.canCreate
          ? QStringLiteral( "The input grids differ; inputs will be resampled "
                            "to the recipe's target resolution" )
          : QStringLiteral( "The input grids differ; set an explicit target "
                            "resolution in the recipe" ) ) };
      // Only rotated/sheared inputs go through the warp (a pure resolution
      // difference composes through VRT rectangles); those carry the same
      // undeclared-void caveat as a reprojection.
      for ( const RasterStructure &structure : structures )
        if ( structure.hasGeoTransform
             && ( structure.geoTransform[2] != 0.0
                  || structure.geoTransform[4] != 0.0 ) )
        {
          appendWarpVoidWarning( result, recipe, structures, displayNames );
          break;
        }
      return result;
    }
  }

  // --- PartialOverlap: lowest-priority warning, after the CRS/grid checks.
  if ( partialOverlap )
  {
    PreflightResult result;
    result.verdict = PreflightVerdict::PartialOverlap;
    result.canCreate = true;
    result.diagnostics = { warningDiagnostic(
      QStringLiteral( "preflight.partial_overlap" ),
      QStringLiteral( "The inputs only partially overlap; the output covers "
                      "their intersection" ) ) };
    return result;
  }

  PreflightResult result;
  result.verdict = PreflightVerdict::Compatible;
  result.canCreate = true;
  return result;
}

} // namespace sicnu::data
