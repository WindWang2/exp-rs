// src/agent/harness/capability_knowledge.cpp
#include "capability_knowledge.h"

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

// --- closed vocabularies (validated at load; extended only with a drift-test
// --- update — these strings are wire-visible through tool outputs) ----------

const char *const kModalities[] = {
  "optical", "sar", "thermal", "dem", "vector", "tabular", "multimodal",
  // Platform 8.0: temporal model inputs (time-series / STAC series feeds).
  "temporal",
};
const char *const kBandRoles[] = {
  "blue", "green", "red", "red_edge", "nir", "swir", "swir1", "swir2", "pan",
  "thermal", "vv", "vh", "hh", "hv", "dem", "index", "mask", "rgb",
};
const char *const kCostClasses[] = { "light", "medium", "heavy" };
const char *const kVerificationChecks[] = {
  "existence", "opens", "crs_present", "crs_matches", "dimensions",
  "finite_fraction", "nodata_fraction", "class_domain", "feature_count",
  "provenance", "extent", "uncertainty",
};
const char *const kArifactKinds[] = { "raster", "vector", "table", "json", "collection" };

bool inVocabulary( const char *const *vocabulary, size_t size, const std::string &value )
{
  return std::any_of( vocabulary, vocabulary + size,
                      [ &value ]( const char *candidate ) { return value == candidate; } );
}

std::string lowered( std::string text )
{
  std::transform( text.begin(), text.end(), text.begin(),
                  []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
  return text;
}

/// Known top-level keys; anything else is rejected so a renamed field cannot
/// silently carry a fact no consumer reads.
const std::set<std::string> &knownKeys()
{
  static const std::set<std::string> kKeys = {
    "id", "kind", "family", "extends", "intents", "variants", "modality",
    "band_roles", "radiometric", "crs", "sar", "temporal", "model_compatibility",
    "resource", "side_effects", "artifacts", "verification", "applicability",
    "limitations", "when", "surface",
  };
  return kKeys;
}

/// Harness 8.0: which live registry an entry's id must resolve against.
/// "operator" (default) = RSOperatorRegistry; "spatial_tool" =
/// SpatialToolRegistry; "data_platform_tool" = dataPlatformToolDefs().
/// The drift test cross-checks each surface against its authoritative
/// registry so knowledge can never name a tool that does not exist.
const std::set<std::string> &knownSurfaces()
{
  static const std::set<std::string> kSurfaces = {
    "operator", "spatial_tool", "data_platform_tool",
  };
  return kSurfaces;
}

} // namespace

CapabilityKnowledge &CapabilityKnowledge::instance()
{
  static CapabilityKnowledge knowledge;
  return knowledge;
}

void CapabilityKnowledge::setDirectory( const std::string &directory )
{
  mDirectory = directory;
  mLoaded = false;
}

std::string CapabilityKnowledge::directory() const
{
  return mDirectory.empty() ? defaultDirectory() : mDirectory;
}

std::string CapabilityKnowledge::defaultDirectory() const
{
  const QString envDir = QProcessEnvironment::systemEnvironment().value(
    QStringLiteral( "SICNU_CAPABILITIES_DIR" ) );
  if ( !envDir.isEmpty() )
    return envDir.toStdString();

  const QDir cwdCapabilities(
    QDir::current().filePath( QStringLiteral( "data/agent/capabilities" ) ) );
  if ( cwdCapabilities.exists() )
    return cwdCapabilities.absolutePath().toStdString();

  if ( QCoreApplication::instance() )
  {
    const QDir appCapabilities( QCoreApplication::applicationDirPath()
                                + QStringLiteral( "/../data/agent/capabilities" ) );
    if ( appCapabilities.exists() )
      return appCapabilities.absolutePath().toStdString();
  }

#ifdef SICNU_SOURCE_DIR
  {
    const QDir sourceCapabilities( QDir( QString::fromUtf8( SICNU_SOURCE_DIR ) ).filePath(
      QStringLiteral( "data/agent/capabilities" ) ) );
    if ( sourceCapabilities.exists() )
      return sourceCapabilities.absolutePath().toStdString();
  }
#endif
  return std::string();
}

