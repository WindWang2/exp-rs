// src/operators/rs/rs_temporal_collection_input.cpp
#include "rs_temporal_collection_input.h"

#include "operators/framework/rs_json_params.h"
#include "processing/algorithms/temporal/temporal_irregular.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "operators/framework/rs_operator_context.h"
#include "processing/algorithms/temporal/temporal_workspace.h"

#include <algorithm>
#include "operators/framework/rs_operator_error.h"

#include <QFile>
#include <QUuid>

#include "data/data_manager.h"

namespace sicnu::operators::rs::temporal_input
{

using namespace params;
using temporal::TemporalCollection;
using temporal::TemporalSceneRef;

namespace
{

void applyGlobalBandOverrides( TemporalCollection &collection, const Json::Value &bands )
{
  if ( !bands.isObject() || bands.empty() )
    return;
  for ( TemporalSceneRef &s : collection.scenes() )
  {
    for ( auto it = bands.begin(); it != bands.end(); ++it )
    {
      const QString role = QString::fromStdString( it.name() );
      const int band = ( *it ).asInt();
      if ( band > 0 )
        s.bandOverrides[role] = band;
    }
  }
}

TemporalCollection fromInlineScenes( const Json::Value &params )
{
  TemporalCollection collection;
  QString err;
  if ( !TemporalCollection::fromInlineScenes(
         params["scenes"], &collection, &err,
         params.isMember( "times" ) ? params["times"] : Json::Value(),
         params.isMember( "bands" ) ? params["bands"] : Json::Value(),
         QStringLiteral( "inline" ) ) )
  {
    throw RSOperatorError( ErrorCode::InvalidParameter, err.toStdString() );
  }

  // Normalize inline scenes against workspace catalog when wired (#725)
  if ( auto *catalog = temporal::workspaceCatalog() )
  {
    temporal::bindCollectionAssets( collection, catalog );
  }

  return collection;
}

} // namespace

temporal::DuplicatePolicy parseDuplicatePolicy( const Json::Value &params )
{
  if ( !params.isMember( "duplicate_policy" ) )
    return temporal::DuplicatePolicy::KeepAll;
  bool ok = false;
  const auto policy = temporal::duplicatePolicyFromString(
    QString::fromStdString( getString( params, "duplicate_policy", "keep_all" ) ), &ok );
  if ( !ok )
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "duplicate_policy must be 'keep_all' or 'reject'" );
  return policy;
}

TemporalCollection parseCollection( const Json::Value &params )
{
  if ( !params.isObject() )
    throw RSOperatorError( ErrorCode::InvalidParameter, "parameters must be a JSON object" );

  TemporalCollection collection;
  // 1. Authoritative workspace collection UUID or descriptor file path takes precedence
  if ( params.isMember( "collection" ) && params["collection"].isString() )
  {
    const QString descriptorPath = QString::fromStdString( params["collection"].asString() );
    // A workspace record id addresses a TemporalCollection registered in the
    // DataManager (project-persistent, revision-identifiable). It takes
    // precedence over the file-path reading because it carries provenance
    // identity; a UUID that does not resolve is a hard error (never a silent
    // reinterpretation as a relative path).
    const QUuid workspaceId( descriptorPath.trimmed() );
    if ( !workspaceId.isNull() )
    {
      const auto id = sicnu::data::CollectionId::fromString( descriptorPath.trimmed() );
      sicnu::data::DataManager *catalog = temporal::workspaceCatalog();
      if ( !id || !catalog )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "'collection' looks like a workspace id but no workspace "
                               "catalog is wired" );
      QString wsErr;
      if ( !temporal::loadCollectionFromWorkspace( *catalog, *id, &collection, &wsErr ) )
        throw RSOperatorError( ErrorCode::InvalidInputData, wsErr.toStdString() );
    }
    else
    {
      if ( !QFile::exists( descriptorPath ) )
        throw RSOperatorError( ErrorCode::FileNotFound,
                               "collection descriptor not found: " + descriptorPath.toStdString() );
      QString err;
      if ( !TemporalCollection::load( descriptorPath, &collection, &err ) )
        throw RSOperatorError( ErrorCode::InvalidInputData, err.toStdString() );
    }
    applyGlobalBandOverrides( collection,
                              params.isMember( "bands" ) ? params["bands"] : Json::Value() );
  }
  // 2. Legacy inline scenes fallback (normalized above)
  else if ( params.isMember( "scenes" ) && params["scenes"].isArray() )
  {
    if ( params["scenes"].empty() )
      throw RSOperatorError( ErrorCode::MissingRequiredParameter,
                             "'scenes' must contain at least one raster" );
    collection = fromInlineScenes( params );
  }
  else
  {
    throw RSOperatorError( ErrorCode::MissingRequiredParameter,
                           "provide 'collection' (authoritative workspace id or descriptor) or 'scenes' (array)" );
  }

  const auto policy = parseDuplicatePolicy( params );
  collection.setDuplicatePolicy( policy );
  QStringList dropped;
  collection.applyDuplicatePolicy( policy, &dropped );
  if ( policy == temporal::DuplicatePolicy::Reject && !dropped.isEmpty() )
  {
    // The user chose 'reject' for explicitness — silently dropping scenes
    // would be the exact opposite. Fail with the offending acquisitions.
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "duplicate acquisition instants with duplicate_policy=reject: " +
                               dropped.join( QStringLiteral( ", " ) ).toStdString() );
  }
  return collection;
}

