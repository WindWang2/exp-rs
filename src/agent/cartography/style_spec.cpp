// src/agent/cartography/style_spec.cpp
#include "style_spec.h"

#include "design_tokens.h"

#include <QDir>
#include <QFile>
#include <QMutexLocker>

#include <json/reader.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <sstream>

namespace sicnu::agent::cartography {

namespace {

// Boundedness: a style spec is knowledge, not data — keep documents small.
constexpr int kMaxClasses = 64;
constexpr int kMaxCategories = 64;
constexpr int kMaxRules = 64;
constexpr int kMaxSemantics = 16;
constexpr size_t kMaxDocumentBytes = 512 * 1024;

const char *const kRasterRenderers[] = {
  "singleband_gray", "singleband_pseudocolor", "paletted", "multiband_color",
};
const char *const kVectorRenderers[] = {
  "simple", "categorized", "graduated", "rule_based",
};

QString defaultStylesDir()
{
  if ( qEnvironmentVariableIsSet( "SICNU_CARTOGRAPHY_DIR" ) )
    return QDir( qEnvironmentVariable( "SICNU_CARTOGRAPHY_DIR" ) ).filePath( QStringLiteral( "styles" ) );
  return QDir::current().filePath( QStringLiteral( "data/cartography/styles" ) );
}

bool parseJsonFile( const QString &path, Json::Value *out, QString *error )
{
  QFile file( path );
  if ( !file.open( QIODevice::ReadOnly ) )
  {
    if ( error )
      *error = QStringLiteral( "cannot read %1" ).arg( path );
    return false;
  }
  const QByteArray bytes = file.readAll();
  if ( bytes.size() > static_cast<qint64>( kMaxDocumentBytes ) )
  {
    if ( error )
      *error = QStringLiteral( "%1 exceeds the %2 KB style-spec budget" )
                 .arg( path )
                 .arg( kMaxDocumentBytes / 1024 );
    return false;
  }
  std::istringstream stream( bytes.toStdString() );
  Json::CharReaderBuilder builder;
  std::string parseErrors;
  if ( !Json::parseFromStream( builder, stream, out, &parseErrors ) )
  {
    if ( error )
      *error = QStringLiteral( "%1: %2" ).arg( path, QString::fromStdString( parseErrors ) );
    return false;
  }
  return true;
}

void checkClassEntry( const char *what, int index, const Json::Value &entry,
                      std::vector<std::string> &problems )
{
  const std::string where = std::string( what ) + "[" + std::to_string( index ) + "]";
  if ( !entry.isObject() )
  {
    problems.push_back( where + " must be an object" );
    return;
  }
  if ( entry.isMember( "label" ) && !entry["label"].isString() )
    problems.push_back( where + ".label must be a string" );
  if ( entry.isMember( "color" ) && !entry["color"].isString() )
    problems.push_back( where + ".color must be a string (hex or token reference)" );
  if ( entry.isMember( "min" ) && !entry["min"].isNumeric() )
    problems.push_back( where + ".min must be numeric" );
  if ( entry.isMember( "max" ) && !entry["max"].isNumeric() )
    problems.push_back( where + ".max must be numeric" );
  if ( entry.isMember( "value" ) && !( entry["value"].isString() || entry["value"].isNumeric() ) )
    problems.push_back( where + ".value must be a string or number" );
}

void checkStretch( const Json::Value &stretch, std::vector<std::string> &problems )
{
  if ( !stretch.isObject() )
  {
    problems.push_back( "raster.stretch must be an object" );
    return;
  }
  const std::string type = stretch.isMember( "type" ) && stretch["type"].isString()
                             ? stretch["type"].asString()
                             : "minmax";
  if ( type != "minmax" && type != "percentile" && type != "stddev" && type != "fixed" )
    problems.push_back( "raster.stretch.type must be minmax|percentile|stddev|fixed" );
  if ( stretch.isMember( "percentile" ) &&
       ( !stretch["percentile"].isNumeric() || stretch["percentile"].asDouble() < 0 ||
         stretch["percentile"].asDouble() >= 50 ) )
    problems.push_back( "raster.stretch.percentile must be in [0, 50)" );
  if ( stretch.isMember( "stddev" ) && ( !stretch["stddev"].isNumeric() || stretch["stddev"].asDouble() <= 0 ) )
    problems.push_back( "raster.stretch.stddev must be positive" );
  if ( type == "fixed" && !( stretch.isMember( "min" ) && stretch.isMember( "max" ) &&
                             stretch["min"].isNumeric() && stretch["max"].isNumeric() ) )
    problems.push_back( "raster.stretch fixed type needs numeric min and max" );
}

void checkLabels( const Json::Value &labels, std::vector<std::string> &problems )
{
  if ( !labels.isObject() )
  {
    problems.push_back( "vector.labels must be an object" );
    return;
  }
  if ( labels.isMember( "enabled" ) && !labels["enabled"].isBool() )
    problems.push_back( "vector.labels.enabled must be a boolean" );
  if ( labels.isMember( "size_pt" ) )
  {
    const Json::Value &size = labels["size_pt"];
    if ( !( size.isNumeric() || ( size.isString() && isTokenReference( size.asString() ) ) ) )
      problems.push_back( "vector.labels.size_pt must be numeric or a token reference" );
  }
  for ( const char *field : { "field", "color" } )
    if ( labels.isMember( field ) && !labels[field].isString() )
      problems.push_back( std::string( "vector.labels." ) + field + " must be a string" );
  for ( const char *field : { "halo_mm", "priority" } )
    if ( labels.isMember( field ) && !labels[field].isNumeric() )
      problems.push_back( std::string( "vector.labels." ) + field + " must be numeric" );
}

void checkRaster( const Json::Value &raster, std::vector<std::string> &problems )
{
  if ( !raster.isObject() )
  {
    problems.push_back( "raster must be an object" );
    return;
  }
  if ( !raster.isMember( "renderertype" ) || !raster["renderertype"].isString() ||
       !isRasterRendererType( raster["renderertype"].asString() ) )
    problems.push_back( "raster.renderertype must be one of singleband_gray, "
                        "singleband_pseudocolor, paletted, multiband_color" );
  if ( raster.isMember( "band" ) && ( !raster["band"].isIntegral() || raster["band"].asInt() < 1 ) )
    problems.push_back( "raster.band must be a 1-based integer" );
  if ( raster.isMember( "gamma" ) &&
       ( !raster["gamma"].isNumeric() || raster["gamma"].asDouble() <= 0 ) )
    problems.push_back( "raster.gamma must be positive" );
  if ( raster.isMember( "opacity" ) &&
       ( !raster["opacity"].isNumeric() || raster["opacity"].asDouble() < 0 ||
         raster["opacity"].asDouble() > 1 ) )
    problems.push_back( "raster.opacity must be within [0, 1]" );
  if ( raster.isMember( "resampling" ) )
  {
    const std::string resampling = raster["resampling"].asString();
    if ( resampling != "nearest" && resampling != "bilinear" && resampling != "cubic" )
      problems.push_back( "raster.resampling must be nearest|bilinear|cubic" );
  }
  if ( raster.isMember( "stretch" ) )
    checkStretch( raster["stretch"], problems );
  if ( raster.isMember( "classification" ) )
  {
    const Json::Value &classification = raster["classification"];
    if ( !classification.isObject() )
    {
      problems.push_back( "raster.classification must be an object" );
    }
    else
    {
      const std::string mode = classification.isMember( "mode" ) && classification["mode"].isString()
                                 ? classification["mode"].asString()
                                 : "discrete";
      if ( mode != "discrete" && mode != "continuous" )
        problems.push_back( "raster.classification.mode must be discrete|continuous" );
      if ( classification.isMember( "ramp" ) )
      {
        // Ramp: a palette name / token reference string, or a token-resolved
        // array of hex stops (what resolveStyleTokens leaves behind).
        const Json::Value &ramp = classification["ramp"];
        if ( !( ramp.isString() ||
                ( ramp.isArray() &&
                  std::all_of( ramp.begin(), ramp.end(),
                               []( const Json::Value &stop ) { return stop.isString(); } ) ) ) )
          problems.push_back( "raster.classification.ramp must be a string or an array of "
                              "color strings" );
      }
      if ( classification.isMember( "classes" ) )
      {
        if ( !classification["classes"].isArray() )
        {
          problems.push_back( "raster.classification.classes must be an array" );
        }
        else
        {
          if ( static_cast<int>( classification["classes"].size() ) > kMaxClasses )
            problems.push_back( "raster.classification.classes exceeds the " +
                                std::to_string( kMaxClasses ) + " class budget" );
          int index = 0;
          for ( const auto &entry : classification["classes"] )
            checkClassEntry( "raster.classification.classes", index++, entry, problems );
        }
      }
    }
  }
}

void checkVector( const Json::Value &vector, std::vector<std::string> &problems )
{
  if ( !vector.isObject() )
  {
    problems.push_back( "vector must be an object" );
    return;
  }
  if ( !vector.isMember( "renderertype" ) || !vector["renderertype"].isString() ||
       !isVectorRendererType( vector["renderertype"].asString() ) )
    problems.push_back( "vector.renderertype must be one of simple, categorized, graduated, "
                        "rule_based" );
  if ( vector.isMember( "field" ) && !vector["field"].isString() )
    problems.push_back( "vector.field must be a string" );
  if ( vector.isMember( "opacity" ) &&
       ( !vector["opacity"].isNumeric() || vector["opacity"].asDouble() < 0 ||
         vector["opacity"].asDouble() > 1 ) )
    problems.push_back( "vector.opacity must be within [0, 1]" );
  if ( vector.isMember( "blend_mode" ) )
  {
    const std::string blend = vector["blend_mode"].asString();
    static const std::set<std::string> kBlendModes = {
      "normal", "multiply", "screen", "overlay", "darken", "lighten",
    };
    if ( !kBlendModes.count( blend ) )
      problems.push_back( "vector.blend_mode must be normal|multiply|screen|overlay|darken|lighten" );
  }
  if ( vector.isMember( "categories" ) )
  {
    if ( !vector["categories"].isArray() )
    {
      problems.push_back( "vector.categories must be an array" );
    }
    else
    {
      if ( static_cast<int>( vector["categories"].size() ) > kMaxCategories )
        problems.push_back( "vector.categories exceeds the " + std::to_string( kMaxCategories ) +
                            " category budget" );
      int index = 0;
      for ( const auto &entry : vector["categories"] )
        checkClassEntry( "vector.categories", index++, entry, problems );
    }
  }
  if ( vector.isMember( "labels" ) )
    checkLabels( vector["labels"], problems );
  if ( vector.isMember( "scaledenominator" ) )
  {
    const Json::Value &scale = vector["scaledenominator"];
    if ( !scale.isObject() )
      problems.push_back( "vector.scaledenominator must be an object" );
    else
      for ( const char *field : { "min", "max" } )
        if ( scale.isMember( field ) && ( !scale[field].isNumeric() || scale[field].asDouble() < 0 ) )
          problems.push_back( std::string( "vector.scaledenominator." ) + field +
                              " must be a non-negative number" );
  }
  if ( vector.isMember( "rules" ) && static_cast<int>( vector["rules"].size() ) > kMaxRules )
    problems.push_back( "vector.rules exceeds the " + std::to_string( kMaxRules ) + " rule budget" );
}

// Token resolution: walks the document replacing reference strings.
Json::Value resolveTokensRecursive( const Json::Value &node, const Json::Value &tokens,
                                    std::vector<std::string> &problems )
{
  if ( node.isString() )
  {
    if ( !isTokenReference( node.asString() ) )
      return node;
    // Issue #815: resolution is transitive — a token may alias another
    // token reference. Cycles/over-deep chains are reported by the shared
    // chain resolver, which returns the original value on failure.
    const Json::Value resolved = resolveTokenReferenceChain( tokens, node, problems );
    if ( resolved.isArray() || resolved.isObject() )
      return resolveTokensRecursive( resolved, tokens, problems );
    return resolved;
  }
  if ( node.isArray() )
  {
    Json::Value out( Json::arrayValue );
    for ( const auto &entry : node )
      out.append( resolveTokensRecursive( entry, tokens, problems ) );
    return out;
  }
  if ( node.isObject() )
  {
    Json::Value out( Json::objectValue );
    for ( const auto &key : node.getMemberNames() )
      out[key] = resolveTokensRecursive( node[key], tokens, problems );
    return out;
  }
  return node;
}

} // namespace

bool isRasterRendererType( const std::string &type )
{
  for ( const char *candidate : kRasterRenderers )
    if ( type == candidate )
      return true;
  return false;
}

bool isVectorRendererType( const std::string &type )
{
  for ( const char *candidate : kVectorRenderers )
    if ( type == candidate )
      return true;
  return false;
}

bool isTokenReference( const std::string &value )
{
  return value.rfind( "token:", 0 ) == 0 && value.size() > strlen( "token:" );
}

std::string tokenReferencePath( const std::string &value )
{
  if ( !isTokenReference( value ) )
    return std::string();
  return value.substr( strlen( "token:" ) );
}

Json::Value resolveTokenReferenceChain( const Json::Value &tokens, const Json::Value &value,
                                        std::vector<std::string> &problems )
{
  if ( !value.isString() || !isTokenReference( value.asString() ) )
    return value;
  const Json::Value original = value;
  Json::Value current = value;
  std::set<std::string> visited;
  int hops = 0;
  while ( current.isString() && isTokenReference( current.asString() ) )
  {
    const std::string path = tokenReferencePath( current.asString() );
    if ( !visited.insert( path ).second )
    {
      problems.push_back( "token reference cycle at '" + path + "'" );
      return original;
    }
    if ( ++hops > kMaxTokenHops )
    {
      problems.push_back( "token reference chain exceeds " + std::to_string( kMaxTokenHops ) +
                          " hops at '" + path + "'" );
      return original;
    }
    const Json::Value resolved = tokenValue( tokens, path );
    if ( resolved.isNull() )
    {
      problems.push_back( "unresolvable token reference '" + current.asString() + "'" );
      return original;
    }
    current = resolved;
  }
  return current;
}

// ---------------------------------------------------------------------------
// Platform 6.0 (Milestone E): semantic applicability
// ---------------------------------------------------------------------------

std::vector<std::string> validateStyleApplicability( const Json::Value &styleSpec )
{
  std::vector<std::string> problems;
  if ( !styleSpec.isObject() || !styleSpec.isMember( "applicability" ) )
    return problems;
  const std::string id = styleSpec.isMember( "id" ) && styleSpec["id"].isString()
                           ? styleSpec["id"].asString()
                           : "";
  const Json::Value &applicability = styleSpec["applicability"];
  if ( !applicability.isObject() )
  {
    problems.push_back( id + ": applicability must be an object" );
    return problems;
  }
  if ( applicability.isMember( "value_domain" ) )
  {
    const Json::Value &domain = applicability["value_domain"];
    if ( !domain.isObject() || !domain.isMember( "min" ) || !domain["min"].isNumeric() ||
         !domain.isMember( "max" ) || !domain["max"].isNumeric() ||
         domain["min"].asDouble() >= domain["max"].asDouble() )
      problems.push_back( id + ": applicability.value_domain needs numeric min < max" );
  }
  if ( applicability.isMember( "band_count" ) )
  {
    const Json::Value &bandCount = applicability["band_count"];
    if ( !bandCount.isObject() )
      problems.push_back( id + ": applicability.band_count must be an object" );
    else
    {
      const bool hasMin = bandCount.isMember( "min" ) && bandCount["min"].isIntegral();
      const bool hasMax = bandCount.isMember( "max" ) && bandCount["max"].isIntegral();
      if ( !hasMin && !hasMax )
        problems.push_back( id + ": applicability.band_count needs integer min and/or max" );
      else if ( hasMin && hasMax && bandCount["min"].asInt() > bandCount["max"].asInt() )
        problems.push_back( id + ": applicability.band_count min must not exceed max" );
    }
  }
  for ( const char *member : { "modalities", "semantics" } )
    if ( applicability.isMember( member ) && !applicability[member].isArray() )
      problems.push_back( id + ": applicability." + member + " must be an array of strings" );
  return problems;
}

std::vector<std::string> checkStyleApplicability( const Json::Value &styleSpec,
                                                  const Json::Value &dataset )
{
  std::vector<std::string> problems;
  if ( !styleSpec.isObject() || !styleSpec.isMember( "applicability" ) ||
       !styleSpec["applicability"].isObject() || !dataset.isObject() )
    return problems;
  const std::string id = styleSpec.isMember( "id" ) && styleSpec["id"].isString()
                           ? styleSpec["id"].asString()
                           : "";
  const Json::Value &applicability = styleSpec["applicability"];

  // Layer-kind contract (applies_to was already mandatory).
  const std::string kind = dataset.isMember( "kind" ) && dataset["kind"].isString()
                             ? dataset["kind"].asString()
                             : std::string();
  const std::string applies = styleSpec.isMember( "applies_to" ) && styleSpec["applies_to"].isString()
                                ? styleSpec["applies_to"].asString()
                                : "any";
  if ( !kind.empty() && applies != "any" && applies != kind )
    problems.push_back( id + ": style applies to " + applies + " data but the dataset is " + kind );

  // Band count contract (e.g. multiband_color needs >= 3 bands).
  if ( applicability.isMember( "band_count" ) && applicability["band_count"].isObject() &&
       dataset.isMember( "band_count" ) && dataset["band_count"].isIntegral() )
  {
    const int actual = dataset["band_count"].asInt();
    const Json::Value &bandCount = applicability["band_count"];
    if ( bandCount.isMember( "min" ) && bandCount["min"].isIntegral() &&
         actual < bandCount["min"].asInt() )
      problems.push_back( id + ": dataset has " + std::to_string( actual ) +
                          " band(s), style requires at least " + bandCount["min"].asString() );
    if ( bandCount.isMember( "max" ) && bandCount["max"].isIntegral() &&
         actual > bandCount["max"].asInt() )
      problems.push_back( id + ": dataset has " + std::to_string( actual ) +
                          " band(s), style requires at most " + bandCount["max"].asString() );
  }

  // Value-domain contract: a style built for NDVI must not be applied to
  // SAR backscatter magnitudes "because it renders something".
  if ( applicability.isMember( "value_domain" ) && applicability["value_domain"].isObject() &&
       dataset.isMember( "value_min" ) && dataset["value_min"].isNumeric() &&
       dataset.isMember( "value_max" ) && dataset["value_max"].isNumeric() )
  {
    const double declaredMin = applicability["value_domain"]["min"].asDouble();
    const double declaredMax = applicability["value_domain"]["max"].asDouble();
    const double actualMin = dataset["value_min"].asDouble();
    const double actualMax = dataset["value_max"].asDouble();
    const bool overlaps = actualMin <= declaredMax && declaredMin <= actualMax;
    if ( !overlaps )
      problems.push_back( id + ": data range [" + std::to_string( actualMin ) + ", " +
                          std::to_string( actualMax ) + "] does not overlap the style's declared "
                          "value domain [" + std::to_string( declaredMin ) + ", " +
                          std::to_string( declaredMax ) + "]" );
  }

  // Modality contract: optical indices do not apply to SAR magnitude and
  // vice versa.
  if ( applicability.isMember( "modalities" ) && applicability["modalities"].isArray() &&
       dataset.isMember( "modality" ) && dataset["modality"].isString() )
  {
    bool matched = false;
    for ( const auto &modality : applicability["modalities"] )
      if ( modality.isString() && modality.asString() == dataset["modality"].asString() )
        matched = true;
    if ( !matched )
      problems.push_back( id + ": style is not declared applicable to modality '" +
                          dataset["modality"].asString() + "'" );
  }

  // Platform 7.0 modality-aware renderer checks (advisory-free: these are
  // hard semantic contradictions, not stylistic advice).
  const std::string renderer =
    styleSpec.isMember( "raster" ) && styleSpec["raster"].isObject() &&
        styleSpec["raster"].isMember( "renderertype" ) && styleSpec["raster"]["renderertype"].isString()
      ? styleSpec["raster"]["renderertype"].asString()
      : std::string();
  const std::string datasetModality =
    dataset.isMember( "modality" ) && dataset["modality"].isString()
      ? dataset["modality"].asString()
      : std::string();
  if ( renderer == "multiband_color" )
  {
    if ( dataset.isMember( "band_count" ) && dataset["band_count"].isIntegral() &&
         dataset["band_count"].asInt() < 3 )
      problems.push_back( id + ": multiband_color needs at least 3 bands, the dataset has " +
                          std::to_string( dataset["band_count"].asInt() ) );
    if ( datasetModality == "sar" )
      problems.push_back( id + ": SAR backscatter is single-band; multiband_color cannot "
                               "represent it semantically" );
  }
  if ( ( datasetModality == "dem" || datasetModality == "terrain" ) &&
       styleSpec.isMember( "raster" ) && styleSpec["raster"].isObject() &&
       !styleSpec["raster"].isMember( "stretch" ) && !styleSpec["raster"].isMember( "classification" ) )
    problems.push_back( id + ": DEM/terrain styles should declare a stretch or a "
                             "classification (bare singleband gray hides elevation semantics)" );
  // Class ontology mapping: a style whose ontology tags are all disjoint
  // from the dataset semantics maps no declared concept of the data —
  // refused with an explicit problem instead of a wrong correspondence.
  const auto checkOntologyMapping = [ & ]( const char *where, const Json::Value &entries ) {
    if ( !entries.isArray() || entries.empty() )
      return;
    std::vector<std::string> ontologyTags;
    for ( const auto &entry : entries )
      if ( entry.isObject() && entry.isMember( "ontology" ) && entry["ontology"].isString() &&
           !entry["ontology"].asString().empty() )
        ontologyTags.push_back( entry["ontology"].asString() );
    if ( ontologyTags.empty() || !dataset.isMember( "semantics" ) ||
         !dataset["semantics"].isArray() || dataset["semantics"].empty() )
      return;
    bool anyMatch = false;
    for ( const auto &tag : dataset["semantics"] )
      anyMatch =
        anyMatch ||
        ( tag.isString() &&
          std::count( ontologyTags.begin(), ontologyTags.end(), tag.asString() ) > 0 );
    if ( !anyMatch )
      problems.push_back( id + ": " + where + " ontology tags (" + ontologyTags.front() +
                          ", …) do not intersect the dataset semantics" );
  };
  if ( styleSpec.isMember( "raster" ) && styleSpec["raster"].isObject() &&
       styleSpec["raster"].isMember( "classification" ) &&
       styleSpec["raster"]["classification"].isObject() &&
       styleSpec["raster"]["classification"].isMember( "classes" ) )
    checkOntologyMapping( "classification.classes",
                          styleSpec["raster"]["classification"]["classes"] );
  if ( styleSpec.isMember( "vector" ) && styleSpec["vector"].isObject() &&
       styleSpec["vector"].isMember( "categories" ) )
    checkOntologyMapping( "categories", styleSpec["vector"]["categories"] );

  // Declared uncertainty wants data that can carry it.
  if ( styleSpec.isMember( "uncertainty" ) && styleSpec["uncertainty"].isObject() )
  {
    const std::string kind =
      styleSpec["uncertainty"].isMember( "kind" ) && styleSpec["uncertainty"]["kind"].isString()
        ? styleSpec["uncertainty"]["kind"].asString()
        : "none";
    if ( kind != "none" && dataset.isMember( "semantics" ) && dataset["semantics"].isArray() )
    {
      bool carries = false;
      for ( const auto &tag : dataset["semantics"] )
        if ( tag.isString() )
        {
          const std::string value = tag.asString();
          carries = carries || value == "uncertainty" || value == "probability" ||
                    value == "confidence";
        }
      if ( !carries )
        problems.push_back( id + ": style declares uncertainty (" + kind +
                            ") but the dataset carries no uncertainty/probability/confidence "
                            "semantics" );
    }
  }
  return problems;
}

//
// Platform 7.0 semantic scheme / nodata / uncertainty / contrast surfaces.
//

bool isStyleScheme( const std::string &scheme )
{
  return scheme == "categorical" || scheme == "sequential" || scheme == "diverging";
}

std::vector<std::string> validateStyleSemantics( const Json::Value &styleSpec )
{
  std::vector<std::string> problems;
  if ( !styleSpec.isObject() )
    return problems;
  const std::string id = styleSpec.isMember( "id" ) && styleSpec["id"].isString()
                           ? styleSpec["id"].asString()
                           : "";
  const Json::Value *classification = nullptr;
  if ( styleSpec.isMember( "raster" ) && styleSpec["raster"].isObject() &&
       styleSpec["raster"].isMember( "classification" ) &&
       styleSpec["raster"]["classification"].isObject() )
    classification = &styleSpec["raster"]["classification"];

  if ( classification && classification->isMember( "scheme" ) )
  {
    const Json::Value &scheme = ( *classification )["scheme"];
    if ( !scheme.isString() || !isStyleScheme( scheme.asString() ) )
    {
      problems.push_back( id + ": raster.classification.scheme must be categorical, "
                               "sequential or diverging" );
    }
    else if ( scheme.asString() == "diverging" )
    {
      if ( !classification->isMember( "center" ) || !( *classification )["center"].isNumeric() )
        problems.push_back( id + ": diverging scheme requires a numeric "
                                "raster.classification.center (the neutral value)" );
      else if ( classification->isMember( "classes" ) && ( *classification )["classes"].isArray() &&
                !( *classification )["classes"].empty() )
      {
        const double center = ( *classification )["center"].asDouble();
        double lo = 0.0;
        double hi = 0.0;
        bool first = true;
        for ( const auto &entry : ( *classification )["classes"] )
        {
          if ( !entry.isObject() )
            continue;
          if ( entry.isMember( "min" ) && entry["min"].isNumeric() )
          {
            lo = first ? entry["min"].asDouble() : std::min( lo, entry["min"].asDouble() );
            first = false;
          }
          if ( entry.isMember( "max" ) && entry["max"].isNumeric() )
          {
            hi = first ? entry["max"].asDouble() : std::max( hi, entry["max"].asDouble() );
            first = false;
          }
        }
        if ( !first && ( center < lo - 1e-9 || center > hi + 1e-9 ) )
          problems.push_back( id + ": diverging center " + std::to_string( center ) +
                              " lies outside the declared class range [" + std::to_string( lo ) +
                              ", " + std::to_string( hi ) + "]" );
      }
    }
    else if ( scheme.asString() == "categorical" && classification->isMember( "mode" ) &&
              ( *classification )["mode"].isString() &&
              ( *classification )["mode"].asString() == "continuous" )
      problems.push_back( id + ": categorical scheme contradicts classification.mode "
                               "'continuous' (categorical classes are discrete)" );
  }

  if ( styleSpec.isMember( "raster" ) && styleSpec["raster"].isObject() &&
       styleSpec["raster"].isMember( "nodata" ) )
  {
    const Json::Value &nodata = styleSpec["raster"]["nodata"];
    if ( !nodata.isObject() )
      problems.push_back( id + ": raster.nodata must be an object" );
    else
    {
      if ( nodata.isMember( "value" ) && !nodata["value"].isNumeric() )
        problems.push_back( id + ": raster.nodata.value must be numeric" );
      if ( nodata.isMember( "transparent" ) && !nodata["transparent"].isBool() )
        problems.push_back( id + ": raster.nodata.transparent must be a boolean" );
      if ( nodata.isMember( "label" ) && !nodata["label"].isString() )
        problems.push_back( id + ": raster.nodata.label must be a string" );
    }
  }

  // Platform 7.0 class ontology mapping: class/category entries may tag an
  // `ontology` concept (free-form string). Shape is validated here; the
  // semantic intersection with the target dataset is checked by
  // checkStyleApplicability.
  const auto checkOntologyShape = [ & ]( const char *where, const Json::Value &entries ) {
    if ( !entries.isArray() )
      return;
    int index = 0;
    for ( const auto &entry : entries )
    {
      if ( !entry.isObject() || !entry.isMember( "ontology" ) )
      {
        ++index;
        continue;
      }
      const std::string at = std::string( where ) + "[" + std::to_string( index++ ) + "]";
      if ( !entry["ontology"].isString() || entry["ontology"].asString().empty() )
        problems.push_back( id + ": " + at + ".ontology must be a non-empty string" );
    }
  };
  if ( styleSpec.isMember( "raster" ) && styleSpec["raster"].isObject() &&
       styleSpec["raster"].isMember( "classification" ) &&
       styleSpec["raster"]["classification"].isObject() &&
       styleSpec["raster"]["classification"].isMember( "classes" ) )
    checkOntologyShape( "classification.classes",
                        styleSpec["raster"]["classification"]["classes"] );
  if ( styleSpec.isMember( "vector" ) && styleSpec["vector"].isObject() &&
       styleSpec["vector"].isMember( "categories" ) )
    checkOntologyShape( "categories", styleSpec["vector"]["categories"] );

  if ( styleSpec.isMember( "uncertainty" ) )
  {
    const Json::Value &uncertainty = styleSpec["uncertainty"];
    if ( !uncertainty.isObject() )
      problems.push_back( id + ": uncertainty must be an object" );
    else
    {
      const std::string kind = uncertainty.isMember( "kind" ) && uncertainty["kind"].isString()
                                 ? uncertainty["kind"].asString()
                                 : "";
      if ( kind != "none" && kind != "band" && kind != "hatch" &&
           kind != "confidence_interval" )
        problems.push_back( id + ": uncertainty.kind must be none, band, hatch or "
                                "confidence_interval" );
      if ( uncertainty.isMember( "level" ) &&
           ( !uncertainty["level"].isNumeric() || uncertainty["level"].asDouble() < 0 ||
             uncertainty["level"].asDouble() > 1 ) )
        problems.push_back( id + ": uncertainty.level must be a number in [0, 1]" );
      if ( uncertainty.isMember( "field" ) && !uncertainty["field"].isString() )
        problems.push_back( id + ": uncertainty.field must be a string" );
    }
  }
  return problems;
}

namespace {

/// WCAG relative luminance of a hex color ("#rrggbb"); 0 when unparseable.
double relativeLuminance( const std::string &hex )
{
  if ( hex.size() != 7 || hex[0] != '#' )
    return -1.0;
  auto nibble = []( char c ) -> int {
    if ( c >= '0' && c <= '9' )
      return c - '0';
    if ( c >= 'a' && c <= 'f' )
      return c - 'a' + 10;
    if ( c >= 'A' && c <= 'F' )
      return c - 'A' + 10;
    return -1;
  };
  int rgb[3];
  for ( int i = 0; i < 3; ++i )
  {
    const int hi = nibble( hex[1 + 2 * i] );
    const int lo = nibble( hex[2 + 2 * i] );
    if ( hi < 0 || lo < 0 )
      return -1.0;
    rgb[i] = hi * 16 + lo;
  }
  double linear[3];
  for ( int i = 0; i < 3; ++i )
  {
    const double c = rgb[i] / 255.0;
    linear[i] = c <= 0.04045 ? c / 12.92 : std::pow( ( c + 0.055 ) / 1.055, 2.4 );
  }
  return 0.2126 * linear[0] + 0.7152 * linear[1] + 0.0722 * linear[2];
}

double contrastRatio( double l1, double l2 )
{
  if ( l1 < 0 || l2 < 0 )
    return -1.0;
  const double lighter = std::max( l1, l2 );
  const double darker = std::min( l1, l2 );
  return ( lighter + 0.05 ) / ( darker + 0.05 );
}

} // namespace

std::vector<std::string> checkStyleContrast( const Json::Value &resolvedStyleSpec,
                                             const Json::Value *tokens )
{
  std::vector<std::string> warnings;
  if ( !resolvedStyleSpec.isObject() )
    return warnings;
  const std::string id = resolvedStyleSpec.isMember( "id" ) && resolvedStyleSpec["id"].isString()
                           ? resolvedStyleSpec["id"].asString()
                           : "";
  const Json::Value &contrast =
    resolvedStyleSpec.isMember( "contrast" ) && resolvedStyleSpec["contrast"].isObject()
      ? resolvedStyleSpec["contrast"]
      : Json::Value( Json::objectValue );
  const double minText = contrast.isMember( "min_text" ) && contrast["min_text"].isNumeric()
                           ? contrast["min_text"].asDouble()
                           : 4.5;
  const double minClass = contrast.isMember( "min_class" ) && contrast["min_class"].isNumeric()
                            ? contrast["min_class"].asDouble()
                            : 1.5;

  // Label text vs background (needs a token background; without tokens the
  // check degrades to class-pair checks only — reported once).
  std::vector<std::string> textColors;
  if ( resolvedStyleSpec.isMember( "vector" ) && resolvedStyleSpec["vector"].isObject() &&
       resolvedStyleSpec["vector"].isMember( "labels" ) &&
       resolvedStyleSpec["vector"]["labels"].isObject() &&
       resolvedStyleSpec["vector"]["labels"].isMember( "color" ) &&
       resolvedStyleSpec["vector"]["labels"]["color"].isString() )
    textColors.push_back( resolvedStyleSpec["vector"]["labels"]["color"].asString() );
  double background = -1.0;
  std::string backgroundHex = "#ffffff";
  if ( tokens && tokens->isObject() && tokens->isMember( "colors" ) &&
       ( *tokens )["colors"].isObject() && ( *tokens )["colors"].isMember( "background" ) &&
       ( *tokens )["colors"]["background"].isString() )
  {
    backgroundHex = ( *tokens )["colors"]["background"].asString();
    background = relativeLuminance( backgroundHex );
  }
  if ( background >= 0 )
  {
    for ( const std::string &color : textColors )
    {
      const double ratio = contrastRatio( relativeLuminance( color ), background );
      if ( ratio >= 0 && ratio < minText )
        warnings.push_back( id + ": label color " + color + " on background " + backgroundHex +
                            " has contrast " + std::to_string( ratio ).substr( 0, 5 ) +
                            " (< " + std::to_string( minText ).substr( 0, 4 ) + ")" );
    }
  }

  // Consecutive class color pairs must stay distinguishable.
  const auto checkPairs = [ & ]( const char *where, const Json::Value &entries ) {
    std::string previous;
    for ( const auto &entry : entries )
    {
      if ( !entry.isObject() || !entry.isMember( "color" ) || !entry["color"].isString() )
        continue;
      const std::string color = entry["color"].asString();
      if ( !previous.empty() )
      {
        const double ratio = contrastRatio( relativeLuminance( color ),
                                            relativeLuminance( previous ) );
        if ( ratio >= 0 && ratio < minClass )
          warnings.push_back( id + ": adjacent " + where + " colors " + previous + " / " +
                              color + " have contrast " + std::to_string( ratio ).substr( 0, 5 ) +
                              " (< " + std::to_string( minClass ).substr( 0, 4 ) + ")" );
      }
      previous = color;
    }
  };
  if ( resolvedStyleSpec.isMember( "raster" ) && resolvedStyleSpec["raster"].isObject() &&
       resolvedStyleSpec["raster"].isMember( "classification" ) &&
       resolvedStyleSpec["raster"]["classification"].isObject() &&
       resolvedStyleSpec["raster"]["classification"].isMember( "classes" ) &&
       resolvedStyleSpec["raster"]["classification"]["classes"].isArray() )
    checkPairs( "class", resolvedStyleSpec["raster"]["classification"]["classes"] );
  if ( resolvedStyleSpec.isMember( "vector" ) && resolvedStyleSpec["vector"].isObject() &&
       resolvedStyleSpec["vector"].isMember( "categories" ) &&
       resolvedStyleSpec["vector"]["categories"].isArray() )
    checkPairs( "category", resolvedStyleSpec["vector"]["categories"] );
  return warnings;
}

bool repairStyleSemantics( Json::Value &styleSpec, std::vector<std::string> *decisions )
{
  if ( !styleSpec.isObject() || !styleSpec.isMember( "raster" ) ||
       !styleSpec["raster"].isObject() || !styleSpec["raster"].isMember( "classification" ) ||
       !styleSpec["raster"]["classification"].isObject() )
    return false;
  Json::Value classification = styleSpec["raster"]["classification"];
  if ( !classification.isMember( "scheme" ) || !classification["scheme"].isString() ||
       classification["scheme"].asString() != "diverging" ||
       classification.isMember( "center" ) )
    return false;
  // Canonical fix: a diverging ramp without a declared center whose class
  // structure brackets 0 takes center = 0 (the neutral-zero convention).
  if ( !classification.isMember( "classes" ) || !classification["classes"].isArray() ||
       classification["classes"].empty() )
    return false;
  double lo = 0.0;
  double hi = 0.0;
  bool first = true;
  for ( const auto &entry : classification["classes"] )
  {
    if ( !entry.isObject() )
      continue;
    if ( entry.isMember( "min" ) && entry["min"].isNumeric() )
    {
      lo = first ? entry["min"].asDouble() : std::min( lo, entry["min"].asDouble() );
      first = false;
    }
    if ( entry.isMember( "max" ) && entry["max"].isNumeric() )
    {
      hi = first ? entry["max"].asDouble() : std::max( hi, entry["max"].asDouble() );
      first = false;
    }
  }
  if ( first || lo > 0 || hi < 0 )
    return false; // does not bracket zero: reject, no canonical fix
  classification["center"] = 0.0;
  styleSpec["raster"]["classification"] = classification;
  if ( decisions )
    decisions->push_back( "diverging scheme without center: derived center = 0 from the "
                          "zero-bracketing class structure" );
  return true;
}

std::vector<std::string> validateStyleSpec( const Json::Value &doc )
{
  std::vector<std::string> problems;
  if ( !doc.isObject() )
    return { "style spec must be an object" };
  const std::string id = doc.isMember( "id" ) && doc["id"].isString() ? doc["id"].asString() : "";
  if ( id.empty() )
    problems.push_back( "style spec needs a string id" );
  if ( !doc.isMember( "version" ) || !doc["version"].isIntegral() )
    problems.push_back( id + ": needs an integer version" );
  if ( !doc.isMember( "kind" ) || doc["kind"].asString() != "style_spec" )
    problems.push_back( id + ": kind must be \"style_spec\"" );

  const std::string applies = doc.isMember( "applies_to" ) && doc["applies_to"].isString()
                                ? doc["applies_to"].asString()
                                : "";
  if ( applies != "raster" && applies != "vector" && applies != "any" )
    problems.push_back( id + ": applies_to must be raster|vector|any" );

  if ( doc.isMember( "raster" ) )
    checkRaster( doc["raster"], problems );
  if ( doc.isMember( "vector" ) )
    checkVector( doc["vector"], problems );
  if ( !doc.isMember( "raster" ) && !doc.isMember( "vector" ) )
    problems.push_back( id + ": needs a raster and/or vector style block" );

  if ( doc.isMember( "semantics" ) )
  {
    if ( !doc["semantics"].isArray() )
      problems.push_back( id + ": semantics must be an array of strings" );
    else if ( static_cast<int>( doc["semantics"].size() ) > kMaxSemantics )
      problems.push_back( id + ": semantics exceeds the " + std::to_string( kMaxSemantics ) +
                          " entry budget" );
  }
  if ( doc.isMember( "token_set_ref" ) && !doc["token_set_ref"].isString() )
    problems.push_back( id + ": token_set_ref must be a string" );

  // Platform 6.0 (Milestone E): semantic applicability surface.
  for ( const auto &problem : validateStyleApplicability( doc ) )
    problems.push_back( problem );
  for ( const auto &problem : validateStyleSemantics( doc ) )
    problems.push_back( problem );

  return problems;
}

Json::Value resolveStyleTokens( const Json::Value &styleSpec, const Json::Value &tokens,
                                std::vector<std::string> *problems )
{
  std::vector<std::string> local;
  Json::Value resolved = resolveTokensRecursive( styleSpec, tokens, local );
  if ( problems )
    *problems = std::move( local );
  return resolved;
}

Json::Value compactStyleSummary( const Json::Value &styleSpec )
{
  Json::Value out( Json::objectValue );
  out["id"] = styleSpec.get( "id", "" );
  out["version"] = styleSpec.get( "version", 1 );
  out["applies_to"] = styleSpec.get( "applies_to", "" );
  std::string renderer = "";
  if ( styleSpec.isMember( "raster" ) && styleSpec["raster"].isMember( "renderertype" ) )
    renderer = styleSpec["raster"]["renderertype"].asString();
  else if ( styleSpec.isMember( "vector" ) && styleSpec["vector"].isMember( "renderertype" ) )
    renderer = styleSpec["vector"]["renderertype"].asString();
  out["renderer"] = renderer;
  if ( styleSpec.isMember( "semantics" ) )
    out["semantics"] = styleSpec["semantics"];
  // Description truncated to keep search responses inside the token budget.
  std::string description = styleSpec.isMember( "description" ) && styleSpec["description"].isString()
                              ? styleSpec["description"].asString()
                              : std::string();
  if ( description.size() > 160 )
    description = description.substr( 0, 157 ) + "...";
  out["description"] = description;
  return out;
}

// ---------------------------------------------------------------------------
// StyleRegistry
// ---------------------------------------------------------------------------

StyleRegistry &StyleRegistry::instance()
{
  static StyleRegistry registry;
  return registry;
}

void StyleRegistry::setDirectory( const QString &dir )
{
  QMutexLocker lock( &mMutex );
  mDirectory = dir;
  mLoaded = false;
}

QString StyleRegistry::directory() const
{
  QMutexLocker lock( &mMutex );
  return mDirectory;
}

void StyleRegistry::ensureLoadedLocked() const
{
  if ( mLoaded )
    return;
  mLoaded = true;
  mStyles.clear();
  mLoadProblems.clear();

  QString base = mDirectory;
  if ( base.isEmpty() )
    base = defaultStylesDir();
  QDir dir( base );
  const QString stylesPath =
    dir.exists( QStringLiteral( "styles" ) ) ? dir.filePath( QStringLiteral( "styles" ) ) : base;
  const QFileInfoList entries =
    QDir( stylesPath ).entryInfoList( QStringList() << QStringLiteral( "*.json" ), QDir::Files );
  for ( const QFileInfo &entry : entries )
  {
    Json::Value doc;
    QString parseError;
    if ( !parseJsonFile( entry.absoluteFilePath(), &doc, &parseError ) )
    {
      mLoadProblems << parseError;
      continue;
    }
    const auto problems = validateStyleSpec( doc );
    if ( doc.isObject() && doc.isMember( "id" ) && problems.empty() )
      mStyles.insert( QString::fromStdString( doc["id"].asString() ), doc );
    else
      mLoadProblems << QString::fromStdString( entry.fileName().toStdString() + ": " +
                                               ( problems.empty() ? "missing id"
                                                                  : problems.front() ) );
  }
  if ( mStyles.isEmpty() )
    loadEmbeddedDefaults();
}

void StyleRegistry::loadEmbeddedDefaults() const
{
  // Minimal safety set: a discrete land-cover palette and a diverging change
  // style so headless runs can render something meaningful without the data dir.
  Json::Value landcover( Json::objectValue );
  landcover["schema_version"] = "1.0";
  landcover["kind"] = "style_spec";
  landcover["id"] = "style.landcover-classes";
  landcover["version"] = 1;
  landcover["description"] = "Embedded fallback discrete land-cover classes.";
  landcover["applies_to"] = "raster";
  landcover["token_set_ref"] = kDefaultTokenSetId;
  landcover["raster"]["renderertype"] = "singleband_pseudocolor";
  landcover["raster"]["band"] = 1;
  landcover["raster"]["classification"]["mode"] = "discrete";
  Json::Value classes( Json::arrayValue );
  const std::vector<std::tuple<int, const char *, const char *>> entries = {
    { 1, "water", "#4a7fb5" },   { 2, "forest", "#2d6a2d" }, { 3, "cropland", "#d9c27a" },
    { 4, "urban", "#b04a4a" },   { 5, "bare", "#c9b8a3" },   { 6, "grass", "#8fbf6b" },
  };
  for ( const auto &[value, label, color] : entries )
  {
    Json::Value entry( Json::objectValue );
    entry["min"] = value;
    entry["max"] = value;
    entry["label"] = label;
    entry["color"] = color;
    classes.append( entry );
  }
  landcover["raster"]["classification"]["classes"] = classes;
  landcover["semantics"] = Json::Value( Json::arrayValue );
  landcover["semantics"].append( "landcover" );
  mStyles.insert( QString::fromStdString( landcover["id"].asString() ), landcover );

  Json::Value change( Json::objectValue );
  change["schema_version"] = "1.0";
  change["kind"] = "style_spec";
  change["id"] = "style.change-gain-loss";
  change["version"] = 1;
  change["description"] = "Embedded fallback diverging gain/loss change style.";
  change["applies_to"] = "raster";
  change["token_set_ref"] = kDefaultTokenSetId;
  change["raster"]["renderertype"] = "singleband_pseudocolor";
  change["raster"]["band"] = 1;
  change["raster"]["classification"]["mode"] = "discrete";
  Json::Value changeClasses( Json::arrayValue );
  const std::vector<std::tuple<int, const char *, const char *>> changeEntries = {
    { -1, "loss", "#d73027" }, { 0, "stable", "#f5f5f5" }, { 1, "gain", "#1a9850" },
  };
  for ( const auto &[value, label, color] : changeEntries )
  {
    Json::Value entry( Json::objectValue );
    entry["min"] = value;
    entry["max"] = value;
    entry["label"] = label;
    entry["color"] = color;
    changeClasses.append( entry );
  }
  change["raster"]["classification"]["classes"] = changeClasses;
  change["semantics"] = Json::Value( Json::arrayValue );
  change["semantics"].append( "change" );
  mStyles.insert( QString::fromStdString( change["id"].asString() ), change );
}

Json::Value StyleRegistry::styles() const
{
  QMutexLocker lock( &mMutex );
  ensureLoadedLocked();
  Json::Value list( Json::arrayValue );
  for ( auto it = mStyles.constBegin(); it != mStyles.constEnd(); ++it )
    list.append( it.value() );
  return list;
}

Json::Value StyleRegistry::find( const QString &id ) const
{
  QMutexLocker lock( &mMutex );
  ensureLoadedLocked();
  return mStyles.value( id, Json::Value() );
}

bool StyleRegistry::registerStyle( Json::Value doc, QString *error )
{
  const auto problems = validateStyleSpec( doc );
  if ( !problems.empty() )
  {
    if ( error )
      *error = QString::fromStdString( problems.front() );
    return false;
  }
  QMutexLocker lock( &mMutex );
  ensureLoadedLocked();
  mStyles.insert( QString::fromStdString( doc["id"].asString() ), std::move( doc ) );
  return true;
}

QStringList StyleRegistry::loadProblems() const
{
  QMutexLocker lock( &mMutex );
  ensureLoadedLocked();
  return mLoadProblems;
}

void StyleRegistry::reload()
{
  QMutexLocker lock( &mMutex );
  mLoaded = false;
}

} // namespace sicnu::agent::cartography
