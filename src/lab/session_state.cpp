/***************************************************************************
  lab/session_state.cpp — LabSession transitions and canonical JSON.
***************************************************************************/

#include "session_state.h"

#include <utility>

namespace sicnu::lab
{
namespace
{

bool isSafeSessionName( const std::string &s )
{
  // Filesystem-safe identity component: no separators, no traversal, bounded.
  if ( s.empty() || s.size() > 64 || s == "." || s == ".." )
    return false;
  for ( const char ch : s )
  {
    const bool ok = ( ch >= 'a' && ch <= 'z' ) || ( ch >= 'A' && ch <= 'Z' ) ||
                    ( ch >= '0' && ch <= '9' ) || ch == '_' || ch == '-' || ch == '.';
    if ( !ok )
      return false;
  }
  return true;
}

bool isPlanSourceKnown( const std::string &s )
{
  return s == "authored_v3" || s == "derived_from_v2" || s == "derived_from_v1";
}

bool toolAllowedByStage( const Stage &stage, const std::string &operatorId )
{
  if ( stage.allowedTools.empty() )
    return true;
  for ( const std::string &tool : stage.allowedTools )
  {
    if ( tool == operatorId )
      return true;
    if ( !tool.empty() && tool.back() == '*' &&
         operatorId.rfind( tool.substr( 0, tool.size() - 1 ), 0 ) == 0 )
      return true;
  }
  return false;
}

std::string hintTarget( const HintEvent &event )
{
  return event.target;
}

std::string stateToString( SessionState state )
{
  switch ( state )
  {
    case SessionState::Active:
      return "active";
    case SessionState::Completed:
      return "completed";
    case SessionState::Abandoned:
      return "abandoned";
  }
  return {};
}

std::string stageStatusToString( StageStatus status )
{
  switch ( status )
  {
    case StageStatus::Active:
      return "active";
    case StageStatus::Advanced:
      return "advanced";
    case StageStatus::BlockedAdvance:
      return "blocked_advance";
  }
  return {};
}

std::string verdictToString( Verdict verdict )
{
  switch ( verdict )
  {
    case Verdict::Pass:
      return "pass";
    case Verdict::Fail:
      return "fail";
    case Verdict::Unverifiable:
      return "unverifiable";
  }
  return {};
}

template <typename T>
bool parseEnum( const Json::Value &node, const std::vector<std::pair<std::string, T>> &vocabulary,
                T &out )
{
  if ( !node.isString() )
    return false;
  for ( const auto &entry : vocabulary )
  {
    if ( entry.first == node.asString() )
    {
      out = entry.second;
      return true;
    }
  }
  return false;
}

std::vector<LabDiag> validateEnvelope( const Json::Value &doc )
{
  std::vector<LabDiag> diags;
  auto add = [ &diags ]( std::string message )
  { diags.push_back( LabDiag{ "lab.session.schema", std::move( message ) } ); };

  if ( !doc.isObject() )
  {
    add( "session document must be an object" );
    return diags;
  }
  static const char *kKeys[] = { "schema", "session_id", "lab_id", "lab_spec_fingerprint",
                                 "lab_spec_version", "plan_source", "student_id", "seed", "state",
                                 "stages", "checkpoint_results", "question_answers", "hint_events",
                                 "tool_choices", "execution_refs", "last_seq" };
  for ( const auto &key : doc.getMemberNames() )
  {
    bool known = false;
    for ( const char *allowed : kKeys )
      known |= key == allowed;
    if ( !known )
      add( std::string( "unknown key '" ) + key + "'" );
  }

  // The strict-envelope promise ("refuses unknown keys ... never adopted")
  // holds at every nesting level: a hand-edited entry must not smuggle extra
  // fields through a load/save cycle. These vocabularies mirror what
  // sessionToJson emits, which is the schema of record.
  auto rejectNested = [ &add ]( const Json::Value &node, const char *array,
                                const char *const *allowed, std::size_t count )
  {
    if ( !node.isArray() )
      return;
    for ( Json::ArrayIndex i = 0; i < node.size(); ++i )
    {
      const Json::Value &entry = node[ i ];
      if ( !entry.isObject() )
        continue; // shape errors are reported by the typed parsers below
      for ( const auto &key : entry.getMemberNames() )
      {
        bool known = false;
        for ( std::size_t k = 0; k < count; ++k )
          known |= key == allowed[ k ];
        if ( !known )
        {
          add( std::string( array ) + "[" + std::to_string( i ) + "]: unknown key '" + key + "'" );
        }
      }
    }
  };

  static const char *kStageKeys[] = { "stage_id", "status" };
  rejectNested( doc[ "stages" ], "stages", kStageKeys, 2 );

  static const char *kResultKeys[] = { "checkpoint_id", "verdict", "attempt", "evidence", "seq" };
  rejectNested( doc[ "checkpoint_results" ], "checkpoint_results", kResultKeys, 5 );
  if ( doc[ "checkpoint_results" ].isArray() )
  {
    static const char *kEvidenceKeys[] = { "check_index", "ok", "observed", "expected" };
    for ( const Json::Value &result : doc[ "checkpoint_results" ] )
      rejectNested( result[ "evidence" ], "checkpoint_results.evidence", kEvidenceKeys, 4 );
  }

  static const char *kAnswerKeys[] = { "question_id", "answer", "numeric", "seq" };
  rejectNested( doc[ "question_answers" ], "question_answers", kAnswerKeys, 4 );

  static const char *kHintKeys[] = { "target", "level", "seq" };
  rejectNested( doc[ "hint_events" ], "hint_events", kHintKeys, 3 );

  static const char *kChoiceKeys[] = { "stage_id", "operator_id", "params_subset", "allowed", "seq" };
  rejectNested( doc[ "tool_choices" ], "tool_choices", kChoiceKeys, 5 );
  // params_subset itself stays open-keyed (names mirror the operator schema);
  // its value types are enforced by the typed parser below.

  static const char *kRefKeys[] = { "kind", "id", "seq" };
  rejectNested( doc[ "execution_refs" ], "execution_refs", kRefKeys, 3 );

  return diags;
}

} // namespace

long long nextSeq( const LabSession &session )
{
  return session.lastSeq + 1;
}

LabResult<LabSession> startSession( const LabRuntimePlan &plan, const SessionMeta &meta,
                                    long long seq )
{
  std::vector<LabDiag> diags;
  if ( !isSafeSessionName( meta.labId ) )
    diags.push_back( LabDiag{ "lab.session.field", "labId is not a safe identity component" } );
  if ( !isSafeSessionName( meta.studentId ) )
    diags.push_back( LabDiag{ "lab.session.field", "studentId is not a safe identity component" } );
  if ( !isPlanSourceKnown( meta.planSource ) )
    diags.push_back( LabDiag{ "lab.session.field", "unknown planSource '" + meta.planSource + "'" } );
  if ( seq < 1 )
    diags.push_back( LabDiag{ "lab.session.field", "session sequence must be >= 1" } );
  if ( plan.reproducibility.requireSeed && !meta.hasSeed )
    diags.push_back( LabDiag{ "lab.session.seed_required",
                              "this lab requires a recorded seed before starting" } );
  if ( !diags.empty() )
    return LabResult<LabSession>::failure( std::move( diags ) );

  LabSession session;
  session.sessionId = meta.labId + "/" + meta.studentId + "/" + std::to_string( seq );
  session.labId = meta.labId;
  session.labSpecVersion = meta.labSpecVersion;
  session.planSource = meta.planSource;
  session.studentId = meta.studentId;
  session.hasSeed = meta.hasSeed;
  session.seed = meta.seed;
  session.state = SessionState::Active;
  session.stages.reserve( plan.stages.size() );
  for ( const Stage &stage : plan.stages )
    session.stages.push_back( SessionStage{ stage.id, StageStatus::Active } );
  return LabResult<LabSession>::success( std::move( session ) );
}

LabResult<> completeSession( LabSession &session, const LabRuntimePlan &plan )
{
  if ( session.state != SessionState::Active )
    return LabResult<>::failure( { LabDiag{ "lab.session.bad_transition",
                                            "only an active session can be completed" } } );

  std::vector<LabDiag> unmet;
  for ( const Question &question : plan.questions )
  {
    bool answered = false;
    for ( const QuestionAnswer &answer : session.questionAnswers )
      answered |= answer.questionId == question.id;
    if ( !answered )
      unmet.push_back( LabDiag{ "lab.session.incomplete",
                                "question '" + question.id + "' is unanswered" } );
  }
  for ( const Stage &stage : plan.stages )
  {
    for ( const Checkpoint &checkpoint : stage.checkpoints )
    {
      if ( checkpoint.advance != AdvancePolicy::Gate )
        continue;
      // Latest-wins by seq; a gate with no result or a non-passing latest
      // result keeps the session open.
      const CheckpointResult *latest = nullptr;
      for ( const CheckpointResult &result : session.checkpointResults )
      {
        if ( result.checkpointId != checkpoint.id )
          continue;
        if ( !latest || result.seq > latest->seq )
          latest = &result;
      }
      if ( !latest )
        unmet.push_back( LabDiag{ "lab.session.incomplete",
                                  "gate checkpoint '" + checkpoint.id + "' has no verification yet" } );
      else if ( latest->verdict != Verdict::Pass )
        unmet.push_back( LabDiag{ "lab.session.incomplete",
                                  "gate checkpoint '" + checkpoint.id + "' has not passed" } );
    }
  }
  if ( !unmet.empty() )
    return LabResult<>::failure( std::move( unmet ) );

  session.state = SessionState::Completed;
  return LabResult<>::success();
}

LabResult<> abandonSession( LabSession &session )
{
  if ( session.state != SessionState::Active )
    return LabResult<>::failure( { LabDiag{ "lab.session.bad_transition",
                                            "only an active session can be abandoned" } } );
  session.state = SessionState::Abandoned;
  return LabResult<>::success();
}

LabResult<> reopenSession( LabSession &session )
{
  if ( session.state != SessionState::Abandoned )
    return LabResult<>::failure( { LabDiag{ "lab.session.bad_transition",
                                            "only an abandoned session can be reopened" } } );
  session.state = SessionState::Active;
  return LabResult<>::success();
}

LabResult<> recordToolUse( LabSession &session, const LabRuntimePlan &plan, ToolChoice choice )
{
  if ( session.state != SessionState::Active )
    return LabResult<>::failure( { LabDiag{ "lab.session.bad_transition",
                                            "cannot record tool use on a non-active session" } } );
  const Stage *stage = nullptr;
  for ( const Stage &candidate : plan.stages )
  {
    if ( candidate.id == choice.stageId )
    {
      stage = &candidate;
      break;
    }
  }
  if ( !stage )
    return LabResult<>::failure( { LabDiag{ "lab.session.unknown_stage",
                                            "unknown stage '" + choice.stageId + "'" } } );
  choice.allowed = toolAllowedByStage( *stage, choice.operatorId );
  // The ledger owns the sequence: a caller-forged seq (gaps, duplicates,
  // order inversions) would corrupt the latest-wins gate semantics, so every
  // recorder draws from nextSeq() — same discipline as recordAnswer and
  // verifyCheckpoint.
  choice.seq = nextSeq( session );
  session.lastSeq = choice.seq;
  session.toolChoices.push_back( std::move( choice ) );
  return LabResult<>::success();
}

LabResult<> recordAnswer( LabSession &session, const LabRuntimePlan &plan,
                          const std::string &questionId, const std::string &answerText,
                          std::optional<double> numeric )
{
  if ( session.state != SessionState::Active )
    return LabResult<>::failure( { LabDiag{ "lab.session.bad_transition",
                                            "cannot answer on a non-active session" } } );
  const Question *question = nullptr;
  for ( const Question &candidate : plan.questions )
  {
    if ( candidate.id == questionId )
    {
      question = &candidate;
      break;
    }
  }
  if ( !question )
    return LabResult<>::failure( { LabDiag{ "lab.session.answer_unknown_question",
                                            "unknown question '" + questionId + "'" } } );

  QuestionAnswer answer;
  answer.questionId = questionId;
  switch ( question->kind )
  {
    case QuestionKind::FreeText:
      if ( answerText.empty() )
        return LabResult<>::failure( { LabDiag{ "lab.session.field",
                                                "free-text answer must not be empty" } } );
      answer.answer = answerText;
      break;
    case QuestionKind::Numeric:
      if ( !numeric )
        return LabResult<>::failure( { LabDiag{ "lab.session.field",
                                                "numeric question '" + questionId +
                                                  "' requires a numeric answer" } } );
      answer.hasNumeric = true;
      answer.numeric = *numeric;
      break;
    case QuestionKind::Choice:
    {
      bool known = false;
      for ( const std::string &choice : question->choices )
        known |= choice == answerText;
      if ( !known )
        return LabResult<>::failure( { LabDiag{
          "lab.session.field", "answer to '" + questionId + "' must be one of the declared choices" } } );
      answer.answer = answerText;
      break;
    }
  }
  answer.seq = nextSeq( session );
  session.lastSeq = answer.seq;
  session.questionAnswers.push_back( std::move( answer ) );
  return LabResult<>::success();
}

LabResult<> recordExecutionRef( LabSession &session, const std::string &kind,
                                const std::string &id )
{
  if ( session.state != SessionState::Active )
    return LabResult<>::failure( { LabDiag{ "lab.session.bad_transition",
                                            "cannot record execution refs on a non-active session" } } );
  if ( kind.empty() || id.empty() )
    return LabResult<>::failure( { LabDiag{ "lab.session.field",
                                            "execution ref requires kind and id" } } );
  ExecutionRef ref;
  ref.kind = kind;
  ref.id = id;
  ref.seq = nextSeq( session );
  session.lastSeq = ref.seq;
  session.executionRefs.push_back( std::move( ref ) );
  return LabResult<>::success();
}

Json::Value sessionToJson( const LabSession &session )
{
  Json::Value doc( Json::objectValue );
  doc[ "schema" ] = session.schemaId;
  doc[ "session_id" ] = session.sessionId;
  doc[ "lab_id" ] = session.labId;
  doc[ "lab_spec_fingerprint" ] = session.labSpecFingerprint;
  doc[ "lab_spec_version" ] = session.labSpecVersion;
  doc[ "plan_source" ] = session.planSource;
  doc[ "student_id" ] = session.studentId;
  if ( session.hasSeed )
    doc[ "seed" ] = Json::Int64( session.seed );
  doc[ "state" ] = stateToString( session.state );
  doc[ "last_seq" ] = Json::Int64( session.lastSeq );

  Json::Value stages( Json::arrayValue );
  for ( const SessionStage &stage : session.stages )
  {
    Json::Value node( Json::objectValue );
    node[ "stage_id" ] = stage.stageId;
    node[ "status" ] = stageStatusToString( stage.status );
    stages.append( node );
  }
  doc[ "stages" ] = stages;

  Json::Value results( Json::arrayValue );
  for ( const CheckpointResult &result : session.checkpointResults )
  {
    Json::Value node( Json::objectValue );
    node[ "checkpoint_id" ] = result.checkpointId;
    node[ "verdict" ] = verdictToString( result.verdict );
    node[ "attempt" ] = result.attempt;
    Json::Value evidence( Json::arrayValue );
    for ( const CheckEvidence &item : result.evidence )
    {
      Json::Value entry( Json::objectValue );
      entry[ "check_index" ] = item.checkIndex;
      entry[ "ok" ] = item.ok;
      entry[ "observed" ] = item.observed;
      entry[ "expected" ] = item.expected;
      evidence.append( entry );
    }
    node[ "evidence" ] = evidence;
    node[ "seq" ] = Json::Int64( result.seq );
    results.append( node );
  }
  doc[ "checkpoint_results" ] = results;

  Json::Value answers( Json::arrayValue );
  for ( const QuestionAnswer &answer : session.questionAnswers )
  {
    Json::Value node( Json::objectValue );
    node[ "question_id" ] = answer.questionId;
    node[ "answer" ] = answer.answer;
    if ( answer.hasNumeric )
      node[ "numeric" ] = answer.numeric;
    node[ "seq" ] = Json::Int64( answer.seq );
    answers.append( node );
  }
  doc[ "question_answers" ] = answers;

  Json::Value hints( Json::arrayValue );
  for ( const HintEvent &event : session.hintEvents )
  {
    Json::Value node( Json::objectValue );
    node[ "target" ] = hintTarget( event );
    node[ "level" ] = event.level;
    node[ "seq" ] = Json::Int64( event.seq );
    hints.append( node );
  }
  doc[ "hint_events" ] = hints;

  Json::Value choices( Json::arrayValue );
  for ( const ToolChoice &choice : session.toolChoices )
  {
    Json::Value node( Json::objectValue );
    node[ "stage_id" ] = choice.stageId;
    node[ "operator_id" ] = choice.operatorId;
    Json::Value params( Json::objectValue );
    for ( const auto &param : choice.paramsSubset )
      params[ param.first ] = param.second;
    node[ "params_subset" ] = params;
    node[ "allowed" ] = choice.allowed;
    node[ "seq" ] = Json::Int64( choice.seq );
    choices.append( node );
  }
  doc[ "tool_choices" ] = choices;

  Json::Value refs( Json::arrayValue );
  for ( const ExecutionRef &ref : session.executionRefs )
  {
    Json::Value node( Json::objectValue );
    node[ "kind" ] = ref.kind;
    node[ "id" ] = ref.id;
    node[ "seq" ] = Json::Int64( ref.seq );
    refs.append( node );
  }
  doc[ "execution_refs" ] = refs;

  return doc;
}

std::string sessionToCanonicalBytes( const LabSession &session )
{
  // jsoncpp objects are key-sorted, so the compact form is canonical
  // (byte-stable round-trip, pinned by tests).
  Json::StreamWriterBuilder writer;
  writer[ "indentation" ] = "";
  writer[ "commentStyle" ] = "None";
  return Json::writeString( writer, sessionToJson( session ) ) + "\n";
}

LabResult<LabSession> sessionFromJson( const Json::Value &doc )
{
  std::vector<LabDiag> diags = validateEnvelope( doc );
  if ( !diags.empty() )
    return LabResult<LabSession>::failure( std::move( diags ) );

  if ( !doc[ "schema" ].isString() || doc[ "schema" ].asString() != kSessionSchemaId )
    return LabResult<LabSession>::failure( { LabDiag{
      "lab.session.version",
      "unsupported session schema '" +
        ( doc[ "schema" ].isString() ? doc[ "schema" ].asString() : std::string( "<non-string>" ) ) +
        "' (expected " + kSessionSchemaId + ")" } } );

  auto requireString = [ &diags ]( const Json::Value &node, const char *key ) -> std::string
  {
    if ( !node[ key ].isString() || node[ key ].asString().empty() )
    {
      diags.push_back( LabDiag{ "lab.session.schema", std::string( key ) + " must be a non-empty string" } );
      return {};
    }
    return node[ key ].asString();
  };

  LabSession session;
  session.schemaId = kSessionSchemaId;
  session.sessionId = requireString( doc, "session_id" );
  session.labId = requireString( doc, "lab_id" );
  session.labSpecFingerprint = doc[ "lab_spec_fingerprint" ].isString()
                                 ? doc[ "lab_spec_fingerprint" ].asString()
                                 : std::string{};
  if ( !doc[ "lab_spec_version" ].isInt() )
    diags.push_back( LabDiag{ "lab.session.schema", "lab_spec_version must be an integer" } );
  else
    session.labSpecVersion = doc[ "lab_spec_version" ].asInt();
  session.planSource = requireString( doc, "plan_source" );
  session.studentId = requireString( doc, "student_id" );
  if ( doc.isMember( "seed" ) )
  {
    if ( !doc[ "seed" ].isInt64() )
      diags.push_back( LabDiag{ "lab.session.schema", "seed must be an integer" } );
    else
    {
      session.hasSeed = true;
      session.seed = doc[ "seed" ].asInt64();
    }
  }
  if ( !parseEnum( doc[ "state" ],
                   { { "active", SessionState::Active },
                     { "completed", SessionState::Completed },
                     { "abandoned", SessionState::Abandoned } },
                   session.state ) )
    diags.push_back( LabDiag{ "lab.session.schema", "state must be active|completed|abandoned" } );
  if ( !doc[ "last_seq" ].isInt64() )
    diags.push_back( LabDiag{ "lab.session.schema", "last_seq must be an integer" } );
  else
    session.lastSeq = doc[ "last_seq" ].asInt64();

  if ( doc[ "stages" ].isArray() )
  {
    for ( const Json::Value &node : doc[ "stages" ] )
    {
      SessionStage stage;
      stage.stageId = node[ "stage_id" ].isString() ? node[ "stage_id" ].asString() : std::string{};
      if ( stage.stageId.empty() )
      {
        diags.push_back( LabDiag{ "lab.session.schema", "stage entry needs a stage_id" } );
        continue;
      }
      if ( !parseEnum( node[ "status" ],
                       { { "active", StageStatus::Active },
                         { "advanced", StageStatus::Advanced },
                         { "blocked_advance", StageStatus::BlockedAdvance } },
                       stage.status ) )
      {
        diags.push_back( LabDiag{ "lab.session.schema",
                                  "stage '" + stage.stageId + "' has an unknown status" } );
        continue;
      }
      session.stages.push_back( std::move( stage ) );
    }
  }
  else
    diags.push_back( LabDiag{ "lab.session.schema", "stages must be an array" } );

  if ( doc[ "checkpoint_results" ].isArray() )
  {
    for ( const Json::Value &node : doc[ "checkpoint_results" ] )
    {
      CheckpointResult result;
      result.checkpointId =
        node[ "checkpoint_id" ].isString() ? node[ "checkpoint_id" ].asString() : std::string{};
      if ( result.checkpointId.empty() )
      {
        diags.push_back( LabDiag{ "lab.session.schema", "checkpoint result needs a checkpoint_id" } );
        continue;
      }
      if ( !parseEnum( node[ "verdict" ],
                       { { "pass", Verdict::Pass },
                         { "fail", Verdict::Fail },
                         { "unverifiable", Verdict::Unverifiable } },
                       result.verdict ) )
      {
        diags.push_back( LabDiag{ "lab.session.schema",
                                  "checkpoint '" + result.checkpointId + "' has an unknown verdict" } );
        continue;
      }
      if ( !node[ "attempt" ].isInt() || !node[ "seq" ].isInt64() )
      {
        diags.push_back( LabDiag{ "lab.session.schema",
                                  "checkpoint '" + result.checkpointId + "' attempt/seq must be integers" } );
        continue;
      }
      result.attempt = node[ "attempt" ].asInt();
      result.seq = node[ "seq" ].asInt64();
      if ( node[ "evidence" ].isArray() )
      {
        for ( const Json::Value &entry : node[ "evidence" ] )
        {
          CheckEvidence evidence;
          if ( !entry[ "check_index" ].isInt() || !entry[ "ok" ].isBool() ||
               !entry[ "observed" ].isString() || !entry[ "expected" ].isString() )
          {
            diags.push_back( LabDiag{ "lab.session.schema",
                                      "evidence entries need check_index/ok/observed/expected" } );
            continue;
          }
          evidence.checkIndex = entry[ "check_index" ].asInt();
          evidence.ok = entry[ "ok" ].asBool();
          evidence.observed = entry[ "observed" ].asString();
          evidence.expected = entry[ "expected" ].asString();
          result.evidence.push_back( std::move( evidence ) );
        }
      }
      session.checkpointResults.push_back( std::move( result ) );
    }
  }
  else
    diags.push_back( LabDiag{ "lab.session.schema", "checkpoint_results must be an array" } );

  if ( doc[ "question_answers" ].isArray() )
  {
    for ( const Json::Value &node : doc[ "question_answers" ] )
    {
      QuestionAnswer answer;
      answer.questionId = node[ "question_id" ].isString() ? node[ "question_id" ].asString() : std::string{};
      answer.answer = node[ "answer" ].isString() ? node[ "answer" ].asString() : std::string{};
      if ( answer.questionId.empty() || !node[ "seq" ].isInt64() )
      {
        diags.push_back( LabDiag{ "lab.session.schema", "question answer needs question_id and seq" } );
        continue;
      }
      if ( node.isMember( "numeric" ) )
      {
        if ( !node[ "numeric" ].isNumeric() )
        {
          diags.push_back( LabDiag{ "lab.session.schema", "answer numeric must be a number" } );
          continue;
        }
        answer.hasNumeric = true;
        answer.numeric = node[ "numeric" ].asDouble();
      }
      answer.seq = node[ "seq" ].asInt64();
      session.questionAnswers.push_back( std::move( answer ) );
    }
  }
  else
    diags.push_back( LabDiag{ "lab.session.schema", "question_answers must be an array" } );

  if ( doc[ "hint_events" ].isArray() )
  {
    for ( const Json::Value &node : doc[ "hint_events" ] )
    {
      HintEvent event;
      event.target = node[ "target" ].isString() ? node[ "target" ].asString() : std::string{};
      if ( event.target.empty() || !node[ "level" ].isInt() || !node[ "seq" ].isInt64() )
      {
        diags.push_back( LabDiag{ "lab.session.schema", "hint event needs target/level/seq" } );
        continue;
      }
      event.level = node[ "level" ].asInt();
      event.seq = node[ "seq" ].asInt64();
      session.hintEvents.push_back( std::move( event ) );
    }
  }
  else
    diags.push_back( LabDiag{ "lab.session.schema", "hint_events must be an array" } );

  if ( doc[ "tool_choices" ].isArray() )
  {
    for ( const Json::Value &node : doc[ "tool_choices" ] )
    {
      ToolChoice choice;
      choice.stageId = node[ "stage_id" ].isString() ? node[ "stage_id" ].asString() : std::string{};
      choice.operatorId = node[ "operator_id" ].isString() ? node[ "operator_id" ].asString() : std::string{};
      if ( choice.stageId.empty() || choice.operatorId.empty() || !node[ "allowed" ].isBool() ||
           !node[ "seq" ].isInt64() )
      {
        diags.push_back( LabDiag{ "lab.session.schema", "tool choice needs stage_id/operator_id/allowed/seq" } );
        continue;
      }
      if ( node[ "params_subset" ].isObject() )
      {
        for ( const auto &key : node[ "params_subset" ].getMemberNames() )
        {
          if ( !node[ "params_subset" ][ key ].isString() )
          {
            diags.push_back( LabDiag{ "lab.session.schema", "params_subset values must be strings" } );
            continue;
          }
          choice.paramsSubset[ key ] = node[ "params_subset" ][ key ].asString();
        }
      }
      choice.allowed = node[ "allowed" ].asBool();
      choice.seq = node[ "seq" ].asInt64();
      session.toolChoices.push_back( std::move( choice ) );
    }
  }
  else
    diags.push_back( LabDiag{ "lab.session.schema", "tool_choices must be an array" } );

  if ( doc[ "execution_refs" ].isArray() )
  {
    for ( const Json::Value &node : doc[ "execution_refs" ] )
    {
      ExecutionRef ref;
      ref.kind = node[ "kind" ].isString() ? node[ "kind" ].asString() : std::string{};
      ref.id = node[ "id" ].isString() ? node[ "id" ].asString() : std::string{};
      if ( ref.kind.empty() || ref.id.empty() || !node[ "seq" ].isInt64() )
      {
        diags.push_back( LabDiag{ "lab.session.schema", "execution ref needs kind/id/seq" } );
        continue;
      }
      ref.seq = node[ "seq" ].asInt64();
      session.executionRefs.push_back( std::move( ref ) );
    }
  }
  else
    diags.push_back( LabDiag{ "lab.session.schema", "execution_refs must be an array" } );

  if ( !diags.empty() )
    return LabResult<LabSession>::failure( std::move( diags ) );
  return LabResult<LabSession>::success( std::move( session ) );
}

} // namespace sicnu::lab