int CapabilityKnowledge::reload()
{
  const std::string dir = directory();
  mEntries = Json::Value( Json::objectValue );
  mFamilyDefaults = Json::Value( Json::objectValue );
  mOrder.clear();
  mLoadProblems.clear();
  mLoaded = true;

  // A missing directory is a deployment problem, not an empty catalog —
  // surface it so tools can distinguish "no knowledge" from "broken install".
  if ( !QDir( QString::fromStdString( dir ) ).exists() )
  {
    mLoadProblems.push_back( "capabilities directory not found: " + dir );
    return 0;
  }
  QDirIterator it( QString::fromStdString( dir ), { QStringLiteral( "*.json" ) }, QDir::Files );
  while ( it.hasNext() )
  {
    it.next();
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

    const bool isArray = parsed.isArray();
    const bool isSingle = parsed.isObject();
    if ( !isArray && !isSingle )
    {
      mLoadProblems.push_back( it.fileName().toStdString() + ": entry or entry array expected" );
      continue;
    }
    Json::Value singleWrap( Json::arrayValue );
    if ( isSingle )
      singleWrap.append( parsed );
    const Json::Value &list = isSingle ? singleWrap : parsed;

    int index = 0;
    for ( const Json::Value &entry : list )
    {
      ++index;
      const std::string where = isArray
        ? it.fileName().toStdString() + "[" + std::to_string( index ) + "]"
        : it.fileName().toStdString();
      for ( const std::string &problem : validateEntry( entry ) )
        mLoadProblems.push_back( where + ": " + problem );
      const std::string id = entry.get( "id", "" ).asString();
      if ( id.empty() )
        continue;
      const bool problems =
        std::any_of( mLoadProblems.begin(), mLoadProblems.end(),
                     [ &where ]( const std::string &p ) { return p.rfind( where, 0 ) == 0; } );
      if ( problems )
        continue;
      if ( entry.get( "kind", "" ).asString() == "family_default" )
      {
        if ( mFamilyDefaults.isMember( id ) )
          mLoadProblems.push_back( where + ": duplicate family_default id " + id );
        else
          mFamilyDefaults[ id ] = entry;
      }
      else if ( mEntries.isMember( id ) )
      {
        mLoadProblems.push_back( where + ": duplicate entry id " + id );
      }
      else
      {
        mEntries[ id ] = entry;
        mOrder.push_back( id );
      }
    }
  }
  // Deterministic order: directory iteration order is filesystem-dependent,
  // and operatorsForIntent/candidates use mOrder as a tiebreak.
  std::sort( mOrder.begin(), mOrder.end() );
  return static_cast<int>( mOrder.size() );
}

std::vector<std::string> CapabilityKnowledge::loadProblems() const
{
  return mLoadProblems;
}

namespace {

/// The knowledge layer auto-loads on first query (same contract as
/// RecipeCatalog::recipe) so tool paths never answer from an empty catalog.
void ensureLoaded( CapabilityKnowledge &knowledge )
{
  if ( !knowledge.loaded() )
    knowledge.reload();
}

} // namespace

