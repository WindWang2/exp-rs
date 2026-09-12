// src/agent/harness/lab_spec.cpp
#include "lab_spec.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QProcessEnvironment>

#include <json/json.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>
#include <utility>

namespace sicnu::agent::harness {

namespace {

bool parseIntUtf8( const std::string &text, int &value )
{
  if ( text.empty() )
    return false;
  try
  {
    size_t consumed = 0;
    const int parsed = std::stoi( text, &consumed );
    if ( consumed != text.size() )
      return false;
    value = parsed;
    return true;
  }
  catch ( ... )
  {
    return false;
  }
}

/// "第3步" / "第三步" / "step 3" / "step three" → 3. 0 when nothing matches.
int stepNumberFromMessage( const std::string &message )
{
  // 第N步 (arabic numerals, 1–2 digits).
  size_t pos = message.find( "第" );
  while ( pos != std::string::npos )
  {
    size_t end = pos + 3; // strlen("第") == 3 in UTF-8
    int digits = 0;
    int value = 0;
    while ( end < message.size() && message[end] >= '0' && message[end] <= '9' && digits < 2 )
    {
      value = value * 10 + ( message[end] - '0' );
      ++digits;
      ++end;
    }
    if ( digits > 0 && message.compare( end, 3, "步" ) == 0 && value > 0 )
      return value;
    pos = message.find( "第", end );
  }

  // 第<中文数字>步 (一…十, with 两 as an alias of 二).
  pos = message.find( "第" );
  while ( pos != std::string::npos )
  {
    static const struct { const char *text; int value; } kCnDigits[] = {
      { "一", 1 }, { "二", 2 }, { "两", 2 }, { "三", 3 }, { "四", 4 }, { "五", 5 },
      { "六", 6 }, { "七", 7 }, { "八", 8 }, { "九", 9 }, { "十", 10 },
    };
    const size_t digitStart = pos + 3;
    for ( const auto &digit : kCnDigits )
    {
      if ( message.compare( digitStart, strlen( digit.text ), digit.text ) == 0 &&
           message.compare( digitStart + strlen( digit.text ), 3, "步" ) == 0 )
        return digit.value;
    }
    pos = message.find( "第", digitStart );
  }

  // English: "step N".
  static const std::string kStepLower = "step ";
  std::string lowered = message;
  std::transform( lowered.begin(), lowered.end(), lowered.begin(),
                  []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
  pos = lowered.find( kStepLower );
  while ( pos != std::string::npos )
  {
    size_t end = pos + kStepLower.size();
    int digits = 0;
    int value = 0;
    while ( end < message.size() && message[end] >= '0' && message[end] <= '9' && digits < 2 )
    {
      value = value * 10 + ( message[end] - '0' );
      ++digits;
      ++end;
    }
    if ( digits > 0 && value > 0 )
      return value;
    pos = lowered.find( kStepLower, end );
  }

  static const char *const kEnDigits[] = { "one", "two", "three", "four", "five",
                                           "six", "seven", "eight", "nine", "ten" };
  pos = lowered.find( kStepLower );
  while ( pos != std::string::npos )
  {
    const size_t digitStart = pos + kStepLower.size();
    for ( int i = 0; i < 10; ++i )
    {
      if ( lowered.compare( digitStart, strlen( kEnDigits[ i ] ), kEnDigits[ i ] ) == 0 )
        return i + 1;
    }
    pos = lowered.find( kStepLower, digitStart );
  }
  return 0;
}

} // namespace

LabSpecCatalog &LabSpecCatalog::instance()
{
  static LabSpecCatalog catalog;
  return catalog;
}

void LabSpecCatalog::setDirectory( const std::string &directory )
{
  mDirectory = directory;
}

std::string LabSpecCatalog::directory() const
{
  return mDirectory.empty() ? defaultDirectory() : mDirectory;
}

std::string LabSpecCatalog::defaultDirectory() const
{
  const QString envDir =
    QProcessEnvironment::systemEnvironment().value( QStringLiteral( "SICNU_LAB_SPEC_DIR" ) );
  if ( !envDir.isEmpty() )
    return envDir.toStdString();

  const QDir cwdLabs( QDir::current().filePath( QStringLiteral( "data/labs" ) ) );
  if ( cwdLabs.exists() )
    return cwdLabs.absolutePath().toStdString();

  if ( QCoreApplication::instance() )
  {
    const QDir appLabs( QCoreApplication::applicationDirPath() +
                        QStringLiteral( "/../data/labs" ) );
    if ( appLabs.exists() )
      return appLabs.absolutePath().toStdString();
  }

#ifdef SICNU_SOURCE_DIR
  {
    const QDir sourceLabs(
      QDir( QString::fromUtf8( SICNU_SOURCE_DIR ) ).filePath( QStringLiteral( "data/labs" ) ) );
    if ( sourceLabs.exists() )
      return sourceLabs.absolutePath().toStdString();
  }
#endif
  return std::string();
}

int LabSpecCatalog::reload()
{
  mLoaded = false;
  mSpecs = Json::Value( Json::objectValue );
  mOrder.clear();
  mLoadProblems.clear();

  const std::string dir = directory();
  if ( dir.empty() || !QDir( QString::fromStdString( dir ) ).exists() )
  {
    mLoadProblems.push_back( "lab spec directory not found: (D2 data/labs not present?)" );
    return 0;
  }

  QDirIterator it( QString::fromStdString( dir ), { QStringLiteral( "*.lab.json" ) },
                   QDir::Files );
  int loaded = 0;
  while ( it.hasNext() )
  {
    const QString path = it.next();
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) )
    {
      mLoadProblems.push_back( it.fileName().toStdString() + ": unreadable" );
      continue;
    }
    const QByteArray bytes = file.readAll();
    Json::Value spec;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    if ( !reader->parse( bytes.constData(), bytes.constData() + bytes.size(), &spec, &errors ) ||
         !spec.isObject() )
    {
      mLoadProblems.push_back( it.fileName().toStdString() + ": invalid JSON" );
      continue;
    }
    const std::string id = spec.get( "id", "" ).asString();
    if ( id.empty() )
    {
      mLoadProblems.push_back( it.fileName().toStdString() + ": missing id" );
      continue;
    }
    const Json::Value &steps = spec["steps"];
    if ( !steps.isArray() || steps.empty() )
    {
      mLoadProblems.push_back( it.fileName().toStdString() + ": no steps" );
      continue;
    }
    if ( mSpecs.isMember( id ) )
    {
      mLoadProblems.push_back( it.fileName().toStdString() + ": duplicate lab id " + id );
      continue;
    }
    mSpecs[ id ] = std::move( spec );
    mOrder.push_back( id );
    ++loaded;
  }
  std::sort( mOrder.begin(), mOrder.end() );
  mLoaded = true;
  if ( loaded == 0 )
    mLoadProblems.push_back( "directory contains no *.lab.json specs: " + dir );
  return loaded;
}

