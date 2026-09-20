// lab_spec_loader.cpp — LabSpec parsing and structural validation
#include "lab_spec_loader.h"

#include "processing/framework/runtime_paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <cmath>
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
    // jsoncpp reports "line N, column M" inside its message; surface it
    // verbatim and extract the line for the structured field, best effort.
    const QRegularExpression linePattern( QStringLiteral( "[Ll]ine\\s+(\\d+)" ) );
    const auto match = linePattern.match( stdToQString( parseErrors ) );
    return fail( QStringLiteral( "invalid JSON: %1" ).arg( stdToQString( parseErrors ) ),
                 match.hasMatch() ? match.captured( 1 ).toInt() : 0 );
  }

  if ( !root.isObject() )
    return fail( QStringLiteral( "root must be an object" ) );

  // --- unknown top-level keys (schema: additionalProperties: false) --------
  // Version-strict: spec_version 2 fields in a v1 document are a semantic
  // drift the author must opt into explicitly, so they are rejected there.
  static const QStringList v2RootKeys = {
    QStringLiteral( "objective_zh" ), QStringLiteral( "glossary" ),
    QStringLiteral( "expected_artifacts" ), QStringLiteral( "param_ranges" ),
    QStringLiteral( "grading_rules" ), QStringLiteral( "principles" ),
    QStringLiteral( "prerequisite_knowledge" )
  };
  QStringList allowedRootKeys = {
    QStringLiteral( "spec_version" ), QStringLiteral( "id" ), QStringLiteral( "title" ),
    QStringLiteral( "title_zh" ), QStringLiteral( "objective" ), QStringLiteral( "prerequisites" ),
    QStringLiteral( "steps" ), QStringLiteral( "grading_ref" ), QStringLiteral( "thinking_questions" )
  };
  allowedRootKeys += v2RootKeys;
  for ( const auto &key : root.getMemberNames() )
  {
    const QString name = stdToQString( key );
    if ( !allowedRootKeys.contains( name ) )
      return fail( QStringLiteral( "unknown top-level key '%1'" ).arg( name ) );
  }

  // --- required scalars ----------------------------------------------------
  // draft-07 "type: integer" also accepts integral floats like 1.0; mirror it.
  if ( !root.isMember( "spec_version" ) )
    return fail( QStringLiteral( "spec_version must be an integer" ) );
  const Json::Value &specVersion = root[ "spec_version" ];
  const bool integral = specVersion.isInt()
    || ( specVersion.isDouble() && specVersion.asDouble() == std::floor( specVersion.asDouble() ) );
  if ( !integral )
    return fail( QStringLiteral( "spec_version must be an integer" ) );
  // LabSpec 2 is a strict superset of v1 (lab platform 12.0): the loader
  // accepts both, v2-only fields are validated below when the version is 2.
  const int contractVersion = specVersion.asInt();
  if ( contractVersion != 1 && contractVersion != 2 )
    return fail( QStringLiteral( "unsupported spec_version %1 (expected 1 or 2)" )
                   .arg( contractVersion ) );
  if ( contractVersion == 1 )
  {
    for ( const auto &key : v2RootKeys )
    {
      if ( root.isMember( key.toStdString() ) )
        return fail( QStringLiteral( "top-level key '%1' requires spec_version 2" ).arg( key ) );
    }
  }

  static const QRegularExpression idPattern( QStringLiteral( "^lab[0-9]{2}_[a-z][a-z0-9_]*$" ) );
  if ( !isNonEmptyString( root[ "id" ] ) )
    return fail( QStringLiteral( "id must be a non-empty string" ) );
  const QString labId = stdToQString( root[ "id" ].asString() );
  if ( !idPattern.match( labId ).hasMatch() )
    return fail( QStringLiteral( "id '%1' does not match ^lab[0-9]{2}_[a-z][a-z0-9_]*$" ).arg( labId ), 0, labId );

  // File name must carry the canonical id: data/labs/<id>.lab.json.
  // baseName() strips every extension, so "<id>.lab.json" yields exactly <id>
  // (ids cannot contain dots by the pattern above).
  const QString stem = QFileInfo( path ).baseName();
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

  // --- LabSpec 2 fields (validated here; authoring/contract data) ----------
  // The runtime LabSpec struct intentionally stays at the v1 shape: the v2
  // fields feed the docs generator, grading tooling and drift tests, which
  // read the JSON directly. Validation-only keeps every consumer source- and
  // ABI-compatible.
  if ( contractVersion == 2 )
  {
    if ( root.isMember( "objective_zh" )
         && !isNonEmptyString( root[ "objective_zh" ] ) )
      return fail( QStringLiteral( "objective_zh must be a non-empty string" ), 0, labId );

    // prerequisite_knowledge[]: prior labs/concepts (strings), as opposed to
    // prerequisites[] which stays a data-ref list.
    if ( root.isMember( "prerequisite_knowledge" ) )
    {
      const Json::Value &knowledge = root[ "prerequisite_knowledge" ];
      if ( !knowledge.isArray() )
        return fail( QStringLiteral( "prerequisite_knowledge must be an array" ), 0, labId );
      for ( const auto &item : knowledge )
      {
        if ( !isNonEmptyString( item ) )
          return fail( QStringLiteral( "prerequisite_knowledge entries must be non-empty strings" ), 0, labId );
      }
    }

    // glossary[]: {term, term_zh, definition_zh}
    if ( root.isMember( "glossary" ) )
    {
      const Json::Value &glossary = root[ "glossary" ];
      if ( !glossary.isArray() )
        return fail( QStringLiteral( "glossary must be an array" ), 0, labId );
      static const QStringList glossaryKeys = {
        QStringLiteral( "term" ), QStringLiteral( "term_zh" ), QStringLiteral( "definition_zh" )
      };
      for ( const auto &entry : glossary )
      {
        if ( !entry.isObject() )
          return fail( QStringLiteral( "glossary entries must be objects" ), 0, labId );
        for ( const auto &key : entry.getMemberNames() )
        {
          if ( !glossaryKeys.contains( stdToQString( key ) ) )
            return fail( QStringLiteral( "unknown glossary key '%1'" ).arg( stdToQString( key ) ), 0, labId );
        }
        for ( const char *key : { "term", "term_zh", "definition_zh" } )
        {
          if ( !isNonEmptyString( entry[ key ] ) )
            return fail( QStringLiteral( "glossary %1 must be a non-empty string" ).arg( key ), 0, labId );
        }
      }
    }

    // principles[]: {heading, body, formulas?}
    if ( root.isMember( "principles" ) )
    {
      const Json::Value &principles = root[ "principles" ];
      if ( !principles.isArray() )
        return fail( QStringLiteral( "principles must be an array" ), 0, labId );
      static const QStringList principleKeys = {
        QStringLiteral( "heading" ), QStringLiteral( "body" ), QStringLiteral( "formulas" )
      };
      for ( const auto &entry : principles )
      {
        if ( !entry.isObject() )
          return fail( QStringLiteral( "principles entries must be objects" ), 0, labId );
        for ( const auto &key : entry.getMemberNames() )
        {
          if ( !principleKeys.contains( stdToQString( key ) ) )
            return fail( QStringLiteral( "unknown principles key '%1'" ).arg( stdToQString( key ) ), 0, labId );
        }
        for ( const char *key : { "heading", "body" } )
        {
          if ( !isNonEmptyString( entry[ key ] ) )
            return fail( QStringLiteral( "principles %1 must be a non-empty string" ).arg( key ), 0, labId );
        }
        if ( entry.isMember( "formulas" )
             && ( !entry[ "formulas" ].isArray() ) )
          return fail( QStringLiteral( "principles formulas must be an array" ), 0, labId );
        if ( entry.isMember( "formulas" ) )
        {
          for ( const auto &formula : entry[ "formulas" ] )
          {
            if ( !isNonEmptyString( formula ) )
              return fail( QStringLiteral( "principles formulas entries must be non-empty strings" ), 0, labId );
          }
        }
      }
    }

    // expected_artifacts[]: {path, kind?, note_zh?}
    if ( root.isMember( "expected_artifacts" ) )
    {
      const Json::Value &artifacts = root[ "expected_artifacts" ];
      if ( !artifacts.isArray() )
        return fail( QStringLiteral( "expected_artifacts must be an array" ), 0, labId );
      static const QStringList artifactKeys = {
        QStringLiteral( "path" ), QStringLiteral( "kind" ), QStringLiteral( "note_zh" )
      };
      for ( const auto &entry : artifacts )
      {
        if ( !entry.isObject() )
          return fail( QStringLiteral( "expected_artifacts entries must be objects" ), 0, labId );
        for ( const auto &key : entry.getMemberNames() )
        {
          if ( !artifactKeys.contains( stdToQString( key ) ) )
            return fail( QStringLiteral( "unknown expected_artifacts key '%1'" ).arg( stdToQString( key ) ), 0, labId );
        }
        if ( !isNonEmptyString( entry[ "path" ] ) )
          return fail( QStringLiteral( "expected_artifacts path must be a non-empty string" ), 0, labId );
        if ( entry.isMember( "kind" ) )
        {
          // Guard the scalar read: asString() on an array/object raises
          // Json::LogicError, which would crash instead of failing typed.
          if ( !entry[ "kind" ].isString() )
            return fail( QStringLiteral( "expected_artifacts kind must be raster|vector|file" ), 0, labId );
          const QString kind = stdToQString( entry[ "kind" ].asString() );
          if ( kind != QLatin1String( "raster" ) && kind != QLatin1String( "vector" )
               && kind != QLatin1String( "file" ) )
            return fail( QStringLiteral( "expected_artifacts kind must be raster|vector|file" ), 0, labId );
        }
        if ( entry.isMember( "note_zh" ) && !isNonEmptyString( entry[ "note_zh" ] ) )
          return fail( QStringLiteral( "expected_artifacts note_zh must be a non-empty string" ), 0, labId );
      }
    }

    // param_ranges: operator_id -> param -> {min?, max?, values?, note_zh?}
    if ( root.isMember( "param_ranges" ) )
    {
      const Json::Value &ranges = root[ "param_ranges" ];
      if ( !ranges.isObject() )
        return fail( QStringLiteral( "param_ranges must be an object" ), 0, labId );
      static const QRegularExpression operatorIdPattern(
        QStringLiteral( "^(rs|opencv):[a-z0-9_]+$" ) );
      static const QStringList rangeKeys = {
        QStringLiteral( "min" ), QStringLiteral( "max" ), QStringLiteral( "values" ),
        QStringLiteral( "note_zh" )
      };
      for ( const auto &operatorKey : ranges.getMemberNames() )
      {
        if ( !operatorIdPattern.match( stdToQString( operatorKey ) ).hasMatch() )
          return fail( QStringLiteral( "param_ranges key '%1' is not an operator id" )
                         .arg( stdToQString( operatorKey ) ), 0, labId );
        const Json::Value &operatorRanges = ranges[ operatorKey ];
        if ( !operatorRanges.isObject() )
          return fail( QStringLiteral( "param_ranges[%1] must be an object" ).arg( operatorKey ), 0, labId );
        for ( const auto &paramKey : operatorRanges.getMemberNames() )
        {
          const Json::Value &range = operatorRanges[ paramKey ];
          if ( !range.isObject() )
            return fail( QStringLiteral( "param_ranges[%1][%2] must be an object" )
                           .arg( operatorKey, paramKey ), 0, labId );
          for ( const auto &key : range.getMemberNames() )
          {
            if ( !rangeKeys.contains( stdToQString( key ) ) )
              return fail( QStringLiteral( "unknown param_ranges key '%1'" ).arg( stdToQString( key ) ), 0, labId );
          }
          const bool hasMin = range.isMember( "min" );
          const bool hasMax = range.isMember( "max" );
          const bool hasValues = range.isMember( "values" );
          if ( !hasMin && !hasMax && !hasValues )
            return fail( QStringLiteral( "param_ranges[%1][%2] needs min, max or values" )
                           .arg( operatorKey, paramKey ), 0, labId );
          if ( hasMin && !range[ "min" ].isNumeric() )
            return fail( QStringLiteral( "param_ranges[%1][%2].min must be a number" )
                           .arg( operatorKey, paramKey ), 0, labId );
          if ( hasMax && !range[ "max" ].isNumeric() )
            return fail( QStringLiteral( "param_ranges[%1][%2].max must be a number" )
                           .arg( operatorKey, paramKey ), 0, labId );
          if ( hasMin && hasMax && range[ "min" ].asDouble() > range[ "max" ].asDouble() )
            return fail( QStringLiteral( "param_ranges[%1][%2]: min exceeds max" )
                           .arg( operatorKey, paramKey ), 0, labId );
          if ( hasValues
               && ( !range[ "values" ].isArray() || range[ "values" ].empty() ) )
            return fail( QStringLiteral( "param_ranges[%1][%2].values must be a non-empty array" )
                           .arg( operatorKey, paramKey ), 0, labId );
          if ( range.isMember( "note_zh" ) && !isNonEmptyString( range[ "note_zh" ] ) )
            return fail( QStringLiteral( "param_ranges[%1][%2].note_zh must be a non-empty string" )
                           .arg( operatorKey, paramKey ), 0, labId );
        }
      }
    }

    // grading_rules: explicit pointer at the auto-grader rules file.
    if ( root.isMember( "grading_rules" )
         && !isNonEmptyString( root[ "grading_rules" ] ) )
      return fail( QStringLiteral( "grading_rules must be a non-empty string" ), 0, labId );
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
    // params is optional (schema) but must be an object when present;
    // an operator step without params behaves as if it were empty.
    if ( stepValue.isMember( "params" ) && !stepValue[ "params" ].isObject() )
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

  // Deterministic order: file-name sorted *.lab.json only. Because ids must
  // equal their file stem, two files in one directory can never collide on id
  // — duplicates are structurally impossible, no cross-file check needed.
  QStringList files = dirInfo.entryList( QStringList{ QStringLiteral( "*.lab.json" ) }, QDir::Files, QDir::Name );

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