Json::Value CapabilityKnowledge::mergedEntry( const Json::Value &raw,
                                              const Json::Value &variantParams ) const
{
  // Bounded extends chain: explicit parent first, then the family default.
  // Depth cap 4 keeps malformed graphs from looping.
  std::vector<const Json::Value *> chain;
  chain.push_back( &raw );
  const Json::Value *cursor = &raw;
  for ( int depth = 0; depth < 4; ++depth )
  {
    std::string parentId = cursor->get( "extends", "" ).asString();
    if ( parentId.empty() )
    {
      const std::string family = cursor->get( "family", "" ).asString();
      if ( family.empty() )
        break;
      parentId = std::string( "family:" ) + family;
    }
    const Json::Value *parent = nullptr;
    if ( parentId.rfind( "family:", 0 ) == 0 )
    {
      // Family defaults are stored under their full id ("family:<name>").
      const Json::Value &fallback = mFamilyDefaults[ parentId ];
      parent = fallback.isNull() ? nullptr : &fallback;
    }
    else
    {
      parent = mEntries[ parentId ].isNull() ? nullptr : &mEntries[ parentId ];
    }
    if ( !parent )
      break;
    chain.push_back( parent );
    cursor = parent;
  }

  Json::Value merged( Json::objectValue );
  // chain = [raw, parent, grandparent, ...]. Processing from the entry toward
  // its deepest ancestor with "first writer wins" gives the entry's own keys
  // priority and lets each explicit `extends` override the family default.
  for ( const Json::Value *source : chain )
  {
    for ( const std::string &key : source->getMemberNames() )
    {
      if ( key == "extends" || key == "id" || key == "kind" || key == "when" )
        continue;
      if ( !merged.isMember( key ) )
        merged[ key ] = source->get( key, Json::Value() );
    }
  }

  // Variant overrides: first variant whose "when" matches wins wholesale for
  // the keys it declares (top-level replace — variants stay small and whole).
  if ( variantParams.isObject() )
  {
    for ( const Json::Value &variant : raw.get( "variants", Json::Value( Json::arrayValue ) ) )
    {
      const Json::Value &when = variant.get( "when", Json::Value() );
      const std::string param = when.get( "param", "" ).asString();
      if ( param.empty() || !variantParams.isMember( param ) )
        continue;
      const std::string actual = lowered( variantParams.get( param, "" ).asString() );
      bool matches = false;
      for ( const Json::Value &candidate : when.get( "values", Json::Value( Json::arrayValue ) ) )
      {
        if ( candidate.isString() && lowered( candidate.asString() ) == actual )
        {
          matches = true;
          break;
        }
      }
      if ( !matches )
        continue;
      for ( const std::string &key : variant.getMemberNames() )
      {
        if ( key == "when" )
          continue;
        merged[ key ] = variant.get( key, Json::Value() );
      }
      break;
    }
  }
  return merged;
}

Json::Value CapabilityKnowledge::entryForOperator( const std::string &operatorId,
                                                   const Json::Value &variantParams ) const
{
  ensureLoaded( const_cast<CapabilityKnowledge &>( *this ) );
  const Json::Value &raw = mEntries[ operatorId ];
  if ( raw.isNull() )
    return Json::Value();
  return mergedEntry( raw, variantParams.isObject() ? variantParams : Json::Value() );
}

Json::Value CapabilityKnowledge::rawEntry( const std::string &operatorId ) const
{
  const Json::Value &raw = mEntries[ operatorId ];
  return raw.isNull() ? Json::Value() : raw;
}

std::vector<std::string> CapabilityKnowledge::operatorsForIntent( const std::string &intent ) const
{
  ensureLoaded( const_cast<CapabilityKnowledge &>( *this ) );
  std::vector<std::string> ids;
  for ( const std::string &id : mOrder )
  {
    const Json::Value merged = mergedEntry( mEntries[ id ], Json::Value() );
    bool declared = false;
    for ( const Json::Value &candidate : merged.get( "intents", Json::Value( Json::arrayValue ) ) )
    {
      if ( candidate.isString() && candidate.asString() == intent )
      {
        declared = true;
        break;
      }
    }
    for ( const Json::Value &variant : mEntries[ id ].get( "variants", Json::Value( Json::arrayValue ) ) )
    {
      if ( declared )
        break;
      for ( const Json::Value &candidate : variant.get( "intents", Json::Value( Json::arrayValue ) ) )
      {
        if ( candidate.isString() && candidate.asString() == intent )
        {
          declared = true;
          break;
        }
      }
    }
    if ( declared )
      ids.push_back( id );
  }
  return ids;
}