std::string LabSpecCatalog::status() const
{
  return ( mLoaded && !mOrder.empty() ) ? "ok" : "unavailable";
}

std::vector<std::string> LabSpecCatalog::loadProblems() const
{
  return mLoadProblems;
}

std::vector<std::string> LabSpecCatalog::labIds() const
{
  return mOrder;
}

Json::Value LabSpecCatalog::lab( const std::string &labId ) const
{
  if ( !mLoaded || labId.empty() || !mSpecs.isMember( labId ) )
    return Json::Value();
  return mSpecs[ labId ];
}

Json::Value LabSpecCatalog::stepDoc( const std::string &labId, int stepIndex ) const
{
  const Json::Value spec = lab( labId );
  if ( spec.isNull() || stepIndex < 0 || stepIndex >= static_cast<int>( spec["steps"].size() ) )
    return Json::Value();

  const Json::Value &step = spec["steps"][ stepIndex ];
  Json::Value doc( Json::objectValue );
  doc["index"] = stepIndex;
  doc["number"] = stepIndex + 1; // human-facing step number (1-based)
  doc["title_zh"] = step.get( "title_zh", step.get( "title", "" ) ).asString();
  doc["description_zh"] = step.get( "description_zh", "" ).asString();
  if ( step.isMember( "teaching_note" ) )
    doc["teaching_note"] = step["teaching_note"];
  if ( step.isMember( "completion_hint" ) )
    doc["completion_hint"] = step["completion_hint"];
  if ( step.isMember( "operator_id" ) )
    doc["operator_id"] = step["operator_id"];
  // Parameter NAMES may guide a hint; parameter VALUES are the solution and
  // never leave this module toward a student.
  Json::Value paramNames( Json::arrayValue );
  if ( step.isMember( "params" ) && step["params"].isObject() )
    for ( const std::string &name : step["params"].getMemberNames() )
      paramNames.append( name );
  doc["param_names"] = std::move( paramNames );
  return doc;
}

int LabSpecCatalog::stepIndexFromMessage( const std::string &message, int stepCount )
{
  const int number = stepNumberFromMessage( message );
  if ( number <= 0 || stepCount <= 0 || number > stepCount )
    return -1;
  return number - 1;
}

} // namespace sicnu::agent::harness
