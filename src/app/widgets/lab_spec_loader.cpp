// lab_spec_loader.cpp — LabSpec parsing and structural validation
#include "lab_spec_loader.h"

#include "processing/framework/runtime_paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <memory>
#include <algorithm>

namespace lab {

using sicnu::processing::resolveRuntimeDataPath;

namespace {

bool isNonEmptyString( const Json::Value &value )
{
  return value.isString() && !value.asString().empty();
}

QString stdToQString( const std::string &s )
{
  return QString::fromStdString( s );
}

/// Prefix of `path` with `prefix`; -1 when not matched.
int prefixedBy( const QString &path, const QString &prefix )
{
  return path.startsWith( prefix ) ? static_cast<int>( prefix.size() ) : -1;
}

} // namespace

QString LabSpecError::toString() const
{
  QString where = path;
  if ( line > 0 )
    where += QStringLiteral( ":%1" ).arg( line );
  QString who = labId.isEmpty() ? QString() : QStringLiteral( " [%1]" ).arg( labId );
  return where + who + QStringLiteral( ": " ) + reason;
}

LabSpec loadLabSpecFile( const QString &path, LabSpecError *error )
{
  auto fail = [error, &path]( const QString &reason, int line = 0, const QString &labId = QString() )
  {
    if ( error )
      *error = LabSpecError{ path, labId, reason, line };
    return LabSpec{};
  };

  QFile file( path );
  if ( !file.open( QIODevice::ReadOnly ) )
    return fail( QStringLiteral( "cannot open file: %1" ).arg( file.errorString() ) );

  const QByteArray raw = file.readAll();

  Json::CharReaderBuilder builder;
  builder[ "collectComments" ] = false;
  std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
  Json::Value root;
  std::string parseErrors;
  if ( !reader->parse( raw.constData(), raw.constData() + raw.size(), &root, &parseErrors ) )
  {
    // jsoncpp reports "line N, column M" inside its message; surface it verbatim.
    return fail( QStringLiteral( "invalid JSON: %1" ).arg( stdToQString( parseErrors ) ) );
  }

  if ( !root.isObject() )
    return fail( QStringLiteral( "root must be an object" ) );

  // --- unknown top-level keys (schema: additionalProperties: false) --------
  static const QStringList allowedRootKeys = {
    QStringLiteral( "spec_version" ), QStringLiteral( "id" ), QStringLiteral( "title" ),
    QStringLiteral( "title_zh" ), QStringLiteral( "objective" ), QStringLiteral( "prerequisites" ),
    QStringLiteral( "steps" ), QStringLiteral( "grading_ref" ), QStringLiteral( "thinking_questions" )
  };
  for ( const auto &key : root.getMemberNames() )
  {
    if ( !allowedRootKeys.contains( stdToQString( key ) ) )
      return fail( QStringLiteral( "unknown top-level key '%1'" ).arg( stdToQString( key ) ) );
  }

  // --- required scalars ----------------------------------------------------
  if ( !root.isMember( "spec_version" ) || !root[ "spec_version" ].isInt() )
    return fail( QStringLiteral( "spec_version must be an integer" ) );
  if ( root[ "spec_version" ].asInt() != 1 )
    return fail( QStringLiteral( "unsupported spec_version %1 (expected 1)" ).arg( root[ "spec_version" ].asInt() ) );

  static const QRegularExpression idPattern( QStringLiteral( "^lab[0-9]{2}_[a-z][a-z0-9_]*$" ) );
  if ( !isNonEmptyString( root[ "id" ] ) )
    return fail( QStringLiteral( "id must be a non-empty string" ) );
  const QString labId = stdToQString( root[ "id" ].asString() );
  if ( !idPattern.match( labId ).hasMatch() )
    return fail( QStringLiteral( "id '%1' does not match ^lab[0-9]{2}_[a-z][a-z0-9_]*$" ).arg( labId ), 0, labId );

  // File name must carry the canonical id: data/labs/<id>.lab.json.
  const QString stem = QFileInfo( path ).completeBaseName(); // strips only ".json"
  if ( stem != labId )
    return fail( QStringLiteral( "file stem '%1' does not match id '%2'" ).arg( stem, labId ), 0, labId );

  for ( const char *key : { "title", "title_zh", "objective" } )
  {
    if ( !isNonEmptyString( root[ key ] ) )
      return fail( QStringLiteral( "%1 must be a non-empty string" ).arg( key ), 0, labId );
  }

  LabSpec spec;
  spec.id = labId;
  spec.title = stdToQString( root[ "title" ].asString() );
  spec.titleZh = stdToQString( root[ "title_zh" ].asString() );
  spec.objective = stdToQString( root[ "objective" ].asString() );

  // --- prerequisites -------------------------------------------------------
  static const QStringList allowedDataRefKeys = { QStringLiteral( "path" ), QStringLiteral( "note" ) };
  if ( root.isMember( "prerequisites" ) )
  {
    const Json::Value &prereqs = root[ "prerequisites" ];
    if ( !prereqs.isArray() )
      return fail( QStringLiteral( "prerequisites must be an array" ), 0, labId );
    for ( const auto &item : prereqs )
    {
      if ( !item.isObject() )
        return fail( QStringLiteral( "prerequisites entries must be objects" ), 0, labId );
      for ( const auto &key : item.getMemberNames() )
      {
        if ( !allowedDataRefKeys.contains( stdToQString( key ) ) )
          return fail( QStringLiteral( "unknown prerequisite key '%1'" ).arg( stdToQString( key ) ), 0, labId );
      }
      if ( !isNonEmptyString( item[ "path" ] ) )
        return fail( QStringLiteral( "prerequisite path must be a non-empty string" ), 0, labId );
      LabDataRef ref;
      ref.path = stdToQString( item[ "path" ].asString() );
      if ( isNonEmptyString( item[ "note" ] ) )
        ref.note = stdToQString( item[ "note" ].asString() );
      spec.prerequisites.append( ref );
    }
  }

  // --- grading_ref ---------------------------------------------------------
  if ( root.isMember( "grading_ref" ) )
  {
    const Json::Value &grading = root[ "grading_ref" ];
    if ( !grading.isObject() )
      return fail( QStringLiteral( "grading_ref must be an object" ), 0, labId );
    for ( const auto &key : grading.getMemberNames() )
    {
      if ( stdToQString( key ) != QStringLiteral( "pipeline" ) )
        return fail( QStringLiteral( "unknown grading_ref key '%1'" ).arg( stdToQString( key ) ), 0, labId );
    }
    if ( !isNonEmptyString( grading[ "pipeline" ] ) )
      return fail( QStringLiteral( "grading_ref.pipeline must be a non-empty string" ), 0, labId );
    spec.gradingPipeline = stdToQString( grading[ "pipeline" ].asString() );
  }

  // --- thinking_questions --------------------------------------------------
  if ( root.isMember( "thinking_questions" ) )
  {
    const Json::Value &questions = root[ "thinking_questions" ];
    if ( !questions.isArray() )
      return fail( QStringLiteral( "thinking_questions must be an array" ), 0, labId );
    for ( const auto &q : questions )
    {
      if ( !isNonEmptyString( q ) )
        return fail( QStringLiteral( "thinking_questions entries must be non-empty strings" ), 0, labId );
      spec.thinkingQuestions << stdToQString( q.asString() );
    }
  }

  // --- steps ---------------------------------------------------------------
  if ( !root.isMember( "steps" ) || !root[ "steps" ].isArray() || root[ "steps" ].empty() )
    return fail( QStringLiteral( "steps must be a non-empty array" ), 0, labId );

  static const QStringList allowedStepKeys = {
    QStringLiteral( "title" ), QStringLiteral( "title_zh" ), QStringLiteral( "description_zh" ),
    QStringLiteral( "operator_id" ), QStringLiteral( "params" ), QStringLiteral( "action" ),
    QStringLiteral( "teaching_note" ), QStringLiteral( "completion_hint" )
  };
  static const QRegularExpression operatorPattern( QStringLiteral( "^(rs|opencv):[a-z0-9_]+$" ) );
  static const QRegularExpression actionPattern( QStringLiteral( "^[A-Za-z][A-Za-z0-9_]*$" ) );

  int stepIndex = 0;
  for ( const auto &stepValue : root[ "steps" ] )
  {
    stepIndex++;
    const QString where = QStringLiteral( "step %1" ).arg( stepIndex );
    if ( !stepValue.isObject() )
      return fail( QStringLiteral( "%1 must be an object" ).arg( where ), 0, labId );

    for ( const auto &key : stepValue.getMemberNames() )
    {
      if ( !allowedStepKeys.contains( stdToQString( key ) ) )
        return fail( QStringLiteral( "%1: unknown key '%2'" ).arg( where, stdToQString( key ) ), 0, labId );
    }

    for ( const char *key : { "title", "title_zh", "description_zh" } )
    {
      if ( !isNonEmptyString( stepValue[ key ] ) )
        return fail( QStringLiteral( "%1: %2 must be a non-empty string" ).arg( where, key ), 0, labId );
    }

    const bool hasOperator = stepValue.isMember( "operator_id" );
    const bool hasAction = stepValue.isMember( "action" );
    if ( hasOperator && hasAction )
      return fail( QStringLiteral( "%1: operator_id and action are mutually exclusive" ).arg( where ), 0, labId );
    if ( stepValue.isMember( "params" ) && !hasOperator )
      return fail( QStringLiteral( "%1: params requires operator_id" ).arg( where ), 0, labId );
    if ( hasOperator && !stepValue[ "params" ].isObject() )
      return fail( QStringLiteral( "%1: params must be an object" ).arg( where ), 0, labId );
    if ( hasOperator )
    {
      const QString operatorId = stdToQString( stepValue[ "operator_id" ].asString() );
      if ( !operatorPattern.match( operatorId ).hasMatch() )
        return fail( QStringLiteral( "%1: operator_id '%2' does not match ^(rs|opencv):[a-z0-9_]+$" ).arg( where, operatorId ), 0, labId );
    }
    if ( hasAction && !actionPattern.match( stdToQString( stepValue[ "action" ].asString() ) ).hasMatch() )
      return fail( QStringLiteral( "%1: action is not a valid slot name" ).arg( where ), 0, labId );

    LabStep step;
    step.title = stdToQString( stepValue[ "title" ].asString() );
    step.titleZh = stdToQString( stepValue[ "title_zh" ].asString() );
    step.descriptionZh = stdToQString( stepValue[ "description_zh" ].asString() );
    if ( hasOperator )
    {
      step.operatorId = stdToQString( stepValue[ "operator_id" ].asString() );
      step.params = stepValue[ "params" ];
    }
    if ( hasAction )
      step.action = stdToQString( stepValue[ "action" ].asString() );
    if ( isNonEmptyString( stepValue[ "teaching_note" ] ) )
      step.teachingNote = stdToQString( stepValue[ "teaching_note" ].asString() );
    if ( isNonEmptyString( stepValue[ "completion_hint" ] ) )
      step.completionHint = stdToQString( stepValue[ "completion_hint" ].asString() );
    spec.steps.append( step );
  }

  if ( error )
    *error = LabSpecError{ path, labId, QString(), 0 };
  return spec;
}

LabLoadResult loadLabSpecsFromDir( const QString &dir )
{
  LabLoadResult result;

  QDir dirInfo( dir );
  if ( !dirInfo.exists() )
  {
    result.errors.append( LabSpecError{ dir, QString(), QStringLiteral( "lab directory does not exist" ), 0 } );
    return result;
  }

  // Deterministic order: file-name sorted *.lab.json only.
  QStringList files = dirInfo.entryList( QStringList{ QStringLiteral( "*.lab.json" ) }, QDir::Files, QDir::Name );

  QSet<QString> seenIds;
  for ( const QString &fileName : files )
  {
    const QString fullPath = dirInfo.filePath( fileName );
    LabSpecError error;
    LabSpec spec = loadLabSpecFile( fullPath, &error );
    if ( !error.reason.isEmpty() )
    {
      result.errors.append( error );
      continue;
    }
    if ( seenIds.contains( spec.id ) )
    {
      result.errors.append( LabSpecError{ fullPath, spec.id, QStringLiteral( "duplicate lab id" ), 0 } );
      continue;
    }
    seenIds.insert( spec.id );
    result.labs.append( spec );
  }

  // Keep the successful labs in file-name order (QList append preserves it).
  return result;
}

QString defaultLabDirectory()
{
  return resolveRuntimeDataPath( QStringLiteral( "data/labs" ) );
}

Json::Value resolveLabParamPaths( const Json::Value &params,
                                  const QString &labId,
                                  const std::function<QString( const QString &, PathRole )> &resolve )
{
  if ( !resolve || params.isNull() )
    return params;

  std::function<Json::Value( const Json::Value & )> walk = [&]( const Json::Value &node ) -> Json::Value
  {
    if ( node.isString() )
    {
      const QString value = stdToQString( node.asString() );
      int cut = prefixedBy( value, QStringLiteral( "data/" ) );
      if ( cut > 0 )
        return Json::Value( resolve( value, PathRole::Input ).toStdString() );
      cut = prefixedBy( value, QStringLiteral( "outputs/" ) );
      if ( cut > 0 )
      {
        const QString relative = value.mid( cut );
        return Json::Value( resolve( QStringLiteral( "output/labs/%1/%2" ).arg( labId, relative ),
                                     PathRole::Output )
                              .toStdString() );
      }
      return node;
    }
    if ( node.isArray() )
    {
      Json::Value out( Json::arrayValue );
      for ( const auto &item : node )
        out.append( walk( item ) );
      return out;
    }
    if ( node.isObject() )
    {
      Json::Value out( Json::objectValue );
      for ( const auto &key : node.getMemberNames() )
        out[ key ] = walk( node[ key ] );
      return out;
    }
    return node;
  };

  return walk( params );
}

QStringList errorStrings( const LabLoadResult &result )
{
  QStringList strings;
  strings.reserve( result.errors.size() );
  for ( const auto &error : result.errors )
    strings << error.toString();
  return strings;
}

} // namespace lab