Json::Value CapabilityKnowledge::familyDefault( const std::string &family ) const
{
  const std::string key = family.rfind( "family:", 0 ) == 0 ? family : "family:" + family;
  const Json::Value &fallback = mFamilyDefaults[ key ];
  return fallback.isNull() ? Json::Value() : fallback;
}

std::vector<std::string> CapabilityKnowledge::entryIds() const
{
  ensureLoaded( const_cast<CapabilityKnowledge &>( *this ) );
  return mOrder;
}

std::vector<std::string> CapabilityKnowledge::entryIdsForSurface(
  const std::string &surface ) const
{
  ensureLoaded( const_cast<CapabilityKnowledge &>( *this ) );
  std::vector<std::string> ids;
  for ( const std::string &id : mOrder )
  {
    const Json::Value raw = mEntries[ id ];
    const std::string entrySurface = raw.get( "surface", "operator" ).asString();
    if ( entrySurface == surface )
      ids.push_back( id );
  }
  return ids;
}

int CapabilityKnowledge::bandRoleMinimum( const Json::Value &entry, const std::string &role )
{
  const Json::Value &bandRoles = entry.get( "band_roles", Json::Value() );
  if ( !bandRoles.isObject() )
    return 0;
  const Json::Value &minimum = bandRoles.get( role, Json::Value() );
  return minimum.isInt() ? std::max( 0, minimum.asInt() ) : 0;
}

