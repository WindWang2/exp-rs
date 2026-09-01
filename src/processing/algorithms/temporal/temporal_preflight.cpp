// src/processing/algorithms/temporal/temporal_preflight.cpp
#include "temporal_preflight.h"

#include "data/raster_grid_compat.h"
#include "processing/algorithms/satellite_products.h"
#include "processing/algorithms/temporal/temporal_band_roles.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_grid_compat.h"

#include <gdal.h>

#include <vector>

namespace sicnu::temporal
{

namespace
{

struct BandScaleOffset
{
  bool defined = false;
  double scale = 1.0;
  double offset = 0.0;
};

BandScaleOffset bandScaleOffset( const GdalDatasetWrapper &ds, int band )
{
  BandScaleOffset so;
  if ( !ds.isValid() || band < 1 || band > ds.bandCount() )
    return so;
  GDALDatasetH h = static_cast<GDALDatasetH>( ds.dataset() );
  GDALRasterBandH rasterBand = GDALGetRasterBand( h, band );
  if ( !rasterBand )
    return so;
  int scaleOk = 0;
  int offsetOk = 0;
  // A declared scale of 1 / offset of 0 still counts as "defined" — mixing
  // declared and undeclared scenes is the hazard, not the identity values.
  const double scale = GDALGetRasterScale( rasterBand, &scaleOk );
  const double offset = GDALGetRasterOffset( rasterBand, &offsetOk );
  so.defined = scaleOk != 0 || offsetOk != 0;
  so.scale = scaleOk ? scale : 1.0;
  so.offset = offsetOk ? offset : 0.0;
  return so;
}

/// Native dtypes the shared QaMask kernels + reader scratch can handle.
bool maskBandTypeSupported( const GdalDatasetWrapper &ds, int band )
{
  const int dtype = ds.bandDataType( band );
  return dtype == GDT_Byte || dtype == GDT_UInt16 || dtype == GDT_Int16;
}

/// Mask-band inference. Deliberately conservative: the generic BandRole::QA
/// is assigned to MANY non-cloud layers at import (Sentinel-2 AOT/WVP/MSK_*,
/// Landsat QA_RADSAT), so a bare 'qa' role is NOT trusted as a cloud mask —
/// only QA_PIXEL-shaped names get the Landsat bit kernel, and SCL is detected
/// by role or name. Anything else must be passed explicitly as mask_band
/// (0/1 validity semantics; use rs:qa_mask to derive one from product QA).
int resolveMaskBand( const GdalDatasetWrapper &ds, int explicitBand, QString *kind,
                     bool *unsupportedType )
{
  *unsupportedType = false;
  const int bandCount = ds.bandCount();
  auto kindFor = [&]( int band ) {
    const QString role = ds.bandMetadataItem( band, "SICNU_BAND_ROLE" ).toLower();
    const QString desc = ds.bandDescription( band ).toLower();
    if ( role == QLatin1String( "scene_classification" ) || desc == QLatin1String( "scl" ) ||
         desc.contains( QLatin1String( "scene_classification" ) ) )
      return QStringLiteral( "sentinel2_scl" );
    if ( desc.contains( QLatin1String( "qa_pixel" ) ) || desc.contains( QLatin1String( "pixel_qa" ) ) )
      return QStringLiteral( "landsat_qa_pixel" );
    return QString();
  };

  if ( explicitBand > 0 )
  {
    // Explicit selection: honor product semantics when the band is
    // recognizable, otherwise 0/1 validity-mask semantics.
    const QString inferred = kindFor( explicitBand );
    *kind = inferred.isEmpty() ? QStringLiteral( "explicit" ) : inferred;
    *unsupportedType = !maskBandTypeSupported( ds, explicitBand );
    return explicitBand;
  }
  for ( int b = 1; b <= bandCount; ++b )
  {
    const QString k = kindFor( b );
    if ( !k.isEmpty() && maskBandTypeSupported( ds, b ) )
    {
      *kind = k;
      return b;
    }
  }
  *kind = QString();
  return 0;
}

} // namespace

bool TemporalPreflightReport::ok() const
{
  return firstBlocking().code.isEmpty();
}

PreflightIssue TemporalPreflightReport::firstBlocking() const
{
  for ( const PreflightIssue &issue : issues )
    if ( issue.blocking )
      return issue;
  return PreflightIssue{};
}

Json::Value TemporalPreflightReport::toJson() const
{
  Json::Value v( Json::objectValue );
  v["ok"] = ok();
  v["scene_count"] = sceneCount;
  v["scenes_with_time"] = scenesWithTime;
  v["duplicate_time_count"] = duplicateTimeCount;
  if ( !timeRangeStartIso.isEmpty() )
  {
    Json::Value range( Json::arrayValue );
    range.append( timeRangeStartIso.toStdString() );
    range.append( timeRangeEndIso.toStdString() );
    v["time_range"] = range;
  }
  v["grid_compatible"] = gridCompatible;
  v["radiometric_state"] = commonRadiometricState.toStdString();
  v["uniform_scale_offset"] = uniformScaleOffset;
  v["scale_offset_declared"] = scaleOffsetDeclared;
  if ( scaleOffsetDeclared )
  {
    v["scale"] = uniformScale;
    v["offset"] = uniformOffset;
  }
  Json::Value issueList( Json::arrayValue );
  for ( const PreflightIssue &issue : issues )
  {
    Json::Value iv( Json::objectValue );
    iv["code"] = issue.code.toStdString();
    iv["message"] = issue.message.toStdString();
    iv["blocking"] = issue.blocking;
    if ( !issue.scenePath.isEmpty() )
      iv["scene"] = issue.scenePath.toStdString();
    issueList.append( iv );
  }
  v["issues"] = issueList;
  return v;
}

TemporalPreflightReport runPreflight( const TemporalCollection &collection,
                                      const PreflightOptions &options,
                                      const QString &analysisRoleId,
                                      int analysisBandOverride )
{
  ensureGdalInit();
  TemporalPreflightReport report;
  report.sceneCount = collection.sceneCount();
  report.commonRadiometricState.clear();

  const QVector<TemporalSceneRef> &scenes = collection.scenes();
  if ( scenes.isEmpty() )
  {
    report.issues.append( { QStringLiteral( "temporal.empty_collection" ),
                             QStringLiteral( "collection has no scenes" ), true, {} } );
    return report;
  }

  // ---- open every scene once (metadata pass) ----
  std::vector<GdalDatasetWrapper> datasets( scenes.size() );
  for ( int i = 0; i < scenes.size(); ++i )
  {
    if ( !datasets[i].open( scenes.at( i ).path ) )
    {
      report.issues.append( { QStringLiteral( "temporal.scene_unreadable" ),
                               QStringLiteral( "cannot open scene %1" ).arg( scenes.at( i ).path ),
                               true, scenes.at( i ).path } );
    }
  }

  // ---- time ----
  for ( const TemporalSceneRef &s : scenes )
  {
    if ( s.time.valid )
      ++report.scenesWithTime;
    else if ( options.requireTimes )
      report.issues.append( { QStringLiteral( "temporal.missing_time" ),
                               QStringLiteral( "scene %1 has no acquisition time "
                                               "(product metadata, descriptor, or explicit input required)" )
                                   .arg( s.path ),
                               true, s.path } );
  }
  for ( int i = 1; i < scenes.size(); ++i )
  {
    if ( scenes.at( i ).time.valid && scenes.at( i - 1 ).time.valid &&
         scenes.at( i ).time.epochMillis == scenes.at( i - 1 ).time.epochMillis )
    {
      ++report.duplicateTimeCount;
      if ( collection.duplicatePolicy() == DuplicatePolicy::Reject )
        report.issues.append( { QStringLiteral( "temporal.duplicate_time" ),
                                 QStringLiteral( "duplicate acquisition instant %1 (%2 / %3) "
                                                 "with reject policy" )
                                     .arg( scenes.at( i ).time.iso, scenes.at( i - 1 ).path,
                                           scenes.at( i ).path ),
                                 true, scenes.at( i ).path } );
      else
        report.issues.append( { QStringLiteral( "temporal.duplicate_time" ),
                                 QStringLiteral( "duplicate acquisition instant %1 kept "
                                                 "(keep_all policy; deterministic input-order tie-break)" )
                                     .arg( scenes.at( i ).time.iso ),
                                 false, scenes.at( i ).path } );
    }
  }
  report.timeRangeStartIso = collection.timeRangeStartIso();
  report.timeRangeEndIso = collection.timeRangeEndIso();

  // ---- spatial (grid) ----
  if ( options.requireSameGrid )
  {
    const GdalDatasetWrapper *ref = nullptr;
    int refIndex = -1;
    for ( int i = 0; i < scenes.size(); ++i )
      if ( datasets[i].isValid() )
      {
        ref = &datasets[i];
        refIndex = i;
        break;
      }
    if ( ref )
    {
      const sicnu::data::RasterGrid refGrid = sicnu::processing::gridFromDataset( *ref );
      for ( int i = 0; i < scenes.size(); ++i )
      {
        if ( i == refIndex || !datasets[i].isValid() )
          continue;
        const sicnu::data::RasterGrid grid = sicnu::processing::gridFromDataset( datasets[i] );
        const sicnu::data::GridCompatReport cmp = sicnu::data::compareGrids( refGrid, grid );
        if ( !cmp.compatible() )
        {
          report.gridCompatible = false;
          const auto blocking = cmp.primaryBlocking();
          report.issues.append( { QStringLiteral( "temporal.grid_mismatch" ),
                                   QStringLiteral( "scene %1 grid is incompatible with %2: %3 "
                                                   "(align explicitly with gdal:reproject/clip "
                                                   "before temporal analysis)" )
                                       .arg( scenes.at( i ).path, scenes.at( refIndex ).path,
                                             blocking ? blocking->message : QString() ),
                                   true, scenes.at( i ).path } );
        }
        if ( datasets[i].width() != ref->width() || datasets[i].height() != ref->height() )
        {
          report.gridCompatible = false;
          report.issues.append( { QStringLiteral( "temporal.dimension_mismatch" ),
                                   QStringLiteral( "scene %1 is %2x%3 but reference %4 is %5x%6" )
                                       .arg( scenes.at( i ).path )
                                       .arg( datasets[i].width() )
                                       .arg( datasets[i].height() )
                                       .arg( scenes.at( refIndex ).path )
                                       .arg( ref->width() )
                                       .arg( ref->height() ),
                                   true, scenes.at( i ).path } );
        }
      }
    }
  }

  // ---- spectral (roles) + radiometric (state, scale/offset) + masks ----
  bool anyState = false;
  bool anyStateMissing = false;
  QString commonState;
  bool stateMismatch = false;
  BandScaleOffset commonScale;
  int declaredScaleScenes = 0;
  int totalScaleChecks = 0;
  bool scaleMismatch = false;
  bool anyNodataDeclared = false;
  report.radiometry.resize( scenes.size() );

  for ( int i = 0; i < scenes.size(); ++i )
  {
    const TemporalSceneRef &s = scenes.at( i );
    const GdalDatasetWrapper &ds = datasets[i];
    if ( !ds.isValid() )
      continue;

    // required roles
    for ( const QString &role : options.requiredBandRoles )
    {
      const auto overrideIt = s.bandOverrides.find( role );
      const int overrideBand = overrideIt != s.bandOverrides.end() ? overrideIt->second : 0;
      const int band = resolveBand( ds, role, overrideBand );
      if ( band <= 0 || band > ds.bandCount() )
        report.issues.append( { QStringLiteral( "temporal.band_role_missing" ),
                                 QStringLiteral( "scene %1 cannot resolve band role '%2'" )
                                     .arg( s.path, role ),
                                 true, s.path } );
    }

    // radiometric state (scenes with no declaration are tolerated but the
    // mix is surfaced — the scale all-or-none gate below is the hard stop)
    SceneRadiometry &rad = report.radiometry[i];
    rad.radiometricState = SatelliteProducts::readRadiometricState( s.path );
    if ( !rad.radiometricState.isEmpty() )
    {
      if ( !anyState )
      {
        anyState = true;
        commonState = rad.radiometricState;
      }
      else if ( commonState != rad.radiometricState )
        stateMismatch = true;
    }
    else
      anyStateMissing = true;

    // scale/offset: verified on EVERY band the operator will read (required
    // roles + the analysis band). Declaration must be all-or-none across all
    // scenes and identical everywhere — mixed calibration is rejected, never
    // half-applied.
    int analysisBand = analysisBandOverride;
    if ( analysisBand <= 0 && !analysisRoleId.isEmpty() )
    {
      const auto overrideIt = s.bandOverrides.find( analysisRoleId );
      const int overrideBand = overrideIt != s.bandOverrides.end() ? overrideIt->second : 0;
      analysisBand = resolveBand( ds, analysisRoleId, overrideBand );
    }
    if ( analysisBand <= 0 )
      analysisBand = 1;
    bool sceneDeclaredAny = false;
    bool sceneUndeclaredAny = false;
    auto checkBandScale = [&]( int band ) {
      if ( band <= 0 || band > ds.bandCount() )
        return;
      const BandScaleOffset so = bandScaleOffset( ds, band );
      ++totalScaleChecks;
      if ( so.defined )
      {
        sceneDeclaredAny = true;
        if ( declaredScaleScenes == 0 && totalScaleChecks == 1 )
          commonScale = so;
        else if ( commonScale.scale != so.scale || commonScale.offset != so.offset )
          scaleMismatch = true;
      }
      else
        sceneUndeclaredAny = true;
      if ( band == analysisBand )
      {
        rad.scaleDefined = so.defined;
        rad.scale = so.scale;
        rad.offset = so.offset;
      }
    };
    for ( const QString &role : options.requiredBandRoles )
    {
      const auto overrideIt = s.bandOverrides.find( role );
      const int overrideBand = overrideIt != s.bandOverrides.end() ? overrideIt->second : 0;
      checkBandScale( resolveBand( ds, role, overrideBand ) );
    }
    checkBandScale( analysisBand );
    if ( sceneDeclaredAny )
    {
      ++declaredScaleScenes;
      if ( sceneUndeclaredAny )
        scaleMismatch = true; // declared and undeclared bands within one scene
    }

    // mask band
    QString maskKind;
    bool unsupportedMaskType = false;
    rad.maskBand = resolveMaskBand( ds, s.maskBand, &maskKind, &unsupportedMaskType );
    rad.maskKind = maskKind;
    if ( unsupportedMaskType )
    {
      report.issues.append( { QStringLiteral( "temporal.mask_unsupported_dtype" ),
                               QStringLiteral( "scene %1: mask band %2 uses a dtype wider than "
                                               "16-bit; masking is disabled for this scene" )
                                   .arg( s.path )
                                   .arg( rad.maskBand ),
                               false, s.path } );
      rad.maskBand = 0;
      rad.maskKind.clear();
    }
    if ( options.expectQualityBands && rad.maskBand == 0 )
      report.issues.append( { QStringLiteral( "temporal.mask_missing" ),
                               QStringLiteral( "scene %1 carries no QA/SCL band; cloud masking "
                                               "will be skipped for it" )
                                   .arg( s.path ),
                               false, s.path } );

    bool hasNodata = false;
    ds.bandNoDataValue( 1, &hasNodata );
    if ( hasNodata )
      anyNodataDeclared = true;
  }

  if ( stateMismatch && options.requireRadiometricConsistency )
    report.issues.append( { QStringLiteral( "temporal.radiometric_mismatch" ),
                             QStringLiteral( "scenes mix different radiometric states "
                                             "(e.g. surface_reflectance vs digital_number); "
                                             "calibrate to a common state first" ),
                             true, {} } );
  report.commonRadiometricState = anyState ? commonState : QString();

  report.scaleOffsetDeclared = declaredScaleScenes > 0;
  report.uniformScaleOffset = !scaleMismatch;
  if ( declaredScaleScenes > 0 )
  {
    report.uniformScale = commonScale.scale;
    report.uniformOffset = commonScale.offset;
  }
  // all-or-none: some scenes (or some bands) declaring scale/offset while
  // others do not is a silent-corruption hazard — reject.
  if ( declaredScaleScenes > 0 && declaredScaleScenes < scenes.size() )
    scaleMismatch = true;
  if ( scaleMismatch && options.requireUniformScaleOffset )
    report.issues.append( { QStringLiteral( "temporal.scale_offset_mismatch" ),
                             QStringLiteral( "scale/offset declarations are inconsistent across "
                                             "the collection (mixed declared/undeclared or "
                                             "differing values); calibrate all scenes to a "
                                             "common state first" ),
                             true, {} } );
  if ( anyState && anyStateMissing )
    report.issues.append( { QStringLiteral( "temporal.radiometric_state_partial" ),
                             QStringLiteral( "some scenes declare SICNU_RADIOMETRIC_STATE and "
                                             "others do not; radiometry may not be comparable" ),
                             false, {} } );
  if ( declaredScaleScenes == 0 )
  {
    // Nothing declared anywhere: raw values are used as-is. Warn once when the
    // data looks like integer DN so users notice the missing calibration.
    for ( int i = 0; i < scenes.size(); ++i )
    {
      if ( datasets[i].isValid() && datasets[i].bandDataType( 1 ) == GDT_UInt16 )
      {
        report.issues.append( { QStringLiteral( "temporal.scale_offset_undeclared" ),
                                 QStringLiteral( "integer scenes without declared scale/offset: "
                                                 "values are treated as-is (no DN→reflectance "
                                                 "guessing; calibrate explicitly if needed)" ),
                                 false, {} } );
        break;
      }
    }
  }
  if ( !anyNodataDeclared )
    report.issues.append( { QStringLiteral( "temporal.nodata_undeclared" ),
                             QStringLiteral( "no scene declares a NoData value; only NaN/inf "
                                             "samples are excluded" ),
                             false, {} } );

  return report;
}

} // namespace sicnu::temporal
