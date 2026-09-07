// src/agent/cartography/design_tokens.cpp
#include "design_tokens.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QJsonDocument>
#include <QMutexLocker>

#include <json/reader.h>

#include <cmath>
#include <sstream>

namespace sicnu::agent::cartography {

const char *const kDefaultTokenSetId = "scientific-light";

namespace {

QString defaultTokensDir()
{
  if ( qEnvironmentVariableIsSet( "SICNU_CARTOGRAPHY_DIR" ) )
    return qEnvironmentVariable( "SICNU_CARTOGRAPHY_DIR" );
  return QDir::current().filePath( QStringLiteral( "data/cartography" ) );
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

bool isHexColor( const std::string &value )
{
  return QColor::isValidColor( QString::fromStdString( value ) );
}

void checkStyleEntry( const std::string &name, const Json::Value &style,
                      std::vector<std::string> &problems )
{
  if ( !style.isObject() )
  {
    problems.push_back( "typography.styles." + name + " must be an object" );
    return;
  }
  if ( !style.isMember( "size_pt" ) || !style["size_pt"].isNumeric() || style["size_pt"].asDouble() <= 0 )
    problems.push_back( "typography.styles." + name + " needs a positive size_pt" );
  if ( style.isMember( "weight" ) && style["weight"].asString() != "normal" &&
       style["weight"].asString() != "bold" )
    problems.push_back( "typography.styles." + name + ".weight must be normal|bold" );
}

} // namespace

TokenSetRegistry &TokenSetRegistry::instance()
{
  static TokenSetRegistry registry;
  return registry;
}

std::vector<std::string> validateTokenSet( const Json::Value &doc )
{
  std::vector<std::string> problems;
  if ( !doc.isObject() )
    return { "token set must be an object" };
  const std::string id = doc.isMember( "id" ) && doc["id"].isString() ? doc["id"].asString() : "";
  if ( id.empty() )
    problems.push_back( "token set needs a string id" );
  if ( !doc.isMember( "version" ) || !doc["version"].isIntegral() )
    problems.push_back( id + ": needs an integer version" );

  if ( doc.isMember( "typography" ) )
  {
    const Json::Value &typography = doc["typography"];
    if ( !typography.isObject() )
      problems.push_back( id + ": typography must be an object" );
    else
    {
      if ( typography.isMember( "styles" ) )
      {
        if ( !typography["styles"].isObject() )
          problems.push_back( id + ": typography.styles must be an object" );
        else
          for ( const auto &name : typography["styles"].getMemberNames() )
            checkStyleEntry( name, typography["styles"][name], problems );
      }
      if ( typography.isMember( "cjk_fallbacks" ) && !typography["cjk_fallbacks"].isArray() )
        problems.push_back( id + ": typography.cjk_fallbacks must be an array" );
    }
  }
  for ( const char *family : { "spacing", "lines", "colors", "palettes", "furniture", "chart" } )
  {
    if ( doc.isMember( family ) && !doc[family].isObject() )
      problems.push_back( id + ": " + family + " must be an object" );
  }
  if ( doc.isMember( "spacing" ) && doc["spacing"].isObject() )
  {
    for ( const auto &key : doc["spacing"].getMemberNames() )
    {
      const Json::Value &value = doc["spacing"][key];
      if ( !value.isNumeric() || value.asDouble() < 0 )
        problems.push_back( id + ": spacing." + key + " must be a non-negative number" );
    }
  }
  if ( doc.isMember( "colors" ) && doc["colors"].isObject() )
  {
    for ( const auto &key : doc["colors"].getMemberNames() )
    {
      const Json::Value &value = doc["colors"][key];
      if ( !value.isString() || !isHexColor( value.asString() ) )
        problems.push_back( id + ": colors." + key + " must be a hex color string" );
    }
  }
  if ( doc.isMember( "palettes" ) && doc["palettes"].isObject() )
  {
    for ( const auto &key : doc["palettes"].getMemberNames() )
    {
      const Json::Value &value = doc["palettes"][key];
      if ( !value.isArray() )
      {
        problems.push_back( id + ": palettes." + key + " must be an array of hex colors" );
        continue;
      }
      for ( const auto &color : value )
        if ( !color.isString() || !isHexColor( color.asString() ) )
          problems.push_back( id + ": palettes." + key + " entries must be hex colors" );
    }
  }
  if ( doc.isMember( "variants" ) )
  {
    if ( !doc["variants"].isObject() )
      problems.push_back( id + ": variants must be an object" );
    else
      for ( const auto &medium : doc["variants"].getMemberNames() )
        if ( !doc["variants"][medium].isObject() )
          problems.push_back( id + ": variants." + medium + " must be an object" );
  }
  return problems;
}

// ---------------------------------------------------------------------------
// TokenSetRegistry
// ---------------------------------------------------------------------------

void TokenSetRegistry::setDirectory( const QString &dir )
{
  QMutexLocker lock( &mMutex );
  mDirectory = dir;
  mLoaded = false;
}

QString TokenSetRegistry::directory() const
{
  QMutexLocker lock( &mMutex );
  return mDirectory;
}

void TokenSetRegistry::ensureLoadedLocked() const
{
  if ( mLoaded )
    return;
  mLoaded = true;
  mTokenSets.clear();

  QString base = mDirectory;
  if ( base.isEmpty() )
    base = defaultTokensDir();
  QDir dir( base );
  const QString tokensPath =
    dir.exists( QStringLiteral( "tokens" ) ) ? dir.filePath( QStringLiteral( "tokens" ) ) : base;
  const QFileInfoList entries =
    QDir( tokensPath ).entryInfoList( QStringList() << QStringLiteral( "*.json" ), QDir::Files );
  for ( const QFileInfo &entry : entries )
  {
    Json::Value doc;
    QString parseError;
    if ( !parseJsonFile( entry.absoluteFilePath(), &doc, &parseError ) )
      continue;
    if ( doc.isObject() && doc.isMember( "id" ) && validateTokenSet( doc ).empty() )
      mTokenSets.insert( QString::fromStdString( doc["id"].asString() ), doc );
  }
  if ( mTokenSets.isEmpty() )
    loadEmbeddedDefaults();
}

void TokenSetRegistry::loadEmbeddedDefaults() const
{
  // Minimal safety set mirroring data/cartography/tokens/scientific-light.json
  // so headless runs without the data dir still get sane typography.
  Json::Value doc( Json::objectValue );
  doc["id"] = kDefaultTokenSetId;
  doc["version"] = 1;
  doc["description"] = "Embedded fallback light scientific style.";
  doc["typography"]["font_family"] = "Arial";
  Json::Value fallbacks( Json::arrayValue );
  fallbacks.append( "Noto Sans CJK SC" );
  fallbacks.append( "WenQuanYi Zen Hei" );
  doc["typography"]["cjk_fallbacks"] = fallbacks;
  doc["typography"]["styles"]["title"]["size_pt"] = 20;
  doc["typography"]["styles"]["title"]["weight"] = "bold";
  doc["typography"]["styles"]["subtitle"]["size_pt"] = 13;
  doc["typography"]["styles"]["section"]["size_pt"] = 11;
  doc["typography"]["styles"]["section"]["weight"] = "bold";
  doc["typography"]["styles"]["body"]["size_pt"] = 9;
  doc["typography"]["styles"]["caption"]["size_pt"] = 8;
  doc["typography"]["styles"]["annotation"]["size_pt"] = 8;
  doc["typography"]["styles"]["source_note"]["size_pt"] = 7;
  doc["spacing"]["margin_mm"] = 12;
  doc["spacing"]["gutter_mm"] = 6;
  doc["spacing"]["furniture_gap_mm"] = 4;
  Json::Value qualitative( Json::arrayValue );
  for ( const char *hex : { "#0072b2", "#e69f00", "#009e73", "#d55e00", "#cc79a7",
                            "#56b4e9", "#f0e442", "#000000" } )
    qualitative.append( hex );
  doc["palettes"]["qualitative"] = qualitative;
  mTokenSets.insert( QString::fromStdString( doc["id"].asString() ), doc );
}

Json::Value TokenSetRegistry::tokenSets() const
{
  QMutexLocker lock( &mMutex );
  ensureLoadedLocked();
  Json::Value list( Json::arrayValue );
  for ( auto it = mTokenSets.constBegin(); it != mTokenSets.constEnd(); ++it )
    list.append( it.value() );
  return list;
}

Json::Value TokenSetRegistry::find( const QString &id ) const
{
  QMutexLocker lock( &mMutex );
  ensureLoadedLocked();
  return mTokenSets.value( id, Json::Value() );
}

bool TokenSetRegistry::registerTokenSet( Json::Value doc, QString *error )
{
  const auto problems = validateTokenSet( doc );
  if ( !problems.empty() )
  {
    if ( error )
      *error = QString::fromStdString( problems.front() );
    return false;
  }
  QMutexLocker lock( &mMutex );
  ensureLoadedLocked();
  mTokenSets.insert( QString::fromStdString( doc["id"].asString() ), std::move( doc ) );
  return true;
}

void TokenSetRegistry::reload()
{
  QMutexLocker lock( &mMutex );
  mLoaded = false;
}

// ---------------------------------------------------------------------------
// Resolution
// ---------------------------------------------------------------------------

Json::Value mergeTokenValues( const Json::Value &base, const Json::Value &overlay )
{
  if ( base.isObject() && overlay.isObject() )
  {
    Json::Value merged = base;
    for ( const auto &key : overlay.getMemberNames() )
    {
      if ( merged.isMember( key ) )
        merged[key] = mergeTokenValues( merged[key], overlay[key] );
      else
        merged[key] = overlay[key];
    }
    return merged;
  }
  return overlay;
}

Json::Value resolveTokenSet( const Json::Value &specOrStyle )
{
  const Json::Value style = specOrStyle.isMember( "style" ) && specOrStyle["style"].isObject()
                              ? specOrStyle["style"]
                              : specOrStyle;
  std::string setId = style.isMember( "token_set" ) && style["token_set"].isString()
                        ? style["token_set"].asString()
                        : kDefaultTokenSetId;
  const std::string medium = style.isMember( "medium" ) && style["medium"].isString()
                               ? style["medium"].asString()
                               : "print";

  TokenSetRegistry &registry = TokenSetRegistry::instance();
  Json::Value tokens = registry.find( QString::fromStdString( setId ) );
  if ( tokens.isNull() )
    setId = kDefaultTokenSetId; // honest fallback: resolved.token_set names what is in use
  tokens = registry.find( QString::fromStdString( setId ) );
  if ( tokens.isNull() )
  {
    // Registry cannot be empty (embedded fallback), but stay total.
    tokens = Json::Value( Json::objectValue );
    tokens["id"] = kDefaultTokenSetId;
  }

  if ( tokens.isMember( "variants" ) && tokens["variants"].isObject() &&
       tokens["variants"].isMember( medium ) && tokens["variants"][medium].isObject() )
    tokens = mergeTokenValues( tokens, tokens["variants"][medium] );
  if ( tokens.isMember( "variants" ) )
    tokens.removeMember( "variants" );

  if ( style.isMember( "overrides" ) && style["overrides"].isObject() )
    tokens = mergeTokenValues( tokens, style["overrides"] );

  tokens["resolved"]["token_set"] = setId;
  tokens["resolved"]["medium"] = medium;
  return tokens;
}

// ---------------------------------------------------------------------------
// Typed accessors
// ---------------------------------------------------------------------------

Json::Value tokenValue( const Json::Value &tokens, const std::string &dottedPath )
{
  const Json::Value *current = &tokens;
  std::string segment;
  std::istringstream stream( dottedPath );
  while ( std::getline( stream, segment, '.' ) )
  {
    if ( !current->isObject() || !current->isMember( segment ) )
      return Json::Value();
    current = &( *current )[segment];
  }
  return *current;
}

std::string tokenString( const Json::Value &tokens, const std::string &dottedPath,
                         const std::string &fallback )
{
  const Json::Value value = tokenValue( tokens, dottedPath );
  return value.isString() ? value.asString() : fallback;
}

double tokenNumber( const Json::Value &tokens, const std::string &dottedPath, double fallback )
{
  const Json::Value value = tokenValue( tokens, dottedPath );
  return value.isNumeric() ? value.asDouble() : fallback;
}

bool tokenBool( const Json::Value &tokens, const std::string &dottedPath, bool fallback )
{
  const Json::Value value = tokenValue( tokens, dottedPath );
  return value.isBool() ? value.asBool() : fallback;
}

Json::Value tokenPalette( const Json::Value &tokens, const std::string &name )
{
  Json::Value palette = tokenValue( tokens, "palettes." + name );
  if ( !palette.isArray() )
    palette = tokenValue( tokens, "palettes.qualitative" );
  return palette.isArray() ? palette : Json::Value( Json::arrayValue );
}

Json::Value tokenTextStyle( const Json::Value &tokens, const std::string &style, double fallbackPt )
{
  Json::Value entry = tokenValue( tokens, "typography.styles." + style );
  if ( !entry.isObject() )
    entry = tokenValue( tokens, "typography.styles.body" );
  if ( !entry.isObject() )
  {
    entry = Json::Value( Json::objectValue );
    entry["size_pt"] = fallbackPt;
    entry["weight"] = "normal";
    return entry;
  }
  if ( !entry.isMember( "size_pt" ) || !entry["size_pt"].isNumeric() || entry["size_pt"].asDouble() <= 0 )
    entry["size_pt"] = fallbackPt;
  if ( !entry.isMember( "weight" ) )
    entry["weight"] = "normal";
  return entry;
}

void applyTokenFontFallbacks( const Json::Value &tokens )
{
  const std::string family = tokenString( tokens, "typography.font_family" );
  if ( family.empty() )
    return;
  const Json::Value fallbacks = tokenValue( tokens, "typography.cjk_fallbacks" );
  QStringList substitutions;
  if ( fallbacks.isArray() )
  {
    for ( const auto &fallback : fallbacks )
      if ( fallback.isString() )
        substitutions << QString::fromStdString( fallback.asString() );
  }
  if ( !substitutions.isEmpty() )
    QFont::insertSubstitutions( QString::fromStdString( family ), substitutions );
}

} // namespace sicnu::agent::cartography
