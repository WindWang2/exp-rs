// src/agent/cartography/solution_registry.cpp
#include "solution_registry.h"

#include "design_tokens.h"

#include <QDir>
#include <QFile>
#include <QMutexLocker>

#include <json/reader.h>

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>

namespace sicnu::agent::cartography {

namespace {

constexpr size_t kMaxDocumentBytes = 512 * 1024;
constexpr int kMaxContracts = 16;
constexpr int kMaxArtifacts = 16;
constexpr int kMaxFacets = 24;
constexpr int kMaxKeywords = 24;

const char *const kModalities[] = { "optical", "sar", "temporal", "terrain", "vector", "model", "multimodal" };
const char *const kQualityGrades[] = { "experimental", "reviewed", "certified" };

QString defaultSolutionsDir()
{
  if ( qEnvironmentVariableIsSet( "SICNU_SOLUTIONS_DIR" ) )
    return qEnvironmentVariable( "SICNU_SOLUTIONS_DIR" );
  if ( qEnvironmentVariableIsSet( "SICNU_SOURCE_DIR" ) )
    return QDir( qEnvironmentVariable( "SICNU_SOURCE_DIR" ) ).filePath( QStringLiteral( "data/agent/solutions" ) );
  return QDir::current().filePath( QStringLiteral( "data/agent/solutions" ) );
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
      *error = QStringLiteral( "%1 exceeds the %2 KB solution budget" )
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

bool stringArrayBounded( const Json::Value &node, int max, std::vector<std::string> &problems,
                         const std::string &where )
{
  if ( !node.isArray() )
  {
    problems.push_back( where + " must be an array of strings" );
    return false;
  }
  if ( static_cast<int>( node.size() ) > max )
  {
    problems.push_back( where + " exceeds the " + std::to_string( max ) + " entry budget" );
    return false;
  }
  for ( const auto &entry : node )
    if ( !entry.isString() )
    {
      problems.push_back( where + " entries must be strings" );
      return false;
    }
  return true;
}

void checkInputContract( int index, const Json::Value &contract,
                         std::vector<std::string> &problems )
{
  const std::string where = "input_contracts[" + std::to_string( index ) + "]";
  if ( !contract.isObject() )
  {
    problems.push_back( where + " must be an object" );
    return;
  }
  if ( !contract.isMember( "name" ) || !contract["name"].isString() || contract["name"].asString().empty() )
    problems.push_back( where + ".name must be a non-empty string" );
  if ( !contract.isMember( "kind" ) || !contract["kind"].isString() )
    problems.push_back( where + ".kind must be a string" );
  if ( !contract.isMember( "required" ) || !contract["required"].isBool() )
    problems.push_back( where + ".required must be a boolean" );
  if ( contract.isMember( "accepted" ) )
    stringArrayBounded( contract["accepted"], kMaxFacets, problems, where + ".accepted" );
  if ( contract.isMember( "description" ) && !contract["description"].isString() )
    problems.push_back( where + ".description must be a string" );
}

void checkArtifact( int index, const Json::Value &artifact, std::vector<std::string> &problems )
{
  const std::string where = "expected_artifacts[" + std::to_string( index ) + "]";
  if ( !artifact.isObject() )
  {
    problems.push_back( where + " must be an object" );
    return;
  }
  if ( !artifact.isMember( "name" ) || !artifact["name"].isString() )
    problems.push_back( where + ".name must be a string" );
  if ( !artifact.isMember( "kind" ) || !artifact["kind"].isString() )
    problems.push_back( where + ".kind must be a string" );
}

} // namespace

bool isSolutionModality( const std::string &modality )
{
  for ( const char *candidate : kModalities )
    if ( modality == candidate )
      return true;
  return false;
}

bool isQualityGrade( const std::string &grade )
{
  for ( const char *candidate : kQualityGrades )
    if ( grade == candidate )
      return true;
  return false;
}

std::vector<std::string> validateSolutionTemplate( const Json::Value &doc,
                                                   const RefResolvers *resolvers )
{
  std::vector<std::string> problems;
  if ( !doc.isObject() )
    return { "solution template must be an object" };
  const std::string id = doc.isMember( "id" ) && doc["id"].isString() ? doc["id"].asString() : "";
  if ( id.empty() )
    problems.push_back( "solution template needs a string id" );
  if ( id.rfind( "solution.", 0 ) != 0 )
    problems.push_back( id + ": id must live in the \"solution.\" namespace" );
  if ( !doc.isMember( "version" ) || !doc["version"].isIntegral() )
    problems.push_back( id + ": needs an integer version" );
  if ( !doc.isMember( "kind" ) || doc["kind"].asString() != "solution_template" )
    problems.push_back( id + ": kind must be \"solution_template\"" );
  if ( !doc.isMember( "title" ) || !doc["title"].isString() )
    problems.push_back( id + ": needs a string title" );

  if ( doc.isMember( "tasks" ) )
    stringArrayBounded( doc["tasks"], kMaxFacets, problems, id + ": tasks" );
  else
    problems.push_back( id + ": needs a tasks array" );
  if ( doc.isMember( "modalities" ) )
  {
    if ( stringArrayBounded( doc["modalities"], kMaxFacets, problems, id + ": modalities" ) )
      for ( const auto &modality : doc["modalities"] )
        if ( !isSolutionModality( modality.asString() ) )
          problems.push_back( id + ": unknown modality '" + modality.asString() + "'" );
  }
  else
  {
    problems.push_back( id + ": needs a modalities array" );
  }
  if ( doc.isMember( "sensors" ) )
    stringArrayBounded( doc["sensors"], kMaxFacets, problems, id + ": sensors" );
  if ( doc.isMember( "keywords" ) )
    stringArrayBounded( doc["keywords"], kMaxKeywords, problems, id + ": keywords" );

  if ( doc.isMember( "input_contracts" ) )
  {
    if ( !doc["input_contracts"].isArray() )
    {
      problems.push_back( id + ": input_contracts must be an array" );
    }
    else
    {
      if ( static_cast<int>( doc["input_contracts"].size() ) > kMaxContracts )
        problems.push_back( id + ": input_contracts exceeds the " + std::to_string( kMaxContracts ) +
                            " entry budget" );
      int index = 0;
      for ( const auto &contract : doc["input_contracts"] )
        checkInputContract( index++, contract, problems );
    }
  }
  else
  {
    problems.push_back( id + ": needs input_contracts" );
  }

  for ( const char *ref : { "analysis_recipe", "map_template" } )
  {
    if ( !doc.isMember( ref ) || !doc[ref].isString() || doc[ref].asString().empty() )
      problems.push_back( id + ": needs a non-empty " + ref );
  }
  if ( doc.isMember( "report_template" ) && !doc["report_template"].isString() )
    problems.push_back( id + ": report_template must be a string" );
  if ( doc.isMember( "style_spec" ) && !doc["style_spec"].isObject() )
    problems.push_back( id + ": style_spec must be an object mapping roles to style ids" );

  if ( doc.isMember( "expected_artifacts" ) )
  {
    if ( !doc["expected_artifacts"].isArray() )
    {
      problems.push_back( id + ": expected_artifacts must be an array" );
    }
    else
    {
      int index = 0;
      for ( const auto &artifact : doc["expected_artifacts"] )
        checkArtifact( index++, artifact, problems );
    }
  }

  if ( doc.isMember( "quality_grade" ) &&
       !isQualityGrade( doc["quality_grade"].asString() ) )
    problems.push_back( id + ": quality_grade must be experimental|reviewed|certified" );
  if ( doc.isMember( "aliases" ) )
    stringArrayBounded( doc["aliases"], kMaxFacets, problems, id + ": aliases" );

  // Cross-domain reference resolution (only with wired resolvers).
  if ( resolvers )
  {
    const std::string recipe = doc.isMember( "analysis_recipe" ) && doc["analysis_recipe"].isString()
                                 ? doc["analysis_recipe"].asString()
                                 : std::string();
    if ( resolvers->recipe && !recipe.empty() && !resolvers->recipe( recipe ) )
      problems.push_back( id + ": unknown analysis_recipe '" + recipe + "'" );
    const std::string mapTemplate = doc.isMember( "map_template" ) && doc["map_template"].isString()
                                      ? doc["map_template"].asString()
                                      : std::string();
    if ( resolvers->mapTemplate && !mapTemplate.empty() && !resolvers->mapTemplate( mapTemplate ) )
      problems.push_back( id + ": unknown map_template '" + mapTemplate + "'" );
    if ( doc.isMember( "report_template" ) && doc["report_template"].isString() && resolvers->reportTemplate )
    {
      const std::string reportTemplate = doc["report_template"].asString();
      if ( !reportTemplate.empty() && !resolvers->reportTemplate( reportTemplate ) )
        problems.push_back( id + ": unknown report_template '" + reportTemplate + "'" );
    }
    if ( doc.isMember( "style_spec" ) && doc["style_spec"].isObject() && resolvers->style )
    {
      for ( const auto &role : doc["style_spec"].getMemberNames() )
      {
        const Json::Value &styleRef = doc["style_spec"][role];
        if ( styleRef.isString() && !styleRef.asString().empty() &&
             !resolvers->style( styleRef.asString() ) )
          problems.push_back( id + ": unknown style '" + styleRef.asString() + "' for role '" +
                              role + "'" );
      }
    }
  }
  return problems;
}

Json::Value resolveSolutionInheritance( const Json::Value &doc,
                                        const QMap<QString, Json::Value> &solutionsById,
                                        QStringList *problems )
{
  if ( !doc.isObject() )
    return Json::Value();
  if ( !doc.isMember( "extends" ) || !doc["extends"].isString() || doc["extends"].asString().empty() )
    return doc;

  // Walk the chain breadth-first, detecting cycles; merge parent-first so the
  // child wins. Depth is bounded by the map size, but keep an explicit cap.
  std::vector<Json::Value> chain{ doc };
  std::set<std::string> seen{ doc["id"].asString() };
  Json::Value cursor = doc;
  while ( cursor.isMember( "extends" ) && cursor["extends"].isString() &&
          !cursor["extends"].asString().empty() )
  {
    const std::string parentId = cursor["extends"].asString();
    if ( seen.count( parentId ) )
    {
      if ( problems )
        *problems << QString::fromStdString( doc["id"].asString() +
                                             ": inheritance cycle at '" + parentId + "'" );
      return Json::Value();
    }
    const Json::Value parent = solutionsById.value( QString::fromStdString( parentId ), Json::Value() );
    if ( parent.isNull() )
    {
      if ( problems )
        *problems << QString::fromStdString( doc["id"].asString() + ": unknown parent '" +
                                             parentId + "'" );
      return Json::Value();
    }
    seen.insert( parentId );
    chain.push_back( parent );
    cursor = parent;
  }

  Json::Value merged;
  for ( auto it = chain.rbegin(); it != chain.rend(); ++it )
  {
    if ( merged.isNull() )
    {
      merged = *it;
      continue;
    }
    merged = mergeTokenValues( merged, *it ); // child fields win over the parent chain
  }
  return merged;
}

Json::Value compactSolutionSummary( const Json::Value &solution )
{
  Json::Value out( Json::objectValue );
  out["id"] = solution.get( "id", "" );
  out["version"] = solution.get( "version", 1 );
  out["title"] = solution.get( "title", "" );
  if ( solution.isMember( "tasks" ) )
    out["tasks"] = solution["tasks"];
  if ( solution.isMember( "modalities" ) )
    out["modalities"] = solution["modalities"];
  if ( solution.isMember( "sensors" ) )
    out["sensors"] = solution["sensors"];
  if ( solution.isMember( "quality_grade" ) )
    out["quality_grade"] = solution["quality_grade"];
  out["analysis_recipe"] = solution.get( "analysis_recipe", "" );
  out["map_template"] = solution.get( "map_template", "" );

  Json::Value inputs( Json::arrayValue );
  if ( solution.isMember( "input_contracts" ) && solution["input_contracts"].isArray() )
  {
    for ( const auto &contract : solution["input_contracts"] )
    {
      Json::Value summary( Json::objectValue );
      summary["name"] = contract.get( "name", "" );
      summary["kind"] = contract.get( "kind", "" );
      summary["required"] = contract.get( "required", false );
      inputs.append( summary );
    }
  }
  out["inputs"] = inputs;
  return out;
}

Json::Value searchSolutions( const Json::Value &solutions, const SolutionQuery &query )
{
  std::vector<Json::Value> hits;
  const std::string task = query.task;
  const std::string modality = query.modality;
  const std::string sensor = query.sensor;
  std::string keyword = query.keyword;
  std::transform( keyword.begin(), keyword.end(), keyword.begin(),
                  []( unsigned char c ) { return std::tolower( c ); } );
  const std::string quality = query.quality;
  const std::string family = query.family;

  for ( const Json::Value &doc : solutions )
  {
    if ( !task.empty() )
    {
      bool match = false;
      if ( doc.isMember( "tasks" ) && doc["tasks"].isArray() )
        for ( const auto &candidate : doc["tasks"] )
          match = match || candidate.asString().find( task ) != std::string::npos;
      if ( !match )
        continue;
    }
    if ( !modality.empty() )
    {
      bool match = false;
      if ( doc.isMember( "modalities" ) && doc["modalities"].isArray() )
        for ( const auto &candidate : doc["modalities"] )
          match = match || candidate.asString() == modality;
      if ( !match )
        continue;
    }
    if ( !sensor.empty() )
    {
      bool match = false;
      if ( doc.isMember( "sensors" ) && doc["sensors"].isArray() )
        for ( const auto &candidate : doc["sensors"] )
          match = match || candidate.asString().find( sensor ) != std::string::npos;
      if ( !match )
        continue;
    }
    if ( !quality.empty() && doc.get( "quality_grade", "" ).asString() != quality )
      continue;
    if ( !family.empty() && doc.get( "family", "" ).asString() != family )
      continue;
    if ( !keyword.empty() )
    {
      std::string haystack = doc.get( "title", "" ).asString() + " " +
                             doc.get( "description", "" ).asString();
      if ( doc.isMember( "keywords" ) && doc["keywords"].isArray() )
        for ( const auto &candidate : doc["keywords"] )
          haystack += " " + candidate.asString();
      std::transform( haystack.begin(), haystack.end(), haystack.begin(),
                      []( unsigned char c ) { return std::tolower( c ); } );
      if ( haystack.find( keyword ) == std::string::npos )
        continue;
    }
    hits.push_back( doc );
  }

  const int pageSize = std::clamp( query.pageSize, 1, 50 );
  const int page = std::max( query.page, 0 );
  Json::Value out( Json::objectValue );
  out["total"] = static_cast<int>( hits.size() );
  out["page"] = page;
  out["page_size"] = pageSize;
  Json::Value items( Json::arrayValue );
  const int begin = page * pageSize;
  const int end = std::min( static_cast<int>( hits.size() ), begin + pageSize );
  for ( int index = begin; index < end; ++index )
    items.append( compactSolutionSummary( hits[index] ) );
  out["items"] = items;
  out["next_page"] = end < static_cast<int>( hits.size() ) ? Json::Value( page + 1 ) : Json::Value();
  return out;
}

// ---------------------------------------------------------------------------
// SolutionRegistry
// ---------------------------------------------------------------------------

SolutionRegistry &SolutionRegistry::instance()
{
  static SolutionRegistry registry;
  return registry;
}

void SolutionRegistry::setDirectory( const QString &dir )
{
  QMutexLocker lock( &mMutex );
  mDirectory = dir;
  mLoaded = false;
}

QString SolutionRegistry::directory() const
{
  QMutexLocker lock( &mMutex );
  return mDirectory;
}

void SolutionRegistry::ensureLoadedLocked() const
{
  if ( mLoaded )
    return;
  mLoaded = true;
  mSolutions.clear();
  mAliases.clear();
  mLoadProblems.clear();

  const QString base = mDirectory.isEmpty() ? defaultSolutionsDir() : mDirectory;
  QMap<QString, Json::Value> raw;
  const QFileInfoList entries =
    QDir( base ).entryInfoList( QStringList() << QStringLiteral( "*.json" ), QDir::Files );
  for ( const QFileInfo &entry : entries )
  {
    Json::Value doc;
    QString parseError;
    if ( !parseJsonFile( entry.absoluteFilePath(), &doc, &parseError ) )
    {
      mLoadProblems << parseError;
      continue;
    }
    const auto problems = validateSolutionTemplate( doc );
    if ( !doc.isObject() || !doc.isMember( "id" ) || !problems.empty() )
    {
      mLoadProblems << QString::fromStdString(
        entry.fileName().toStdString() + ": " +
        ( problems.empty() ? std::string( "missing id" ) : problems.front() ) );
      continue;
    }
    raw.insert( QString::fromStdString( doc["id"].asString() ), doc );
  }

  // Resolve inheritance for every document, then collect aliases.
  for ( auto it = raw.constBegin(); it != raw.constEnd(); ++it )
  {
    const Json::Value resolved = resolveSolutionInheritance( it.value(), raw, &mLoadProblems );
    if ( resolved.isNull() )
      continue;
    mSolutions.insert( it.key(), resolved );
    if ( resolved.isMember( "aliases" ) && resolved["aliases"].isArray() )
    {
      for ( const auto &alias : resolved["aliases"] )
      {
        const QString aliasId = QString::fromStdString( alias.asString() );
        if ( aliasId.isEmpty() || mSolutions.contains( aliasId ) || mAliases.contains( aliasId ) )
        {
          mLoadProblems << QStringLiteral( "%1: alias '%2' collides" ).arg( it.key(), aliasId );
          continue;
        }
        mAliases.insert( aliasId, it.key() );
      }
    }
  }

  if ( mSolutions.isEmpty() )
    loadEmbeddedDefaults();
}

void SolutionRegistry::loadEmbeddedDefaults() const
{
  // Headless safety net: one minimal water/NDWI optical solution so search and
  // instantiation surfaces never face an empty catalog.
  Json::Value doc( Json::objectValue );
  doc["schema_version"] = "1.0";
  doc["kind"] = "solution_template";
  doc["id"] = "solution.water.optical-ndwi";
  doc["version"] = 1;
  doc["title"] = "Optical surface water mapping (NDWI)";
  doc["description"] = "Embedded fallback: NDWI water index with threshold, "
                       "water-extent style and the water/flood map template.";
  Json::Value tasks( Json::arrayValue );
  tasks.append( "water" );
  tasks.append( "flood" );
  doc["tasks"] = tasks;
  Json::Value modalities( Json::arrayValue );
  modalities.append( "optical" );
  doc["modalities"] = modalities;
  Json::Value sensors( Json::arrayValue );
  sensors.append( "sentinel-2" );
  sensors.append( "landsat" );
  doc["sensors"] = sensors;
  Json::Value contracts( Json::arrayValue );
  Json::Value primary( Json::objectValue );
  primary["name"] = "primary";
  primary["kind"] = "raster";
  primary["required"] = true;
  primary["description"] = "Optical reflectance product with Green and NIR bands.";
  contracts.append( primary );
  doc["input_contracts"] = contracts;
  doc["analysis_recipe"] = "harness.optical_water";
  doc["map_template"] = "water-flood-a4l";
  Json::Value styles( Json::objectValue );
  styles["primary"] = "style.water-extent";
  doc["style_spec"] = styles;
  Json::Value artifacts( Json::arrayValue );
  Json::Value extent( Json::objectValue );
  extent["name"] = "water_extent";
  extent["kind"] = "raster";
  artifacts.append( extent );
  doc["expected_artifacts"] = artifacts;
  doc["quality_grade"] = "experimental";
  mSolutions.insert( QString::fromStdString( doc["id"].asString() ), doc );
}

Json::Value SolutionRegistry::solutions() const
{
  QMutexLocker lock( &mMutex );
  ensureLoadedLocked();
  Json::Value list( Json::arrayValue );
  for ( auto it = mSolutions.constBegin(); it != mSolutions.constEnd(); ++it )
    list.append( it.value() );
  return list;
}

Json::Value SolutionRegistry::find( const QString &id ) const
{
  QMutexLocker lock( &mMutex );
  ensureLoadedLocked();
  Json::Value doc = mSolutions.value( id, Json::Value() );
  if ( doc.isNull() )
  {
    const QString canonical = mAliases.value( id, QString() );
    if ( !canonical.isEmpty() )
      doc = mSolutions.value( canonical, Json::Value() );
  }
  return doc;
}

bool SolutionRegistry::knowsId( const std::string &id ) const
{
  QMutexLocker lock( &mMutex );
  ensureLoadedLocked();
  const QString key = QString::fromStdString( id );
  return mSolutions.contains( key ) || mAliases.contains( key );
}

bool SolutionRegistry::registerSolution( Json::Value doc, QString *error )
{
  const auto problems = validateSolutionTemplate( doc );
  if ( !problems.empty() )
  {
    if ( error )
      *error = QString::fromStdString( problems.front() );
    return false;
  }
  QMutexLocker lock( &mMutex );
  ensureLoadedLocked();
  mSolutions.insert( QString::fromStdString( doc["id"].asString() ), std::move( doc ) );
  return true;
}

QStringList SolutionRegistry::loadProblems() const
{
  QMutexLocker lock( &mMutex );
  ensureLoadedLocked();
  return mLoadProblems;
}

void SolutionRegistry::reload()
{
  QMutexLocker lock( &mMutex );
  mLoaded = false;
}

} // namespace sicnu::agent::cartography