std::vector<std::string> CapabilityKnowledge::validateEntry( const Json::Value &entry )
{
  std::vector<std::string> problems;
  if ( !entry.isObject() )
  {
    problems.push_back( "entry must be an object" );
    return problems;
  }

  for ( const std::string &key : entry.getMemberNames() )
  {
    if ( !knownKeys().count( key ) )
      problems.push_back( "unknown key '" + key + "'" );
  }

  const std::string id = entry.get( "id", "" ).asString();
  const std::string family = entry.get( "family", "" ).asString();
  const bool isFamilyDefault = entry.get( "kind", "" ).asString() == "family_default";
  if ( isFamilyDefault && id.rfind( "family:", 0 ) != 0 )
    problems.push_back( "family_default id must start with 'family:'" );
  if ( !isFamilyDefault && id.empty() )
    problems.push_back( "missing 'id'" );
  if ( family.empty() )
    problems.push_back( "missing 'family'" );

  // Harness 8.0: surface discriminator. The default "operator" keeps the
  // 7.0 semantics; tool surfaces must stay out of the scientific intent
  // vocabulary (intents route to processing operators only).
  if ( entry.isMember( "surface" ) )
  {
    const Json::Value &surface = entry["surface"];
    if ( !surface.isString() || !knownSurfaces().count( surface.asString() ) )
      problems.push_back( "'surface' must be operator|spatial_tool|data_platform_tool" );
    else if ( surface.asString() != "operator" && entry.isMember( "intents" ) )
      problems.push_back( "tool-surface entries must not declare 'intents'" );
  }

  const auto checkStringArray = [ &problems ]( const Json::Value &value,
                                               const std::string &field )
  {
    if ( value.isNull() )
      return;
    if ( !value.isArray() )
    {
      problems.push_back( "'" + field + "' must be an array" );
      return;
    }
    for ( const Json::Value &item : value )
      if ( !item.isString() )
        problems.push_back( "'" + field + "' entries must be strings" );
  };

  // intents: closed vocabulary (mirrors isKnownIntent; the drift test pins
  // the agreement so this local table cannot silently diverge).
  static const std::set<std::string> kIntents = {
    "ndvi", "change", "sar_change", "classify", "phenology",
    "evi", "savi", "ndre", "ndwi", "mndwi", "ndsi", "nbr", "dnbr", "ndbi", "bsi",
    "water", "flood", "sar_water", "sar_flood", "sar", "ship",
    "temporal", "terrain", "accuracy", "qa", "preprocess", "inference",
    // Harness 9.0 (M2): zonal raster statistics over vector zones.
    "zonal",
  };
  checkStringArray( entry.get( "intents", Json::Value() ), "intents" );
  for ( const Json::Value &candidate : entry.get( "intents", Json::Value( Json::arrayValue ) ) )
    if ( candidate.isString() && !kIntents.count( candidate.asString() ) )
      problems.push_back( "unknown intent '" + candidate.asString() + "'" );

  // modality / applicability.modalities
  const auto checkModalityList = [ &problems ]( const Json::Value &value,
                                                const std::string &field )
  {
    if ( value.isNull() )
      return;
    if ( !value.isArray() )
    {
      problems.push_back( "'" + field + "' must be an array" );
      return;
    }
    for ( const Json::Value &item : value )
    {
      if ( !item.isString() ||
           !inVocabulary( kModalities, sizeof( kModalities ) / sizeof( kModalities[0] ),
                          lowered( item.asString() ) ) )
        problems.push_back( "unknown modality in '" + field + "'" );
    }
  };
  checkModalityList( entry.get( "modality", Json::Value() ), "modality" );
  if ( entry.isMember( "applicability" ) )
  {
    const Json::Value &applicability = entry["applicability"];
    if ( !applicability.isObject() )
      problems.push_back( "'applicability' must be an object" );
    else
    {
      checkModalityList( applicability.get( "modalities", Json::Value() ),
                         "applicability.modalities" );
      const Json::Value &range =
        applicability.get( "resolution_range", Json::Value() );
      if ( !range.isNull() &&
           ( !range.isArray() || range.size() != 2 || !range[0].isNumeric() ||
             !range[1].isNumeric() || range[0].asDouble() > range[1].asDouble() ) )
        problems.push_back( "'applicability.resolution_range' must be [min,max] numerics" );
    }
  }

  // band_roles
  const Json::Value &bandRoles = entry.get( "band_roles", Json::Value() );
  if ( !bandRoles.isNull() )
  {
    if ( !bandRoles.isObject() )
      problems.push_back( "'band_roles' must be an object" );
    else
    {
      for ( const std::string &role : bandRoles.getMemberNames() )
      {
        if ( !inVocabulary( kBandRoles, sizeof( kBandRoles ) / sizeof( kBandRoles[0] ),
                            lowered( role ) ) )
          problems.push_back( "unknown band role '" + role + "'" );
        else if ( !bandRoles[role].isInt() || bandRoles[role].asInt() < 1 )
          problems.push_back( "band role '" + role + "' needs a positive integer count" );
      }
    }
  }

  if ( entry.isMember( "crs" ) )
  {
    const Json::Value &crs = entry["crs"];
    if ( !crs.isObject() )
      problems.push_back( "'crs' must be an object" );
    else
    {
      const Json::Value &projected = crs.get( "requires_projected", Json::Value() );
      if ( !projected.isNull() && !projected.isBool() )
        problems.push_back( "'crs.requires_projected' must be boolean" );
    }
  }

  // radiometric / sar / temporal / resource / verification / artifacts
  if ( entry.isMember( "radiometric" ) )
  {
    const Json::Value &radiometric = entry["radiometric"];
    if ( !radiometric.isObject() )
      problems.push_back( "'radiometric' must be an object" );
    else
    {
      checkStringArray( radiometric.get( "acceptable", Json::Value() ),
                        "radiometric.acceptable" );
      checkStringArray( radiometric.get( "warn", Json::Value() ), "radiometric.warn" );
    }
  }
  if ( entry.isMember( "sar" ) )
  {
    const Json::Value &sar = entry["sar"];
    if ( !sar.isObject() )
      problems.push_back( "'sar' must be an object" );
    else
    {
      checkStringArray( sar.get( "polarizations", Json::Value() ), "sar.polarizations" );
      checkStringArray( sar.get( "calibration", Json::Value() ), "sar.calibration" );
    }
  }
  if ( entry.isMember( "temporal" ) )
  {
    const Json::Value &temporal = entry["temporal"];
    if ( !temporal.isObject() )
      problems.push_back( "'temporal' must be an object" );
    else
    {
      const Json::Value &minScenes = temporal.get( "min_scenes", Json::Value() );
      if ( !minScenes.isNull() && ( !minScenes.isInt() || minScenes.asInt() < 0 ) )
        problems.push_back( "'temporal.min_scenes' must be a non-negative integer" );
      const Json::Value &maxGap = temporal.get( "max_gap_days", Json::Value() );
      if ( !maxGap.isNull() && ( !maxGap.isInt() || maxGap.asInt() < 0 ) )
        problems.push_back( "'temporal.max_gap_days' must be a non-negative integer" );
      const Json::Value &requiresTime =
        temporal.get( "requires_acquisition_time", Json::Value() );
      if ( !requiresTime.isNull() && !requiresTime.isBool() )
        problems.push_back( "'temporal.requires_acquisition_time' must be boolean" );
    }
  }
  if ( entry.isMember( "resource" ) )
  {
    const Json::Value &resource = entry["resource"];
    if ( !resource.isObject() )
      problems.push_back( "'resource' must be an object" );
    else
    {
      const Json::Value &costClass = resource.get( "cost_class", Json::Value() );
      if ( !costClass.isNull() &&
           ( !costClass.isString() ||
             !inVocabulary( kCostClasses, sizeof( kCostClasses ) / sizeof( kCostClasses[0] ),
                            lowered( costClass.asString() ) ) ) )
        problems.push_back( "unknown resource.cost_class" );
      const Json::Value &largeSafe = resource.get( "large_raster_safe", Json::Value() );
      if ( !largeSafe.isNull() && !largeSafe.isBool() )
        problems.push_back( "'resource.large_raster_safe' must be boolean" );
    }
  }
  if ( entry.isMember( "verification" ) )
  {
    const Json::Value &verification = entry["verification"];
    if ( !verification.isObject() )
      problems.push_back( "'verification' must be an object" );
    else
    {
      const Json::Value &expectedKind = verification.get( "expected_kind", Json::Value() );
      if ( !expectedKind.isNull() &&
           ( !expectedKind.isString() ||
             !inVocabulary( kArifactKinds, sizeof( kArifactKinds ) / sizeof( kArifactKinds[0] ),
                            lowered( expectedKind.asString() ) ) ) )
        problems.push_back( "unknown verification.expected_kind" );
      checkStringArray( verification.get( "checks", Json::Value() ), "verification.checks" );
      for ( const Json::Value &check : verification.get( "checks", Json::Value( Json::arrayValue ) ) )
        if ( check.isString() &&
             !inVocabulary( kVerificationChecks,
                            sizeof( kVerificationChecks ) / sizeof( kVerificationChecks[0] ),
                            lowered( check.asString() ) ) )
          problems.push_back( "unknown verification check '" + check.asString() + "'" );
    }
  }
  if ( entry.isMember( "artifacts" ) )
  {
    const Json::Value &artifacts = entry["artifacts"];
    if ( !artifacts.isObject() )
      problems.push_back( "'artifacts' must be an object" );
    else
    {
      for ( const std::string &artifact : artifacts.getMemberNames() )
      {
        const Json::Value &contract = artifacts[ artifact ];
        if ( !contract.isObject() )
          problems.push_back( "artifacts." + artifact + " must be an object" );
        else
        {
          const Json::Value &kind = contract.get( "kind", Json::Value() );
          if ( !kind.isNull() &&
               ( !kind.isString() ||
                 !inVocabulary( kArifactKinds, sizeof( kArifactKinds ) / sizeof( kArifactKinds[0] ),
                                lowered( kind.asString() ) ) ) )
            problems.push_back( "unknown artifacts." + artifact + ".kind" );
        }
      }
    }
  }
  if ( entry.isMember( "model_compatibility" ) )
  {
    const Json::Value &model = entry["model_compatibility"];
    if ( !model.isObject() )
      problems.push_back( "'model_compatibility' must be an object" );
    else
      checkStringArray( model.get( "families", Json::Value() ),
                        "model_compatibility.families" );
  }
  if ( entry.isMember( "variants" ) )
  {
    const Json::Value &variants = entry["variants"];
    if ( !variants.isArray() )
      problems.push_back( "'variants' must be an array" );
    else
    {
      int index = 0;
      for ( const Json::Value &variant : variants )
      {
        ++index;
        if ( !variant.isObject() )
        {
          problems.push_back( "variants[" + std::to_string( index ) + "] must be an object" );
          continue;
        }
        const Json::Value &when = variant.get( "when", Json::Value() );
        if ( !when.isObject() || when.get( "param", "" ).asString().empty() ||
             !when.get( "values", Json::Value() ).isArray() )
          problems.push_back( "variants[" + std::to_string( index ) +
                              "] needs when:{param, values[]}" );
        for ( const std::string &key : variant.getMemberNames() )
        {
          if ( key == "when" || knownKeys().count( key ) )
            continue;
          problems.push_back( "variants[" + std::to_string( index ) + "] unknown key '" + key +
                              "'" );
        }
        // Variant payloads obey the same closed vocabularies as top-level
        // keys — a variant cannot smuggle unknown intents, band roles, or
        // verification checks past the merged entry's consumers.
        for ( const Json::Value &candidate :
              variant.get( "intents", Json::Value( Json::arrayValue ) ) )
        {
          if ( candidate.isString() && !kIntents.count( candidate.asString() ) )
            problems.push_back( "variants[" + std::to_string( index ) +
                                "] unknown intent '" + candidate.asString() + "'" );
        }
        const Json::Value &variantRoles = variant.get( "band_roles", Json::Value() );
        if ( !variantRoles.isNull() )
        {
          if ( !variantRoles.isObject() )
            problems.push_back( "variants[" + std::to_string( index ) +
                                "] band_roles must be an object" );
          else
          {
            for ( const std::string &role : variantRoles.getMemberNames() )
            {
              if ( !inVocabulary( kBandRoles, sizeof( kBandRoles ) / sizeof( kBandRoles[0] ),
                                  lowered( role ) ) )
                problems.push_back( "variants[" + std::to_string( index ) +
                                    "] unknown band role '" + role + "'" );
              else if ( !variantRoles[role].isInt() || variantRoles[role].asInt() < 1 )
                problems.push_back( "variants[" + std::to_string( index ) +
                                    "] band role '" + role +
                                    "' needs a positive integer count" );
            }
          }
        }
        for ( const Json::Value &check : variant.get( "verification", Json::Value() ).get(
                "checks", Json::Value( Json::arrayValue ) ) )
        {
          if ( check.isString() &&
               !inVocabulary( kVerificationChecks,
                              sizeof( kVerificationChecks ) / sizeof( kVerificationChecks[0] ),
                              lowered( check.asString() ) ) )
            problems.push_back( "variants[" + std::to_string( index ) +
                                "] unknown verification check '" + check.asString() + "'" );
        }
        for ( const Json::Value &modality :
              variant.get( "modality", Json::Value( Json::arrayValue ) ) )
        {
          if ( !modality.isString() ||
               !inVocabulary( kModalities, sizeof( kModalities ) / sizeof( kModalities[0] ),
                              lowered( modality.asString() ) ) )
            problems.push_back( "variants[" + std::to_string( index ) +
                                "] unknown modality" );
        }
      }
    }
  }
  const Json::Value &sideEffects = entry.get( "side_effects", Json::Value() );
  if ( !sideEffects.isNull() && !sideEffects.isBool() )
    problems.push_back( "'side_effects' must be boolean" );
  checkStringArray( entry.get( "limitations", Json::Value() ), "limitations" );

  return problems;
}

} // namespace sicnu::agent::harness
