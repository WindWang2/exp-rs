// src/agent/harness/capability_catalog.cpp
#include "capability_catalog.h"

#include "processing/framework/algorithm_descriptor.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QIODevice>
#include <QProcessEnvironment>
#include <QString>

#include <algorithm>
#include <map>
#include <memory>
#include <set>

namespace sicnu::agent::harness {

namespace {

// --- closed vocabularies (validated at load; wire-visible) ------------------

const char *const kModalities[] = {
  "optical", "sar", "thermal", "dem", "hyperspectral", "temporal", "vector",
  "multimodal",
};
const char *const kDeterminismGrades[] = { "bit_exact", "tolerance" };

// failure_modes[].code reuses the stable typed error taxonomy — D8 never
// invents a second error vocabulary; domain caveats that have no code live in
// limitations/applicability notes instead.
const char *const kFailureModeCodes[] = {
  "BAND_ROLE_UNRESOLVED", "CALIBRATION_MISMATCH", "CANCELLED", "CRS_MISMATCH",
  "DATASET_NOT_FOUND", "EXECUTION_FAILED", "GRID_MISMATCH",
  "INSUFFICIENT_MEMORY", "INVALID_PARAMETER", "INVALID_RADIOMETRY",
  "INVALID_PLAN", "MODALITY_MISMATCH", "MODEL_INCOMPATIBLE",
  "MODEL_NOT_READY", "NOT_SUPPORTED", "OUTPUT_INVALID",
  "POLARIZATION_MISMATCH", "PREFLIGHT_BLOCKED", "TIME_ORDER_INVALID",
  "TRAINING_INVALID",
};

bool inVocabulary( const char *const *vocabulary, size_t size, const std::string &value )
{
  return std::any_of( vocabulary, vocabulary + size,
                      [ &value ]( const char *candidate ) { return value == candidate; } );
}

const std::set<std::string> &knownSidecarKeys()
{
  static const std::set<std::string> kKeys = {
    "id", "schema_version", "display_name", "description", "task", "input",
    "output", "gpu", "accuracy", "notes", "tags", "capability",
  };
  return kKeys;
}

const std::set<std::string> &knownCapabilityKeys()
{
  static const std::set<std::string> kKeys = {
    "schema_version", "family", "operator_group", "summary", "io", "modality",
    "band_roles", "crs", "determinism", "prerequisites", "limitations",
    "failure_modes", "applicability", "teaching_use",
  };
  return kKeys;
}

const std::set<std::string> &knownIoItemKeys()
{
  static const std::set<std::string> kKeys = {
    "name", "data_kind", "type", "required", "array", "item_type", "default",
    "enum", "file_format", "grid_relation", "radiometric_state", "modality",
  };
  return kKeys;
}

const std::set<std::string> &knownDeterminismKeys()
{
  static const std::set<std::string> kKeys = {
    "grade", "stochastic", "memory_policy", "large_raster_safe", "cost_class",
    "side_effects",
  };
  return kKeys;
}

std::string lowered( std::string text )
{
  std::transform( text.begin(), text.end(), text.begin(),
                  []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
  return text;
}

/// Canonical jsoncpp rendering — the same formatter AlgorithmMetaStore uses,
/// so byte comparisons in tests are stable across platforms.
std::string renderJson( const Json::Value &value )
{
  Json::StreamWriterBuilder builder;
  builder[ "indentation" ] = "  ";
  return Json::writeString( builder, value );
}

std::string dataTypeKind( const sicnu::processing::DataType type )
{
  using sicnu::processing::DataType;
  switch ( type )
  {
    case DataType::Raster: return "raster";
    case DataType::Vector: return "vector";
    case DataType::Table: return "table";
    case DataType::Numeric: return "numeric";
    case DataType::Integer: return "integer";
    case DataType::String: return "string";
    case DataType::Boolean: return "boolean";
    case DataType::Enum: return "enum";
    case DataType::BoundingBox: return "bbox";
    case DataType::Crs: return "crs";
    case DataType::Json: return "json";
    case DataType::Any: break;
  }
  return "any";
}

bool isDataKindType( const std::string &kind )
{
  return kind == "raster" || kind == "vector" || kind == "table";
}

/// group()-based fallback for the canonical family map (id map below covers
/// all 111 rs: operators; this keeps future operators classifiable).
std::string familyForGroup( const std::string &group )
{
  static const std::map<std::string, std::string> kGroupFamily = {
    { "sar", "sar" },           { "temporal", "temporal" },
    { "obia", "obia" },         { "terrain", "terrain" },
    { "change-detection", "change" },
    { "classification", "classification" },
    { "spectral", "spectral" },
    { "hyperspectral", "hyperspectral" },
    { "radiometric", "optical" },   { "enhancement", "optical" },
    { "optical", "optical" },       { "masking", "optical" },
    { "fusion", "optical" },        { "qa", "optical" },
    { "ml", "classification" },
    { "features", "spectral" },
    { "raster-vector", "raster_spatial" },
    { "raster", "raster_spatial" },
    { "raster_spatial", "raster_spatial" },
    { "composition", "raster_spatial" },
    { "data-formats", "io" },
  };
  const auto it = kGroupFamily.find( group );
  return it == kGroupFamily.end() ? std::string( "io" ) : it->second;
}

/// Authored id → family map: complete over the 111 rs: operators (the guard
/// test pins totality). One line per operator keeps review diffs readable.
const std::map<std::string, std::string> &familyMap()
{
  static const std::map<std::string, std::string> kMap = {
    // optical: radiometric normalisation, enhancement, fusion, masking, qa
    { "rs:apply_mask", "optical" },
    { "rs:atmospheric_correction", "optical" },
    { "rs:atmospheric_dos1", "optical" },
    { "rs:atmospheric_dos2", "optical" },
    { "rs:atmospheric_quac", "optical" },
    { "rs:contrast_stretch", "optical" },
    { "rs:dn_to_radiance", "optical" },
    { "rs:fusion_brovey", "optical" },
    { "rs:fusion_gram_schmidt", "optical" },
    { "rs:fusion_ihs", "optical" },
    { "rs:fusion_linear", "optical" },
    { "rs:fusion_pca", "optical" },
    { "rs:image_enhancement", "optical" },
    { "rs:image_fusion", "optical" },
    { "rs:qa_mask", "optical" },
    { "rs:radiometric_calibration", "optical" },
    { "rs:topographic_correction", "optical" },
    // spectral: indices, band algebra, derivatives
    { "rs:band_math", "spectral" },
    { "rs:band_ratio", "spectral" },
    { "rs:evi", "spectral" },
    { "rs:extract_bands", "spectral" },
    { "rs:mndwi", "spectral" },
    { "rs:ndbi", "spectral" },
    { "rs:ndvi", "spectral" },
    { "rs:ndwi", "spectral" },
    { "rs:pca", "spectral" },
    { "rs:savi", "spectral" },
    { "rs:spectral_derivative", "spectral" },
    { "rs:spectral_index", "spectral" },
    // sar: everything sar_* (calibration → terrain flattening → texture)
    { "rs:sar_backscatter", "sar" },
    { "rs:sar_calibrate", "sar" },
    { "rs:sar_change", "sar" },
    { "rs:sar_dualpol_features", "sar" },
    { "rs:sar_geocode", "sar" },
    { "rs:sar_ratio", "sar" },
    { "rs:sar_speckle", "sar" },
    { "rs:sar_temporal_stats", "sar" },
    { "rs:sar_terrain_correction", "sar" },
    { "rs:sar_terrain_flatten", "sar" },
    { "rs:sar_terrain_masks", "sar" },
    { "rs:sar_texture", "sar" },
    // terrain
    { "rs:terrain_analysis", "terrain" },
    { "rs:terrain_flow", "terrain" },
    // temporal
    { "rs:temporal_anomaly", "temporal" },
    { "rs:temporal_breakpoints", "temporal" },
    { "rs:temporal_composite", "temporal" },
    { "rs:temporal_decompose", "temporal" },
    { "rs:temporal_extract_series", "temporal" },
    { "rs:temporal_gap_fill", "temporal" },
    { "rs:temporal_harmonic_fit", "temporal" },
    { "rs:temporal_index_series", "temporal" },
    { "rs:temporal_monitor", "temporal" },
    { "rs:temporal_phenology", "temporal" },
    { "rs:temporal_sen_trend", "temporal" },
    { "rs:temporal_smooth", "temporal" },
    { "rs:temporal_summary", "temporal" },
    { "rs:temporal_trend", "temporal" },
    // classification + ml inference
    { "rs:detect", "classification" },
    { "rs:embedding", "classification" },
    { "rs:feature_normalize", "classification" },
    { "rs:feature_select", "classification" },
    { "rs:feature_stack", "classification" },
    { "rs:infer", "classification" },
    { "rs:kmeans_classification", "classification" },
    { "rs:sam_classify", "classification" },
    { "rs:supervised_classification", "classification" },
    // change
    { "rs:change_cva", "change" },
    { "rs:change_cva_angle", "change" },
    { "rs:change_detection", "change" },
    { "rs:change_difference", "change" },
    { "rs:change_irmad", "change" },
    { "rs:change_log_ratio", "change" },
    { "rs:change_mad", "change" },
    { "rs:change_normalized_difference", "change" },
    { "rs:change_ratio", "change" },
    { "rs:change_sam", "change" },
    { "rs:post_classification_change", "change" },
    // obia: segmentation-based object analysis
    { "rs:obia_classify", "obia" },
    { "rs:obia_features", "obia" },
    { "rs:obia_hierarchy", "obia" },
    { "rs:obia_label", "obia" },
    { "rs:obia_segment", "obia" },
    { "rs:segment", "obia" },
    { "rs:segment_stats", "obia" },
    // hyperspectral: detection, unmixing, transforms, resampling
    { "rs:ace", "hyperspectral" },
    { "rs:continuum_removal", "hyperspectral" },
    { "rs:endmember_extraction", "hyperspectral" },
    { "rs:matched_filter", "hyperspectral" },
    { "rs:mnf", "hyperspectral" },
    { "rs:rx_anomaly", "hyperspectral" },
    { "rs:spectral_resample", "hyperspectral" },
    { "rs:spectral_unmixing", "hyperspectral" },
    // raster_spatial: neighbourhood, morphology, grid geometry, zonal
    { "rs:align", "raster_spatial" },
    { "rs:connected_components", "raster_spatial" },
    { "rs:fill_holes", "raster_spatial" },
    { "rs:focal_stats", "raster_spatial" },
    { "rs:local_extrema", "raster_spatial" },
    { "rs:majority_filter", "raster_spatial" },
    { "rs:morphology", "raster_spatial" },
    { "rs:mosaic", "raster_spatial" },
    { "rs:proximity", "raster_spatial" },
    { "rs:rasterize", "raster_spatial" },
    { "rs:recode", "raster_spatial" },
    { "rs:resample", "raster_spatial" },
    { "rs:sieve", "raster_spatial" },
    { "rs:threshold_raster", "raster_spatial" },
    { "rs:zonal_stats", "raster_spatial" },
    // io: sensor import / georeference entry points
    { "rs:landsat_import", "io" },
    { "rs:modis_georeference", "io" },
    { "rs:modis_import", "io" },
    { "rs:sentinel2_import", "io" },
  };
  return kMap;
}

/// Merges authored + derived string lists: descriptor-declared facts first
/// (source of truth), authored extras appended, duplicates dropped once.
Json::Value mergeStringLists( const Json::Value &derived, const Json::Value &authored )
{
  Json::Value out( Json::arrayValue );
  std::set<std::string> seen;
  for ( const Json::Value &item : derived )
  {
    if ( item.isString() && seen.insert( item.asString() ).second )
      out.append( item.asString() );
  }
  for ( const Json::Value &item : authored )
  {
    if ( item.isString() && seen.insert( item.asString() ).second )
      out.append( item.asString() );
  }
  return out;
}

/// One io row from a descriptor port. `contract` is the port's x-rs-contract
/// (may be null). Parameter rows carry type/default/enum; data rows carry
/// data_kind + grid/radiometric contract facts.
Json::Value ioRow( const std::string &kind, const sicnu::processing::PortDescriptor &port )
{
  Json::Value row( Json::objectValue );
  row[ "name" ] = port.name;
  if ( kind == "input" || kind == "output" )
  {
    const std::string contractKind = port.rsContract.isMember( "dataKind" )
      ? port.rsContract[ "dataKind" ].asString()
      : std::string();
    row[ "data_kind" ] =
      contractKind.empty() ? dataTypeKind( port.type ) : lowered( contractKind );
    if ( port.isArray )
    {
      row[ "array" ] = true;
      if ( port.itemType != sicnu::processing::DataType::Any )
        row[ "item_type" ] = dataTypeKind( port.itemType );
    }
    if ( port.rsContract.isMember( "gridRelation" ) )
      row[ "grid_relation" ] = lowered( port.rsContract[ "gridRelation" ].asString() );
    if ( port.rsContract[ "radiometricState" ].isArray() )
      row[ "radiometric_state" ] = port.rsContract[ "radiometricState" ];
    if ( port.rsContract.isMember( "modality" ) )
      row[ "modality" ] = port.rsContract[ "modality" ];
    if ( kind == "output" && !port.fileFormat.empty() )
      row[ "file_format" ] = port.fileFormat;
    row[ "required" ] = port.required;
  }
  else
  {
    row[ "type" ] = dataTypeKind( port.type );
    row[ "required" ] = port.required;
    if ( !port.defaultValue.empty() )
      row[ "default" ] = port.defaultValue;
    if ( !port.enumOptions.empty() )
    {
      Json::Value options( Json::arrayValue );
      for ( const auto &option : port.enumOptions )
        options.append( option );
      row[ "enum" ] = options;
    }
  }
  return row;
}

} // namespace

const std::vector<std::string> &capabilityFamilies()
{
  static const std::vector<std::string> kFamilies = {
    "optical", "spectral", "sar", "terrain", "temporal", "classification",
    "change", "obia", "hyperspectral", "raster_spatial", "io",
  };
  return kFamilies;
}

std::string canonicalFamily( const std::string &operatorId, const std::string &group )
{
  const auto &map = familyMap();
  const auto it = map.find( operatorId );
  if ( it != map.end() )
    return it->second;
  return familyForGroup( group );
}

const std::vector<std::string> &CapabilityCatalog::failureModeCodes()
{
  static const std::vector<std::string> kCodes(
    kFailureModeCodes, kFailureModeCodes + sizeof( kFailureModeCodes ) / sizeof( kFailureModeCodes[0] ) );
  return kCodes;
}

Json::Value deriveCapabilityBlock( const sicnu::processing::AlgorithmDescriptor &desc,
                                   const Json::Value &authored )
{
  using sicnu::processing::DataType;
  const Json::Value authoredCapability =
    authored.isObject() ? authored : Json::Value( Json::objectValue );

  Json::Value block( Json::objectValue );
  block[ "schema_version" ] = 2;
  block[ "family" ] = canonicalFamily( desc.id, desc.group );
  block[ "operator_group" ] = desc.group;

  // io: file-like ports vs scalar parameters, split on the port data type
  // (raster/vector/table = data, everything else = parameter). x-rs-contract
  // facts ride along on the data rows.
  Json::Value inputs( Json::arrayValue );
  Json::Value outputs( Json::arrayValue );
  Json::Value parameters( Json::arrayValue );
  bool sharedGrid = false;
  Json::Value derivedModalities( Json::arrayValue );
  for ( const auto &port : desc.inputs )
  {
    const std::string kind = dataTypeKind( port.type );
    const std::string contractKind =
      port.rsContract.isMember( "dataKind" ) ? lowered( port.rsContract[ "dataKind" ].asString() ) : std::string();
    if ( isDataKindType( kind ) || isDataKindType( contractKind ) ||
         ( port.isArray && port.itemType == DataType::Raster ) )
    {
      inputs.append( ioRow( "input", port ) );
      if ( port.rsContract.isMember( "gridRelation" ) &&
           lowered( port.rsContract[ "gridRelation" ].asString() ) == "same-grid" )
        sharedGrid = true;
    }
    else
    {
      parameters.append( ioRow( "parameter", port ) );
    }
  }
  for ( const auto &port : desc.outputs )
    outputs.append( ioRow( "output", port ) );

  Json::Value io( Json::objectValue );
  io[ "inputs" ] = inputs;
  io[ "outputs" ] = outputs;
  io[ "parameters" ] = parameters;
  block[ "io" ] = io;

  // Modality: authored wins; derived from input contracts, else the family's
  // default input modality.
  if ( authoredCapability[ "modality" ].isArray() && !authoredCapability[ "modality" ].empty() )
  {
    block[ "modality" ] = authoredCapability[ "modality" ];
  }
  else
  {
    std::set<std::string> modalities;
    for ( const auto &port : desc.inputs )
    {
      for ( const Json::Value &m : port.rsContract[ "modality" ] )
        if ( m.isString() )
          modalities.insert( lowered( m.asString() ) );
      if ( port.rsContract.isMember( "modality" ) && port.rsContract[ "modality" ].isString() )
        modalities.insert( lowered( port.rsContract[ "modality" ].asString() ) );
    }
    if ( modalities.empty() )
    {
      const std::string family = block[ "family" ].asString();
      if ( family == "sar" )
        modalities.insert( "sar" );
      else if ( family == "temporal" )
        modalities.insert( "temporal" );
      else if ( family == "terrain" )
        modalities.insert( "dem" );
      else if ( family == "obia" )
        modalities.insert( "optical" );
      else if ( family == "io" )
        modalities.insert( "optical" );
      else
        modalities.insert( "optical" );
    }
    for ( const auto &modality : modalities )
      derivedModalities.append( modality );
    block[ "modality" ] = derivedModalities;
  }

  // band_roles: authored (seeded from the preflight knowledge mirror at
  // authoring time) — no code path can derive them for every operator.
  block[ "band_roles" ] =
    authoredCapability[ "band_roles" ].isObject() ? authoredCapability[ "band_roles" ]
                                                  : Json::Value( Json::objectValue );

  // crs: requires_projected authored (default false); requires_shared_grid
  // always derived from the contracts — never hand-written.
  Json::Value crs( Json::objectValue );
  const Json::Value &authoredCrs = authoredCapability[ "crs" ];
  crs[ "requires_projected" ] =
    authoredCrs.isMember( "requires_projected" ) && authoredCrs[ "requires_projected" ].isBool()
      ? authoredCrs[ "requires_projected" ].asBool()
      : false;
  crs[ "requires_shared_grid" ] = sharedGrid;
  block[ "crs" ] = crs;

  // determinism: ADR 0124 grade + kernel facts from the descriptor.
  const auto &meta = desc.agentMetadata;
  Json::Value determinism( Json::objectValue );
  determinism[ "grade" ] = meta.determinismGrade.empty() ? "bit_exact" : meta.determinismGrade;
  determinism[ "stochastic" ] = !meta.deterministic;
  determinism[ "memory_policy" ] = meta.memoryPolicy.empty() ? "full_raster" : meta.memoryPolicy;
  determinism[ "large_raster_safe" ] = meta.largeRasterSafe;
  if ( !meta.costClass.empty() )
    determinism[ "cost_class" ] = meta.costClass;
  determinism[ "side_effects" ] = meta.sideEffects;
  block[ "determinism" ] = determinism;

  // prerequisites/limitations: descriptor list first, authored extras appended.
  Json::Value derivedPrereqs( Json::arrayValue );
  for ( const auto &prereq : meta.prerequisites )
    derivedPrereqs.append( prereq );
  block[ "prerequisites" ] =
    mergeStringLists( derivedPrereqs, authoredCapability[ "prerequisites" ] );

  Json::Value derivedLimits( Json::arrayValue );
  for ( const auto &limit : meta.limitations )
    derivedLimits.append( limit );
  block[ "limitations" ] = mergeStringLists( derivedLimits, authoredCapability[ "limitations" ] );

  // Authored-only enrichment: passthrough, empty scaffolding when absent so
  // the schema stays uniform across all 111 sidecars.
  block[ "summary" ] = authoredCapability[ "summary" ].isString()
    ? authoredCapability[ "summary" ]
    : Json::Value( "" );
  block[ "failure_modes" ] = authoredCapability[ "failure_modes" ].isArray()
    ? authoredCapability[ "failure_modes" ]
    : Json::Value( Json::arrayValue );
  block[ "applicability" ] = authoredCapability[ "applicability" ].isObject()
    ? authoredCapability[ "applicability" ]
    : Json::Value( Json::objectValue );
  block[ "teaching_use" ] = authoredCapability[ "teaching_use" ].isObject()
    ? authoredCapability[ "teaching_use" ]
    : Json::Value( Json::objectValue );

  return block;
}

// --- CapabilityCatalog ------------------------------------------------------

CapabilityCatalog &CapabilityCatalog::instance()
{
  static CapabilityCatalog catalog;
  return catalog;
}

void CapabilityCatalog::setDirectory( const std::string &directory )
{
  mDirectory = directory;
  mLoaded = false;
}

std::string CapabilityCatalog::directory() const
{
  return mDirectory.empty() ? defaultDirectory() : mDirectory;
}

std::string CapabilityCatalog::defaultDirectory() const
{
  const QString envDir = QProcessEnvironment::systemEnvironment().value(
    QStringLiteral( "SICNU_CAPABILITY_META_DIR" ) );
  if ( !envDir.isEmpty() )
    return envDir.toStdString();

  const QDir cwdCapability(
    QDir::current().filePath( QStringLiteral( "data/processing/algorithm_meta/capability" ) ) );
  if ( cwdCapability.exists() )
    return cwdCapability.absolutePath().toStdString();

  if ( QCoreApplication::instance() )
  {
    const QDir appCapability( QCoreApplication::applicationDirPath()
                              + QStringLiteral( "/../data/processing/algorithm_meta/capability" ) );
    if ( appCapability.exists() )
      return appCapability.absolutePath().toStdString();
  }

#ifdef SICNU_SOURCE_DIR
  {
    const QDir sourceCapability( QDir( QString::fromUtf8( SICNU_SOURCE_DIR ) ).filePath(
      QStringLiteral( "data/processing/algorithm_meta/capability" ) ) );
    if ( sourceCapability.exists() )
      return sourceCapability.absolutePath().toStdString();
  }
#endif
  return std::string();
}

int CapabilityCatalog::reload()
{
  const std::string dir = directory();
  mEntries = Json::Value( Json::objectValue );
  mOrder.clear();
  mLoadProblems.clear();
  mLoaded = true;

  if ( !QDir( QString::fromStdString( dir ) ).exists() )
  {
    mLoadProblems.push_back( "capability meta directory not found: " + dir );
    return 0;
  }
  QDirIterator it( QString::fromStdString( dir ), { QStringLiteral( "*.json" ) }, QDir::Files );
  while ( it.hasNext() )
  {
    it.next();
    // The relation graph lives beside the sidecars but is not one of them.
    if ( it.fileName() == QStringLiteral( "capability_relations.json" ) )
      continue;
    QFile file( it.filePath() );
    if ( !file.open( QIODevice::ReadOnly ) )
    {
      mLoadProblems.push_back( it.fileName().toStdString() + ": unreadable" );
      continue;
    }
    const QByteArray raw = file.readAll();
    Json::Value parsed;
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    std::string errors;
    if ( !reader->parse( raw.constData(), raw.constData() + raw.size(), &parsed, &errors ) )
    {
      mLoadProblems.push_back( it.fileName().toStdString() + ": " + errors );
      continue;
    }
    const std::string where = it.fileName().toStdString();
    for ( const std::string &problem : validateEntry( parsed ) )
      mLoadProblems.push_back( where + ": " + problem );
    const std::string id = parsed[ "id" ].asString();
    if ( id.empty() )
      continue;
    const bool problems = std::any_of(
      mLoadProblems.begin(), mLoadProblems.end(),
      [ &where ]( const std::string &p ) { return p.rfind( where, 0 ) == 0; } );
    if ( problems )
      continue;
    if ( mEntries.isMember( id ) )
    {
      mLoadProblems.push_back( where + ": duplicate entry id " + id );
      continue;
    }
    mEntries[ id ] = parsed;
    mOrder.push_back( id );
  }
  std::sort( mOrder.begin(), mOrder.end() );
  return static_cast<int>( mOrder.size() );
}

bool CapabilityCatalog::loaded() const
{
  return mLoaded;
}

std::vector<std::string> CapabilityCatalog::loadProblems() const
{
  return mLoadProblems;
}

std::vector<std::string> CapabilityCatalog::entryIds() const
{
  return mOrder;
}

bool CapabilityCatalog::hasEntry( const std::string &operatorId ) const
{
  return mEntries.isMember( operatorId );
}

Json::Value CapabilityCatalog::entry( const std::string &operatorId ) const
{
  const Json::Value &value = mEntries[ operatorId ];
  return value.isNull() ? Json::Value() : value;
}

Json::Value CapabilityCatalog::capability( const std::string &operatorId ) const
{
  const Json::Value &value = mEntries[ operatorId ];
  if ( value.isNull() )
    return Json::Value( Json::objectValue );
  const Json::Value &block = value[ "capability" ];
  return block.isObject() ? block : Json::Value( Json::objectValue );
}

std::vector<std::string> CapabilityCatalog::byFamily( const std::string &family ) const
{
  std::vector<std::string> ids;
  for ( const std::string &id : mOrder )
  {
    if ( capability( id ).get( "family", "" ).asString() == family )
      ids.push_back( id );
  }
  return ids;
}

std::vector<std::string> CapabilityCatalog::byInputModality( const std::string &modality ) const
{
  const std::string wanted = lowered( modality );
  std::vector<std::string> ids;
  for ( const std::string &id : mOrder )
  {
    // Named copy: iterating capability(id)["modality"] directly would walk a
    // subobject of a temporary destroyed at the end of the range-init.
    const Json::Value block = capability( id );
    for ( const Json::Value &candidate : block[ "modality" ] )
    {
      if ( candidate.isString() && lowered( candidate.asString() ) == wanted )
      {
        ids.push_back( id );
        break;
      }
    }
  }
  return ids;
}

std::string CapabilityCatalog::determinismOf( const std::string &operatorId ) const
{
  const Json::Value grade = capability( operatorId ).get( "determinism", Json::Value() ).get(
    "grade", Json::Value() );
  return grade.isString() ? grade.asString() : std::string();
}

bool CapabilityCatalog::isStochastic( const std::string &operatorId ) const
{
  const Json::Value stochastic = capability( operatorId ).get( "determinism", Json::Value() ).get(
    "stochastic", Json::Value() );
  return stochastic.isBool() && stochastic.asBool();
}

std::vector<std::string> CapabilityCatalog::stochasticOperators() const
{
  std::vector<std::string> ids;
  for ( const std::string &id : mOrder )
  {
    if ( isStochastic( id ) )
      ids.push_back( id );
  }
  return ids;
}

bool CapabilityCatalog::requiresGrid( const std::string &operatorId ) const
{
  const Json::Value shared = capability( operatorId ).get( "crs", Json::Value() ).get(
    "requires_shared_grid", Json::Value() );
  return shared.isBool() && shared.asBool();
}

Json::Value CapabilityCatalog::manifestPage( const std::string &family, int page,
                                             int pageSize ) const
{
  std::vector<std::string> ids =
    family.empty() ? mOrder : byFamily( family );
  if ( pageSize < 1 )
    pageSize = 1;
  if ( pageSize > 64 )
    pageSize = 64;
  const int total = static_cast<int>( ids.size() );
  int pageCount = ( total + pageSize - 1 ) / pageSize;
  if ( pageCount < 1 )
    pageCount = 1;
  if ( page < 1 )
    page = 1;
  if ( page > pageCount )
    page = pageCount;

  auto rendered = [&]( int firstIndex, int lastIndex ) {
    Json::Value pageValue( Json::objectValue );
    pageValue[ "family" ] = family.empty() ? "all" : family;
    pageValue[ "page" ] = page;
    pageValue[ "page_count" ] = pageCount;
    pageValue[ "total" ] = total;
    Json::Value entries( Json::arrayValue );
    for ( int index = firstIndex; index < lastIndex; ++index )
    {
      const Json::Value block = capability( ids[ static_cast<size_t>( index ) ] );
      Json::Value summary( Json::objectValue );
      summary[ "id" ] = ids[ static_cast<size_t>( index ) ];
      summary[ "family" ] = block[ "family" ];
      if ( block[ "summary" ].isString() && !block[ "summary" ].asString().empty() )
        summary[ "summary" ] = block[ "summary" ];
      summary[ "modality" ] = block[ "modality" ];
      summary[ "band_roles" ] = block[ "band_roles" ];
      summary[ "determinism" ] = block[ "determinism" ];
      summary[ "io" ] = block[ "io" ];
      if ( block[ "failure_modes" ].isArray() && !block[ "failure_modes" ].empty() )
        summary[ "failure_modes" ] = block[ "failure_modes" ];
      if ( block[ "applicability" ].isObject() && !block[ "applicability" ].empty() )
        summary[ "applicability" ] = block[ "applicability" ];
      if ( block[ "teaching_use" ].isObject() && !block[ "teaching_use" ].empty() )
        summary[ "teaching_use" ] = block[ "teaching_use" ];
      entries.append( summary );
    }
    pageValue[ "entries" ] = entries;
    pageValue[ "budget_bytes" ] = static_cast<Json::UInt64>( kManifestBudgetBytes );
    return pageValue;
  };

  // Trim the tail entries until the page fits the hard 64 KiB budget.
  int lastIndex = std::min( page * pageSize, total );
  int firstIndex = ( page - 1 ) * pageSize;
  if ( lastIndex < firstIndex )
    lastIndex = firstIndex;
  Json::Value pageValue = rendered( firstIndex, lastIndex );
  while ( renderJson( pageValue ).size() > kManifestBudgetBytes && lastIndex > firstIndex )
  {
    --lastIndex;
    pageValue = rendered( firstIndex, lastIndex );
  }
  return pageValue;
}

Json::Value CapabilityCatalog::errorCatalog() const
{
  std::map<std::string, std::set<std::string>> byCode;
  for ( const std::string &id : mOrder )
  {
    // Named copy — see byInputModality: subscripting a temporary and ranging
    // over the subobject would read freed memory.
    const Json::Value block = capability( id );
    for ( const Json::Value &failure : block[ "failure_modes" ] )
    {
      if ( failure.isObject() && failure[ "code" ].isString() )
        byCode[ failure[ "code" ].asString() ].insert( id );
    }
  }
  auto rendered = []( const std::map<std::string, std::set<std::string>> &codes,
                      const std::set<std::string> &skip ) {
    Json::Value catalog( Json::objectValue );
    Json::Value codeArray( Json::arrayValue );
    for ( const auto &[ code, operators ] : codes )
    {
      if ( skip.count( code ) )
        continue;
      Json::Value row( Json::objectValue );
      row[ "code" ] = code;
      Json::Value idArray( Json::arrayValue );
      for ( const auto &id : operators )
        idArray.append( id );
      row[ "operators" ] = idArray;
      codeArray.append( row );
    }
    catalog[ "codes" ] = codeArray;
    catalog[ "budget_bytes" ] = static_cast<Json::UInt64>( kErrorCatalogBudgetBytes );
    return catalog;
  };

  std::set<std::string> skipped;
  Json::Value catalog = rendered( byCode, skipped );
  while ( renderJson( catalog ).size() > kErrorCatalogBudgetBytes && !byCode.empty() )
  {
    // Deterministic degradation: drop the highest code (largest operator
    // lists go last) until the catalog fits, with an explicit marker.
    skipped.insert( byCode.rbegin()->first );
    catalog = rendered( byCode, skipped );
    catalog[ "truncated" ] = true;
  }
  return catalog;
}

std::vector<std::string> CapabilityCatalog::validateEntry( const Json::Value &sidecar )
{
  std::vector<std::string> problems;
  if ( !sidecar.isObject() )
  {
    problems.push_back( "sidecar must be an object" );
    return problems;
  }
  for ( const std::string &key : sidecar.getMemberNames() )
  {
    if ( !knownSidecarKeys().count( key ) )
      problems.push_back( "unknown key '" + key + "'" );
  }
  const std::string id = sidecar[ "id" ].isString() ? sidecar[ "id" ].asString() : std::string();
  if ( id.empty() )
    problems.push_back( "missing 'id'" );
  const Json::Value &block = sidecar[ "capability" ];
  if ( !block.isObject() )
  {
    problems.push_back( "missing 'capability' block" );
    return problems;
  }
  if ( sidecar[ "schema_version" ].asInt() != 2 )
    problems.push_back( "schema_version must be 2" );

  for ( const std::string &key : block.getMemberNames() )
  {
    if ( !knownCapabilityKeys().count( key ) )
      problems.push_back( "capability: unknown key '" + key + "'" );
  }
  const std::string family = block[ "family" ].isString() ? block[ "family" ].asString() : std::string();
  if ( !std::any_of( capabilityFamilies().begin(), capabilityFamilies().end(),
                     [ &family ]( const std::string &candidate ) { return candidate == family; } ) )
    problems.push_back( "capability: unknown family '" + family + "'" );
  if ( !canonicalFamily( id, block[ "operator_group" ].isString()
                                   ? block[ "operator_group" ].asString()
                                   : std::string() )
            .empty() )
  {
    // The authored family must agree with the canonical map — pages and the
    // query API route by family, so a disagreeing sidecar would silently
    // land in two families.
    const std::string canonical = canonicalFamily(
      id, block[ "operator_group" ].isString() ? block[ "operator_group" ].asString()
                                               : std::string() );
    if ( canonical != family )
      problems.push_back( "capability: family '" + family + "' disagrees with canonical map ('" +
                          canonical + "')" );
  }

  // io
  const Json::Value &io = block[ "io" ];
  if ( !io.isObject() )
    problems.push_back( "capability: missing io" );
  else
  {
    const auto checkRows = [ &problems ]( const Json::Value &rows, const std::string &field ) {
      if ( !rows.isArray() )
      {
        problems.push_back( "capability.io: '" + field + "' must be an array" );
        return;
      }
      for ( const Json::Value &row : rows )
      {
        if ( !row.isObject() )
        {
          problems.push_back( "capability.io." + field + ": row must be an object" );
          continue;
        }
        for ( const std::string &rowKey : row.getMemberNames() )
        {
          if ( !knownIoItemKeys().count( rowKey ) )
            problems.push_back( "capability.io." + field + ": unknown key '" + rowKey + "'" );
        }
        if ( !row[ "name" ].isString() || row[ "name" ].asString().empty() )
          problems.push_back( "capability.io." + field + ": row needs a name" );
      }
    };
    checkRows( io[ "inputs" ], "inputs" );
    checkRows( io[ "outputs" ], "outputs" );
    checkRows( io[ "parameters" ], "parameters" );
  }

  // modality
  if ( !block[ "modality" ].isArray() || block[ "modality" ].empty() )
    problems.push_back( "capability: modality must be a non-empty array" );
  for ( const Json::Value &modality : block[ "modality" ] )
  {
    if ( !modality.isString() ||
         !inVocabulary( kModalities, sizeof( kModalities ) / sizeof( kModalities[0] ),
                        modality.asString() ) )
      problems.push_back( "capability: unknown modality" );
  }

  // band_roles
  const Json::Value &bandRoles = block[ "band_roles" ];
  if ( !bandRoles.isObject() )
    problems.push_back( "capability: band_roles must be an object" );
  else
  {
    for ( const std::string &role : bandRoles.getMemberNames() )
    {
      if ( !bandRoles[ role ].isInt() || bandRoles[ role ].asInt() < 1 )
        problems.push_back( "capability: band role '" + role + "' needs a positive integer count" );
    }
  }

  // crs
  const Json::Value &crs = block[ "crs" ];
  if ( !crs.isObject() || !crs[ "requires_projected" ].isBool() ||
       !crs[ "requires_shared_grid" ].isBool() )
    problems.push_back( "capability: crs needs boolean requires_projected/requires_shared_grid" );

  // determinism
  const Json::Value &determinism = block[ "determinism" ];
  if ( !determinism.isObject() )
  {
    problems.push_back( "capability: missing determinism" );
  }
  else
  {
    for ( const std::string &key : determinism.getMemberNames() )
    {
      if ( !knownDeterminismKeys().count( key ) )
        problems.push_back( "capability.determinism: unknown key '" + key + "'" );
    }
    const std::string grade = determinism[ "grade" ].isString() ? determinism[ "grade" ].asString()
                                                                : std::string();
    if ( !inVocabulary( kDeterminismGrades,
                        sizeof( kDeterminismGrades ) / sizeof( kDeterminismGrades[0] ), grade ) )
      problems.push_back( "capability.determinism: unknown grade '" + grade + "'" );
    if ( !determinism[ "stochastic" ].isBool() )
      problems.push_back( "capability.determinism: stochastic must be boolean" );
  }

  // failure_modes
  if ( !block[ "failure_modes" ].isArray() )
    problems.push_back( "capability: failure_modes must be an array" );
  else
  {
    for ( const Json::Value &failure : block[ "failure_modes" ] )
    {
      if ( !failure.isObject() )
      {
        problems.push_back( "capability: failure_modes entries must be objects" );
        continue;
      }
      for ( const std::string &key : failure.getMemberNames() )
      {
        if ( key != "code" && key != "when" && key != "remedy" )
          problems.push_back( "capability.failure_modes: unknown key '" + key + "'" );
      }
      const std::string code = failure[ "code" ].isString() ? failure[ "code" ].asString()
                                                            : std::string();
      if ( !std::any_of( failureModeCodes().begin(), failureModeCodes().end(),
                         [ &code ]( const std::string &candidate ) { return candidate == code; } ) )
        problems.push_back( "capability.failure_modes: unknown code '" + code + "'" );
      if ( !failure[ "when" ].isString() || !failure[ "remedy" ].isString() )
        problems.push_back( "capability.failure_modes: entries need when/remedy strings" );
    }
  }

  // prerequisites / limitations / summary
  for ( const char *field : { "prerequisites", "limitations" } )
  {
    if ( !block[ field ].isArray() )
      problems.push_back( std::string( "capability: " ) + field + " must be an array" );
    else
      for ( const Json::Value &item : block[ field ] )
        if ( !item.isString() )
          problems.push_back( std::string( "capability: " ) + field + " entries must be strings" );
  }
  if ( !block[ "summary" ].isString() )
    problems.push_back( "capability: summary must be a string" );

  // applicability / teaching_use
  const Json::Value &applicability = block[ "applicability" ];
  if ( !applicability.isObject() )
    problems.push_back( "capability: applicability must be an object" );
  else
  {
    for ( const std::string &key : applicability.getMemberNames() )
    {
      if ( key != "land_cover" && key != "scenes" && key != "notes" )
        problems.push_back( "capability.applicability: unknown key '" + key + "'" );
      else if ( key == "notes" && !applicability[ "notes" ].isString() )
        problems.push_back( "capability.applicability: notes must be a string" );
      else if ( key != "notes" && !applicability[ key ].isArray() )
        problems.push_back( "capability.applicability: " + key + " must be an array" );
    }
  }
  const Json::Value &teaching = block[ "teaching_use" ];
  if ( !teaching.isObject() )
    problems.push_back( "capability: teaching_use must be an object" );
  else
  {
    for ( const std::string &key : teaching.getMemberNames() )
    {
      if ( key != "concepts" && key != "courses" && key != "exercise" )
        problems.push_back( "capability.teaching_use: unknown key '" + key + "'" );
      else if ( key == "exercise" && !teaching[ "exercise" ].isString() )
        problems.push_back( "capability.teaching_use: exercise must be a string" );
      else if ( key != "exercise" && !teaching[ key ].isArray() )
        problems.push_back( "capability.teaching_use: " + key + " must be an array" );
    }
  }

  return problems;
}

} // namespace sicnu::agent::harness