PreparedTemporalRun prepareTemporalRun( const Json::Value &params, RSOperatorContext &context,
                                        const std::vector<QString> &requiredRoles,
                                        const QString &analysisRole, int analysisBandOverride )
{
  PreparedTemporalRun run;
  run.collection = parseCollection( params );

  temporal::PreflightOptions options;
  for ( const QString &role : requiredRoles )
    if ( !role.isEmpty() )
      options.requiredBandRoles.push_back( role );
  if ( !analysisRole.isEmpty() && !options.requiredBandRoles.contains( analysisRole ) )
    options.requiredBandRoles.push_back( analysisRole );

  run.preflight = temporal::runPreflight( run.collection, options, analysisRole,
                                          analysisBandOverride );
  if ( !run.preflight.ok() )
  {
    const auto blocking = run.preflight.firstBlocking();
    const int blockingCount = static_cast<int>( std::count_if(
      run.preflight.issues.begin(), run.preflight.issues.end(),
      []( const temporal::PreflightIssue &i ) { return i.blocking; } ) );
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "temporal preflight failed [" + blocking.code.toStdString() + "]: " +
                               blocking.message.toStdString() + " (" +
                               std::to_string( blockingCount ) + " blocking issue(s))" );
  }
  for ( const auto &issue : run.preflight.issues )
  {
    if ( !issue.blocking )
      context.logWarning( "[temporal preflight] " + issue.code.toStdString() + ": " +
                          issue.message.toStdString() );
  }

  // NOTE: per-scene band numbers are resolved on the streaming reader (which
  // owns the open dataset handles) via TemporalTileReader::bandForRole —
  // re-opening every scene here would double the metadata pass.
  (void)analysisBandOverride;
  return run;
}


// ---- #1167: provenance channel -------------------------------------------

struct ProvenanceChannel::Impl
{
  std::vector<GdalDatasetWrapper> datasets;
  /// (dataset index, band index) per scene: identity mapping for the
  /// per-scene array form, band s+1 of dataset 0 for the gap-fill
  /// provenance_output artifact form.
  std::vector<std::pair<int, int>> sceneBands;
};

ProvenanceChannel::~ProvenanceChannel() = default;

