// src/agent/harness/lab_glossary.cpp
#include "lab_glossary.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcessEnvironment>

#include <json/json.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <utility>

namespace sicnu::agent::harness {

namespace {

/// The glossary auto-loads on first query (same contract as
/// CapabilityKnowledge/RecipeCatalog) so answers cite terms when D6 data is
/// actually present.
void ensureLoaded( LabGlossary &glossary )
{
  if ( !glossary.loaded() )
    glossary.reload();
}

std::string loweredKey( const std::string &text )
{
  std::string out = text;
  std::transform( out.begin(), out.end(), out.begin(),
                  []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
  return out;
}

} // namespace

LabGlossary &LabGlossary::instance()
{
  static LabGlossary glossary;
  return glossary;
}

void LabGlossary::setFilePath( const std::string &path )
{
  mFilePath = path;
  mLoaded = false; // next query reloads the new file
}

std::string LabGlossary::filePath() const
{
  return mFilePath.empty() ? defaultFilePath() : mFilePath;
}

std::string LabGlossary::defaultFilePath() const
{
  const QString envPath =
    QProcessEnvironment::systemEnvironment().value( QStringLiteral( "SICNU_RS_GLOSSARY" ) );
  if ( !envPath.isEmpty() )
    return envPath.toStdString();

  auto exists = []( const QString &path ) {
    return QFile::exists( path ) ? path.toStdString() : std::string();
  };
  if ( std::string p = exists( QDir::current().filePath( QStringLiteral( "data/terms/rs_glossary.json" ) ) );
       !p.empty() )
    return p;
  if ( QCoreApplication::instance() )
  {
    if ( std::string p = exists( QCoreApplication::applicationDirPath() +
                                 QStringLiteral( "/../data/terms/rs_glossary.json" ) );
         !p.empty() )
      return p;
  }
#ifdef SICNU_SOURCE_DIR
  if ( std::string p = exists( QDir( QString::fromUtf8( SICNU_SOURCE_DIR ) ).filePath(
                                 QStringLiteral( "data/terms/rs_glossary.json" ) ) );
       !p.empty() )
    return p;
#endif
  return std::string();
}

int LabGlossary::reload()
{
  mLoaded = false;
  mByLower = Json::Value( Json::objectValue );
  mLoadProblems.clear();

  const std::string path = filePath();
  if ( path.empty() )
  {
    mLoadProblems.push_back( "rs_glossary.json not found (D6 data/terms not present?)" );
    return 0;
  }
  QFile file( QString::fromStdString( path ) );
  if ( !file.open( QIODevice::ReadOnly ) )
  {
    mLoadProblems.push_back( "rs_glossary.json unreadable: " + path );
    return 0;
  }
  const QByteArray bytes = file.readAll();
  Json::Value root;
  Json::CharReaderBuilder builder;
  std::string errors;
  std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
  if ( !reader->parse( bytes.constData(), bytes.constData() + bytes.size(), &root, &errors ) ||
       !root.isArray() )
  {
    mLoadProblems.push_back( "rs_glossary.json invalid: " + errors );
    return 0;
  }

  int loaded = 0;
  for ( const Json::Value &entry : root )
  {
    if ( !entry.isObject() )
      continue;
    const std::string en = entry.get( "en", "" ).asString();
    const std::string zh = entry.get( "zh", "" ).asString();
    if ( en.empty() || zh.empty() )
      continue;
    mByLower[ loweredKey( en ) ] = entry;
    mByLower[ loweredKey( zh ) ] = entry;
    if ( entry.isMember( "alias" ) && entry["alias"].isArray() )
      for ( const Json::Value &alias : entry["alias"] )
        if ( alias.isString() )
          mByLower[ loweredKey( alias.asString() ) ] = entry;
    ++loaded;
  }
  if ( loaded == 0 )
  {
    mLoadProblems.push_back( "rs_glossary.json contains no usable entries" );
    return 0;
  }
  mLoaded = true;
  return loaded;
}

std::string LabGlossary::status() const
{
  ensureLoaded( const_cast<LabGlossary &>( *this ) );
  return mLoaded ? "ok" : "unavailable";
}

std::vector<std::string> LabGlossary::loadProblems() const
{
  return mLoadProblems;
}

Json::Value LabGlossary::term( const std::string &word ) const
{
  ensureLoaded( const_cast<LabGlossary &>( *this ) );
  if ( !mLoaded || word.empty() )
    return Json::Value();
  const Json::Value &entry = mByLower[ loweredKey( word ) ];
  return entry.isObject() ? entry : Json::Value();
}

} // namespace sicnu::agent::harness
