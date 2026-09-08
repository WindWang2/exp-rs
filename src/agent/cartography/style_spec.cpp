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
    const std::string value = node.asString();
    if ( !isTokenReference( value ) )
      return node;
    const std::string path = tokenReferencePath( value );
    const Json::Value resolved = tokenValue( tokens, path );
    if ( resolved.isNull() )
    {
      problems.push_back( "unresolvable token reference '" + value + "'" );
      return node;
    }
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