std::unique_ptr<ProvenanceChannel> ProvenanceChannel::parse(
  const Json::Value &params, const temporal::TemporalCollection &collection,
  int referenceWidth, int referenceHeight )
{
  if ( !params.isMember( "provenance" ) || params["provenance"].isNull() )
    return nullptr;
  const Json::Value &declared = params["provenance"];
  if ( !declared.isArray() || declared.empty() )
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "'provenance' must be a non-empty array of raster paths: the "
                           "rs:temporal_gap_fill provenance_output artifact passed ONCE, or "
                           "one per-scene raster per entry (acquisition-time-sorted order, "
                           "matching the collection)" );
  const int sceneCount = static_cast<int>( collection.scenes().size() );
  auto channel = std::unique_ptr<ProvenanceChannel>( new ProvenanceChannel );
  channel->m_impl = std::make_unique<Impl>();
  channel->m_impl->datasets.resize( declared.size() );
  channel->m_impl->sceneBands.assign(
      static_cast<size_t>( sceneCount ), { 0, 1 } );
  for ( Json::ArrayIndex i = 0; i < declared.size(); ++i )
  {
    if ( !declared[i].isString() || declared[i].asString().empty() )
      throw RSOperatorError( ErrorCode::InvalidParameter,
                             "'provenance[" + std::to_string( i ) + "]' must be a raster path" );
    const QString path = QString::fromStdString( declared[i].asString() );
    if ( !channel->m_impl->datasets[i].open( path ) )
      throw RSOperatorError( ErrorCode::InvalidParameter,
                             "cannot open provenance raster: " + declared[i].asString() );
    if ( channel->m_impl->datasets[i].width() != referenceWidth ||
         channel->m_impl->datasets[i].height() != referenceHeight )
      throw RSOperatorError( ErrorCode::InvalidParameter,
                             "provenance raster '" + declared[i].asString() +
                                 "' does not share the collection grid" );
  }

  // Gap-fill artifact form: ONE multi-band GeoTIFF with a prov_<date> band
  // per scene (rs:temporal_gap_fill provenance_output). Band s+1 is scene s's
  // channel — band names must carry the prov_ prefix so the artifact is
  // identifiable; anything else with sceneCount bands is ambiguous and
  // refused instead of silently reading band 1 for every scene.
  const auto sceneTag = []( const temporal::TemporalSceneRef &scene, int index ) {
      return scene.time.valid ? scene.time.dateString()
                              : QStringLiteral( "scene%1" ).arg( index + 1 );
  };
  if ( declared.size() == 1 )
  {
    const GdalDatasetWrapper &ds = channel->m_impl->datasets[0];
    // The artifact maps band s+1 -> scene s; that mapping is only lawful
    // when the band DATES match this collection (an artifact produced from
    // a different/reordered collection on the same grid would otherwise
    // exclude the wrong dates silently).
    bool datesMatch = ds.bandCount() == sceneCount;
    for ( int s = 0; datesMatch && s < sceneCount; ++s )
      datesMatch = ds.bandDescription( s + 1 ) ==
                   QStringLiteral( "prov_%1" ).arg(
                       sceneTag( collection.scenes().at( s ), s ) );
    if ( datesMatch )
    {
      for ( int s = 0; s < sceneCount; ++s )
        channel->m_impl->sceneBands[static_cast<size_t>( s )] = { 0, s + 1 };
      return channel;
    }
    if ( ds.bandCount() > 1 )
    {
      std::string detail = "band names do not match the collection dates";
      for ( int s = 0; s < sceneCount && s < ds.bandCount(); ++s )
      {
        const QString expected =
            QStringLiteral( "prov_%1" ).arg( sceneTag( collection.scenes().at( s ), s ) );
        if ( ds.bandDescription( s + 1 ) != expected )
        {
          detail = "band " + std::to_string( s + 1 ) + " is '" +
                   ds.bandDescription( s + 1 ).toStdString() + "' but scene " +
                   std::to_string( s ) + " expects '" + expected.toStdString() + "'";
          break;
        }
      }
      throw RSOperatorError(
          ErrorCode::InvalidParameter,
          "'provenance' with one path and " + std::to_string( ds.bandCount() ) +
              " bands requires the rs:temporal_gap_fill provenance_output artifact "
              "for THIS collection (one prov_<date> band per scene): " + detail +
              "; pass one per-scene raster per entry otherwise" );
    }
    // A single 1-band raster for a 1-scene collection: the per-scene form.
  }

  // Per-scene array form: count must match, and a raster carrying a
  // prov_<date> band name must be scene i's OWN channel. Previously the same
  // multi-band artifact repeated per scene silently read band 1 for every
  // scene — excluding the WRONG dates from every statistic.
  if ( static_cast<int>( declared.size() ) != sceneCount )
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "'provenance' declares " + std::to_string( declared.size() ) +
                               " rasters but the collection has " + std::to_string( sceneCount ) +
                               " scenes (pass the provenance_output artifact once, or one "
                               "per-scene raster per scene)" );
  for ( Json::ArrayIndex i = 0; i < declared.size(); ++i )
  {
    channel->m_impl->sceneBands[static_cast<size_t>( i )] = { static_cast<int>( i ), 1 };
    const QString bandName = channel->m_impl->datasets[i].bandDescription( 1 );
    if ( !bandName.startsWith( QLatin1String( "prov_" ) ) )
      continue; // hand-assembled per-scene rasters carry no channel name
    const QString tag = sceneTag( collection.scenes().at( i ), static_cast<int>( i ) );
    if ( bandName != QStringLiteral( "prov_%1" ).arg( tag ) )
      throw RSOperatorError(
          ErrorCode::InvalidParameter,
          "provenance raster '" + declared[i].asString() + "' band 1 is '" +
              bandName.toStdString() + "' but scene " + std::to_string( i ) +
              " is '" + tag.toStdString() +
              "' — pass the provenance_output artifact once (multi-band), or one "
              "per-scene raster per scene in acquisition-time-sorted order" );
  }
  return channel;
}

bool ProvenanceChannel::readKeepMask( int sceneIndex, int x, int y, int w, int h,
                                      std::uint8_t *keep )
{
  if ( !m_impl || sceneIndex < 0
       || sceneIndex >= static_cast<int>( m_impl->sceneBands.size() ) )
    return false;
  const auto [datasetIdx, bandIdx] = m_impl->sceneBands[static_cast<size_t>( sceneIndex )];
  std::vector<float> codes( static_cast<size_t>( w ) * h );
  if ( !m_impl->datasets[static_cast<size_t>( datasetIdx )].readBandWindow(
           bandIdx, x, y, w, h, codes.data() ) )
    return false;
  for ( size_t i = 0; i < codes.size(); ++i )
    keep[i] = codes[i] == static_cast<float>( temporal::SampleProvenance::Observed ) ? 1 : 0;
  return true;
}

} // namespace sicnu::operators::rs::temporal_input
