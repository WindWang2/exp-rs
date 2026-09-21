/***************************************************************************
  lab/spec_runtime.cpp — parsing/validation of the LabSpec v3 `runtime` block.
  See spec_runtime.h for the contract. Every rejection is typed and carries a
  locator; nothing is silently coerced, dropped or defaulted away.
***************************************************************************/

#include "spec_runtime.h"

#include "sha256.h"

#include <cstdio>
#include <sstream>

namespace sicnu::lab
{
namespace
{

bool isLowerHex64( const std::string &s )
{
  if ( s.size() != 64 )
    return false;
  for ( const char ch : s )
  {
    const bool ok = ( ch >= '0' && ch <= '9' ) || ( ch >= 'a' && ch <= 'f' );
    if ( !ok )
      return false;
  }
  return true;
}

bool matchesIdPattern( const std::string &s )
{
  // ^[a-z][a-z0-9_]*$
  if ( s.empty() || s[0] < 'a' || s[0] > 'z' )
    return false;
  for ( const char ch : s )
  {
    const bool ok = ( ch >= 'a' && ch <= 'z' ) || ( ch >= '0' && ch <= '9' ) || ch == '_';
    if ( !ok )
      return false;
  }
  return true;
}

bool matchesOperatorId( const std::string &s )
{
  // ^[a-z]+:[a-z0-9_]+$
  const std::size_t colon = s.find( ':' );
  if ( colon == std::string::npos || colon == 0 || colon + 1 == s.size() )
    return false;
  for ( std::size_t i = 0; i < s.size(); ++i )
  {
    const char ch = s[i];
    if ( i == colon )
      continue;
    const bool lower = ch >= 'a' && ch <= 'z';
    const bool digit = ch >= '0' && ch <= '9';
    const bool before = i < colon ? lower : ( lower || digit || ch == '_' );
    if ( !before )
      return false;
  }
  return true;
}

bool matchesAllowedTool( const std::string &s )
{
  // ^[a-z]+:[a-z0-9_*]+$ — exact id, or trailing '*' prefix wildcard.
  if ( s.empty() )
    return false;
  const std::string body = s.back() == '*' ? s.substr( 0, s.size() - 1 ) : s;
  return matchesOperatorId( body );
}

bool matchesPackId( const std::string &s )
{
  // ^[a-z0-9_]+$
  if ( s.empty() )
    return false;
  for ( const char ch : s )
  {
    const bool ok = ( ch >= 'a' && ch <= 'z' ) || ( ch >= '0' && ch <= '9' ) || ch == '_';
    if ( !ok )
      return false;
  }
  return true;
}

/// Project-root-relative, no absolute form, no `..` escape, conservative
/// character set. Checked at parse time so an unsafe path can never reach
/// the filesystem layer.
bool safeRelativePath( const std::string &s )
{
  if ( s.empty() || s.front() == '/' || s.find( '\\' ) != std::string::npos )
    return false;
  std::string component;
  std::istringstream stream( s );
  while ( std::getline( stream, component, '/' ) )
  {
    if ( component.empty() || component == "." || component == ".." )
      return false;
    for ( const char ch : component )
    {
      const bool ok = ( ch >= 'a' && ch <= 'z' ) || ( ch >= 'A' && ch <= 'Z' ) ||
                      ( ch >= '0' && ch <= '9' ) || ch == '_' || ch == '-' || ch == '.';
      if ( !ok )
        return false;
    }
  }
  return true;
}

std::string scalarToString( const Json::Value &v, bool &ok )
{
  ok = true;
  switch ( v.type() )
  {
    case Json::stringValue:
      return v.asString();
    case Json::intValue:
      return std::to_string( v.asInt64() );
    case Json::uintValue:
      return std::to_string( v.asUInt64() );
    case Json::realValue:
    {
      std::ostringstream out;
      out << v.asDouble();
      return out.str();
    }
    case Json::booleanValue:
      return v.asBool() ? "true" : "false";
    default:
      ok = false;
      return {};
  }
}

class Parser
{
  public:
    explicit Parser( const Json::Value &labDoc )
      : m_labDoc( labDoc )
      , m_stepCount( labDoc[ "steps" ].isArray() ? labDoc[ "steps" ].size() : 0u )
    {
    }

    LabResult<LabRuntimePlan> run()
    {
      const Json::Value &runtime = m_labDoc[ "runtime" ];
      if ( !runtime.isObject() )
      {
        add( "lab.runtime.schema", "runtime must be an object" );
        return LabResult<LabRuntimePlan>::failure( std::move( m_diags ) );
      }

      static const char *kTopKeys[] = { "data_packs", "stages", "questions", "hints", "reproducibility" };
      rejectUnknownKeys( runtime, "runtime", kTopKeys, 5 );

      parseDataPacks( runtime );
      parseQuestions( runtime );
      parseStages( runtime );
      parseHints( runtime );
      parseReproducibility( runtime );

      if ( !m_diags.empty() )
        return LabResult<LabRuntimePlan>::failure( std::move( m_diags ) );
      return LabResult<LabRuntimePlan>::success( std::move( m_plan ) );
    }

  private:
    void add( std::string code, std::string message )
    {
      m_diags.push_back( LabDiag{ std::move( code ), std::move( message ) } );
    }

    void rejectUnknownKeys( const Json::Value &object, const std::string &locator,
                            const char *const *allowed, std::size_t count )
    {
      for ( const auto &key : object.getMemberNames() )
      {
        bool known = false;
        for ( std::size_t i = 0; i < count; ++i )
          known |= key == allowed[i];
        if ( !known )
          add( "lab.runtime.schema", locator + "." + key + ": unknown key" );
      }
    }

    static bool hasKey( const Json::Value &object, const char *key )
    {
      return object.isMember( key ) && !object[ key ].isNull();
    }

    static std::string requireString( const Json::Value &object, const char *key,
                                      const std::string &locator, bool &ok )
    {
      ok = false;
      const Json::Value &v = object[ key ];
      if ( !v.isString() || v.asString().empty() )
        return {};
      ok = true;
      return v.asString();
    }

    void parseDataPacks( const Json::Value &runtime )
    {
      if ( !hasKey( runtime, "data_packs" ) )
        return;
      const Json::Value &packs = runtime[ "data_packs" ];
      if ( !packs.isArray() )
      {
        add( "lab.runtime.schema", "runtime.data_packs must be an array" );
        return;
      }
      for ( Json::ArrayIndex i = 0; i < packs.size(); ++i )
      {
        const Json::Value &entry = packs[ i ];
        if ( !entry.isString() || !matchesPackId( entry.asString() ) )
        {
          add( "lab.runtime.field",
               "runtime.data_packs[" + std::to_string( i ) + "]: pack id must match ^[a-z0-9_]+$" );
          continue;
        }
        m_plan.dataPacks.push_back( entry.asString() );
      }
    }

    void parseQuestions( const Json::Value &runtime )
    {
      if ( !hasKey( runtime, "questions" ) )
        return;
      const Json::Value &questions = runtime[ "questions" ];
      if ( !questions.isArray() )
      {
        add( "lab.runtime.schema", "runtime.questions must be an array" );
        return;
      }
      for ( Json::ArrayIndex i = 0; i < questions.size(); ++i )
      {
        const std::string locator = "runtime.questions[" + std::to_string( i ) + "]";
        const Json::Value &node = questions[ i ];
        if ( !node.isObject() )
        {
          add( "lab.runtime.schema", locator + " must be an object" );
          continue;
        }
        static const char *kKeys[] = { "id", "prompt", "prompt_zh", "kind", "choices", "expected_numeric" };
        rejectUnknownKeys( node, locator, kKeys, 6 );

        Question question;
        bool ok = false;
        question.id = requireString( node, "id", locator, ok );
        if ( !ok || !matchesIdPattern( question.id ) )
          add( "lab.runtime.field", locator + ".id: required, must match ^[a-z][a-z0-9_]*$" );
        question.prompt = requireString( node, "prompt", locator, ok );
        if ( !ok )
          add( "lab.runtime.schema", locator + ".prompt: required non-empty string" );
        if ( hasKey( node, "prompt_zh" ) )
          question.promptZh = node[ "prompt_zh" ].asString();

        const std::string kind = hasKey( node, "kind" ) && node[ "kind" ].isString()
                                   ? node[ "kind" ].asString()
                                   : std::string{};
        if ( kind == "free_text" )
          question.kind = QuestionKind::FreeText;
        else if ( kind == "numeric" )
          question.kind = QuestionKind::Numeric;
        else if ( kind == "choice" )
          question.kind = QuestionKind::Choice;
        else
        {
          add( "lab.runtime.field", locator + ".kind: must be free_text|numeric|choice" );
          continue;
        }

        if ( question.kind == QuestionKind::Choice )
        {
          if ( !hasKey( node, "choices" ) || !node[ "choices" ].isArray() || node[ "choices" ].empty() )
          {
            add( "lab.runtime.field", locator + ".choices: required non-empty array for choice questions" );
            continue;
          }
          for ( const auto &choice : node[ "choices" ] )
            question.choices.push_back( choice.asString() );
        }
        if ( question.kind == QuestionKind::Numeric && hasKey( node, "expected_numeric" ) )
        {
          const Json::Value &range = node[ "expected_numeric" ];
          if ( !range.isObject() )
          {
            add( "lab.runtime.schema", locator + ".expected_numeric must be an object" );
            continue;
          }
          question.hasExpectedNumeric = true;
          question.expectedMin = range[ "min" ].isNumeric() ? range[ "min" ].asDouble() : 0.0;
          question.expectedMax = range[ "max" ].isNumeric() ? range[ "max" ].asDouble() : 0.0;
          if ( question.expectedMin > question.expectedMax )
          {
            add( "lab.runtime.field", locator + ".expected_numeric: min must be <= max" );
            continue;
          }
        }
        if ( question.kind == QuestionKind::FreeText &&
             ( hasKey( node, "choices" ) || hasKey( node, "expected_numeric" ) ) )
        {
          add( "lab.runtime.field", locator + ": free_text questions take neither choices nor expected_numeric" );
          continue;
        }

        m_questions.push_back( question );
        m_plan.questions.push_back( std::move( question ) );
      }

      for ( std::size_t i = 0; i < m_plan.questions.size(); ++i )
      {
        for ( std::size_t j = i + 1; j < m_plan.questions.size(); ++j )
        {
          if ( m_plan.questions[i].id == m_plan.questions[j].id )
            add( "lab.runtime.field", "runtime.questions: duplicate id '" + m_plan.questions[i].id + "'" );
        }
      }
    }

    bool questionExists( const std::string &id ) const
    {
      for ( const Question &q : m_questions )
      {
        if ( q.id == id )
          return true;
      }
      return false;
    }

    bool checkpointExists( const std::string &id ) const
    {
      for ( const Stage &stage : m_plan.stages )
      {
        for ( const Checkpoint &ckpt : stage.checkpoints )
        {
          if ( ckpt.id == id )
            return true;
        }
      }
      return false;
    }

    void parseStages( const Json::Value &runtime )
    {
      if ( !hasKey( runtime, "stages" ) )
      {
        add( "lab.runtime.schema", "runtime.stages: required array" );
        return;
      }
      const Json::Value &stages = runtime[ "stages" ];
      if ( !stages.isArray() || stages.empty() )
      {
        add( "lab.runtime.field", "runtime.stages: must be a non-empty array" );
        return;
      }
      for ( Json::ArrayIndex i = 0; i < stages.size(); ++i )
      {
        const std::string locator = "runtime.stages[" + std::to_string( i ) + "]";
        const Json::Value &node = stages[ i ];
        if ( !node.isObject() )
        {
          add( "lab.runtime.schema", locator + " must be an object" );
          continue;
        }
        static const char *kKeys[] = { "id", "title", "title_zh", "objective", "objective_zh",
                                       "step_indices", "allowed_tools", "checkpoints" };
        rejectUnknownKeys( node, locator, kKeys, 8 );

        Stage stage;
        bool ok = false;
        stage.id = requireString( node, "id", locator, ok );
        if ( !ok || !matchesIdPattern( stage.id ) )
          add( "lab.runtime.field", locator + ".id: required, must match ^[a-z][a-z0-9_]*$" );
        stage.title = requireString( node, "title", locator, ok );
        if ( !ok )
          add( "lab.runtime.schema", locator + ".title: required non-empty string" );
        stage.titleZh = requireString( node, "title_zh", locator, ok );
        if ( !ok )
          add( "lab.runtime.schema", locator + ".title_zh: required non-empty string" );
        stage.objective = requireString( node, "objective", locator, ok );
        if ( !ok )
          add( "lab.runtime.schema", locator + ".objective: required non-empty string" );
        if ( hasKey( node, "objective_zh" ) )
          stage.objectiveZh = node[ "objective_zh" ].asString();

        if ( !hasKey( node, "step_indices" ) || !node[ "step_indices" ].isArray() )
        {
          add( "lab.runtime.schema", locator + ".step_indices: required array" );
          continue;
        }
        const Json::Value &indices = node[ "step_indices" ];
        int previous = -1;
        for ( Json::ArrayIndex j = 0; j < indices.size(); ++j )
        {
          const Json::Value &index = indices[ j ];
          if ( !index.isInt() )
          {
            add( "lab.runtime.field", locator + ".step_indices[" + std::to_string( j ) + "]: must be an integer" );
            continue;
          }
          const int value = index.asInt();
          if ( value < 0 || static_cast<unsigned>( value ) >= m_stepCount )
          {
            add( "lab.runtime.field", locator + ".step_indices[" + std::to_string( j ) +
                                        "]: out of range (document has " + std::to_string( m_stepCount ) +
                                        " steps)" );
            continue;
          }
          if ( value <= previous )
          {
            add( "lab.runtime.field", locator + ".step_indices[" + std::to_string( j ) + "]: must strictly ascend" );
            continue;
          }
          previous = value;
          stage.stepIndices.push_back( value );
        }

        if ( hasKey( node, "allowed_tools" ) )
        {
          const Json::Value &tools = node[ "allowed_tools" ];
          if ( !tools.isArray() )
          {
            add( "lab.runtime.schema", locator + ".allowed_tools must be an array" );
            continue;
          }
          for ( Json::ArrayIndex j = 0; j < tools.size(); ++j )
          {
            const Json::Value &tool = tools[ j ];
            if ( !tool.isString() || !matchesAllowedTool( tool.asString() ) )
            {
              add( "lab.runtime.field",
                   locator + ".allowed_tools[" + std::to_string( j ) +
                     "]: must be an operator id or trailing-'*' prefix" );
              continue;
            }
            stage.allowedTools.push_back( tool.asString() );
          }
        }

        if ( hasKey( node, "checkpoints" ) )
        {
          const Json::Value &checkpoints = node[ "checkpoints" ];
          if ( !checkpoints.isArray() )
          {
            add( "lab.runtime.schema", locator + ".checkpoints must be an array" );
            continue;
          }
          for ( Json::ArrayIndex j = 0; j < checkpoints.size(); ++j )
            parseCheckpoint( checkpoints[ j ], locator + ".checkpoints[" + std::to_string( j ) + "]", stage );
        }

        for ( const Stage &existing : m_plan.stages )
        {
          if ( existing.id == stage.id )
            add( "lab.runtime.field", locator + ".id: duplicate stage id '" + stage.id + "'" );
        }
        m_plan.stages.push_back( std::move( stage ) );
      }

      for ( std::size_t i = 0; i < m_plan.stages.size(); ++i )
      {
        for ( std::size_t j = i + 1; j < m_plan.stages.size(); ++j )
        {
          for ( const int left : m_plan.stages[i].stepIndices )
          {
            for ( const int right : m_plan.stages[j].stepIndices )
            {
              if ( left == right )
                add( "lab.runtime.field", "runtime.stages[" + std::to_string( j ) +
                                            "]: step index " + std::to_string( right ) +
                                            " already claimed by stages[" + std::to_string( i ) + "]" );
            }
          }
        }
      }
    }

    void parseCheckpoint( const Json::Value &node, const std::string &locator, Stage &stage )
    {
      if ( !node.isObject() )
      {
        add( "lab.runtime.schema", locator + " must be an object" );
        return;
      }
      static const char *kKeys[] = { "id", "title", "title_zh", "checks", "attempts_allowed", "advance" };
      rejectUnknownKeys( node, locator, kKeys, 6 );

      Checkpoint checkpoint;
      bool ok = false;
      checkpoint.id = requireString( node, "id", locator, ok );
      if ( !ok || !matchesIdPattern( checkpoint.id ) )
        add( "lab.runtime.field", locator + ".id: required, must match ^[a-z][a-z0-9_]*$" );
      checkpoint.title = requireString( node, "title", locator, ok );
      if ( !ok )
        add( "lab.runtime.schema", locator + ".title: required non-empty string" );
      checkpoint.titleZh = requireString( node, "title_zh", locator, ok );
      if ( !ok )
        add( "lab.runtime.schema", locator + ".title_zh: required non-empty string" );

      if ( !hasKey( node, "checks" ) || !node[ "checks" ].isArray() || node[ "checks" ].empty() )
      {
        add( "lab.runtime.field", locator + ".checks: required non-empty array" );
        return;
      }
      const Json::Value &checks = node[ "checks" ];
      for ( Json::ArrayIndex k = 0; k < checks.size(); ++k )
        parseCheck( checks[ k ], locator + ".checks[" + std::to_string( k ) + "]", checkpoint );

      if ( hasKey( node, "attempts_allowed" ) )
      {
        const Json::Value &attempts = node[ "attempts_allowed" ];
        if ( !attempts.isInt() || attempts.asInt() < 0 )
        {
          add( "lab.runtime.field", locator + ".attempts_allowed: must be a non-negative integer" );
          return;
        }
        checkpoint.attemptsAllowed = attempts.asInt();
      }
      if ( hasKey( node, "advance" ) )
      {
        const Json::Value &advance = node[ "advance" ];
        if ( !advance.isString() || ( advance.asString() != "observe" && advance.asString() != "gate" ) )
        {
          add( "lab.runtime.field", locator + ".advance: must be observe|gate" );
          return;
        }
        checkpoint.advance = advance.asString() == "gate" ? AdvancePolicy::Gate : AdvancePolicy::Observe;
      }

      bool duplicate = checkpointExists( checkpoint.id );
      for ( const Checkpoint &existing : stage.checkpoints )
        duplicate |= existing.id == checkpoint.id;
      if ( duplicate )
        add( "lab.runtime.field", locator + ".id: duplicate checkpoint id '" + checkpoint.id + "'" );
      stage.checkpoints.push_back( std::move( checkpoint ) );
    }

    void parseCheck( const Json::Value &node, const std::string &locator, Checkpoint &checkpoint )
    {
      if ( !node.isObject() )
      {
        add( "lab.runtime.schema", locator + " must be an object" );
        return;
      }
      static const char *kKeys[] = { "kind", "path", "min_bytes", "sha256", "operator_id",
                                     "operator_id_any", "params_subset", "question_id" };
      rejectUnknownKeys( node, locator, kKeys, 8 );

      CheckpointCheck check;
      if ( !hasKey( node, "kind" ) || !node[ "kind" ].isString() )
      {
        add( "lab.runtime.schema", locator + ".kind: required string" );
        return;
      }
      const std::string kind = node[ "kind" ].asString();
      if ( kind == "artifact_present" )
        check.kind = CheckKind::ArtifactPresent;
      else if ( kind == "operator_invoked" )
        check.kind = CheckKind::OperatorInvoked;
      else if ( kind == "question_answered" )
        check.kind = CheckKind::QuestionAnswered;
      else
      {
        add( "lab.runtime.field", locator + ".kind: unknown check kind '" + kind + "'" );
        return;
      }

      switch ( check.kind )
      {
        case CheckKind::ArtifactPresent:
        {
          bool ok = false;
          check.path = requireString( node, "path", locator, ok );
          if ( !ok || !safeRelativePath( check.path ) )
          {
            add( "lab.runtime.field",
                 locator + ".path: required project-root-relative path without '..' escapes" );
            return;
          }
          if ( hasKey( node, "min_bytes" ) )
          {
            const Json::Value &minBytes = node[ "min_bytes" ];
            if ( !minBytes.isInt64() || minBytes.asInt64() < 0 )
            {
              add( "lab.runtime.field", locator + ".min_bytes: must be a non-negative integer" );
              return;
            }
            check.minBytes = minBytes.asInt64();
          }
          if ( hasKey( node, "sha256" ) )
          {
            const Json::Value &sha = node[ "sha256" ];
            if ( !sha.isString() || !isLowerHex64( sha.asString() ) )
            {
              add( "lab.runtime.field", locator + ".sha256: must be 64 lowercase hex chars" );
              return;
            }
            check.sha256 = sha.asString();
          }
          break;
        }
        case CheckKind::OperatorInvoked:
        {
          const bool hasSingle = hasKey( node, "operator_id" );
          const bool hasAny = hasKey( node, "operator_id_any" );
          if ( hasSingle == hasAny )
          {
            add( "lab.runtime.field", locator + ": exactly one of operator_id|operator_id_any is required" );
            return;
          }
          if ( hasSingle )
          {
            const Json::Value &id = node[ "operator_id" ];
            if ( !id.isString() || !matchesOperatorId( id.asString() ) )
            {
              add( "lab.runtime.field", locator + ".operator_id: must match ^[a-z]+:[a-z0-9_]+$" );
              return;
            }
            check.operatorId = id.asString();
          }
          else
          {
            const Json::Value &any = node[ "operator_id_any" ];
            if ( !any.isArray() || any.empty() )
            {
              add( "lab.runtime.field", locator + ".operator_id_any: required non-empty array" );
              return;
            }
            for ( const auto &id : any )
            {
              if ( !id.isString() || !matchesOperatorId( id.asString() ) )
              {
                add( "lab.runtime.field", locator + ".operator_id_any: entries must match ^[a-z]+:[a-z0-9_]+$" );
                return;
              }
              check.operatorIdAny.push_back( id.asString() );
            }
          }
          if ( hasKey( node, "params_subset" ) )
          {
            const Json::Value &params = node[ "params_subset" ];
            if ( !params.isObject() )
            {
              add( "lab.runtime.schema", locator + ".params_subset must be an object" );
              return;
            }
            for ( const auto &key : params.getMemberNames() )
            {
              bool ok = false;
              const std::string scalar = scalarToString( params[ key ], ok );
              if ( key.empty() || !ok )
              {
                add( "lab.runtime.field", locator + ".params_subset: values must be scalars" );
                return;
              }
              check.paramsSubset[ key ] = scalar;
            }
          }
          break;
        }
        case CheckKind::QuestionAnswered:
        {
          bool ok = false;
          check.questionId = requireString( node, "question_id", locator, ok );
          if ( !ok || !matchesIdPattern( check.questionId ) )
          {
            add( "lab.runtime.field", locator + ".question_id: required, must match ^[a-z][a-z0-9_]*$" );
            return;
          }
          if ( !questionExists( check.questionId ) )
            add( "lab.runtime.reference",
                 locator + ".question_id: unknown question '" + check.questionId + "'" );
          break;
        }
      }

      checkpoint.checks.push_back( std::move( check ) );
    }

    void parseHints( const Json::Value &runtime )
    {
      if ( !hasKey( runtime, "hints" ) )
        return;
      const Json::Value &hints = runtime[ "hints" ];
      if ( !hints.isObject() )
      {
        add( "lab.runtime.schema", "runtime.hints must be an object" );
        return;
      }
      static const char *kKeys[] = { "policy", "entries" };
      rejectUnknownKeys( hints, "runtime.hints", kKeys, 2 );

      if ( hasKey( hints, "policy" ) )
      {
        const Json::Value &policy = hints[ "policy" ];
        if ( !policy.isObject() )
        {
          add( "lab.runtime.schema", "runtime.hints.policy must be an object" );
          return;
        }
        static const char *kPolicyKeys[] = { "max_reveals_per_target", "escalation" };
        rejectUnknownKeys( policy, "runtime.hints.policy", kPolicyKeys, 2 );
        if ( hasKey( policy, "max_reveals_per_target" ) )
        {
          const Json::Value &max = policy[ "max_reveals_per_target" ];
          if ( !max.isInt() || max.asInt() < 0 )
          {
            add( "lab.runtime.field", "runtime.hints.policy.max_reveals_per_target: must be a non-negative integer" );
            return;
          }
          m_plan.hints.policy.maxRevealsPerTarget = max.asInt();
        }
        if ( hasKey( policy, "escalation" ) )
        {
          const Json::Value &escalation = policy[ "escalation" ];
          if ( !escalation.isArray() || escalation.empty() )
          {
            add( "lab.runtime.field", "runtime.hints.policy.escalation: required non-empty array" );
            return;
          }
          m_plan.hints.policy.escalation.clear();
          for ( const auto &level : escalation )
          {
            if ( !level.isString() || level.asString().empty() )
            {
              add( "lab.runtime.field", "runtime.hints.policy.escalation: entries must be non-empty strings" );
              return;
            }
            m_plan.hints.policy.escalation.push_back( level.asString() );
          }
        }
      }

      if ( !hasKey( hints, "entries" ) )
        return;
      const Json::Value &entries = hints[ "entries" ];
      if ( !entries.isArray() )
      {
        add( "lab.runtime.schema", "runtime.hints.entries must be an array" );
        return;
      }
      for ( Json::ArrayIndex i = 0; i < entries.size(); ++i )
      {
        const std::string locator = "runtime.hints.entries[" + std::to_string( i ) + "]";
        const Json::Value &node = entries[ i ];
        if ( !node.isObject() )
        {
          add( "lab.runtime.schema", locator + " must be an object" );
          continue;
        }
        static const char *kEntryKeys[] = { "target_step", "target_checkpoint", "level", "text", "text_zh" };
        rejectUnknownKeys( node, locator, kEntryKeys, 5 );

        HintEntry entry;
        bool ok = false;
        entry.text = requireString( node, "text", locator, ok );
        if ( !ok )
          add( "lab.runtime.schema", locator + ".text: required non-empty string" );
        if ( hasKey( node, "text_zh" ) )
          entry.textZh = node[ "text_zh" ].asString();

        entry.level = hasKey( node, "level" ) && node[ "level" ].isInt() ? node[ "level" ].asInt() : 1;
        if ( entry.level < 1 || entry.level > static_cast<int>( m_plan.hints.policy.escalation.size() ) )
        {
          add( "lab.runtime.field", locator + ".level: must be within 1.." +
                                      std::to_string( m_plan.hints.policy.escalation.size() ) );
          continue;
        }

        const bool hasStep = hasKey( node, "target_step" );
        const bool hasCheckpoint = hasKey( node, "target_checkpoint" );
        if ( hasStep == hasCheckpoint )
        {
          add( "lab.runtime.field", locator + ": exactly one of target_step|target_checkpoint is required" );
          continue;
        }
        if ( hasStep )
        {
          const Json::Value &step = node[ "target_step" ];
          if ( !step.isInt() )
          {
            add( "lab.runtime.field", locator + ".target_step: must be an integer" );
            continue;
          }
          if ( step.asInt() < 0 || static_cast<unsigned>( step.asInt() ) >= m_stepCount )
          {
            add( "lab.runtime.reference", locator + ".target_step: out of range (document has " +
                                            std::to_string( m_stepCount ) + " steps)" );
            continue;
          }
          entry.hasStep = true;
          entry.targetStep = step.asInt();
        }
        else
        {
          const Json::Value &ckpt = node[ "target_checkpoint" ];
          if ( !ckpt.isString() )
          {
            add( "lab.runtime.field", locator + ".target_checkpoint: must be a string" );
            continue;
          }
          if ( !checkpointExists( ckpt.asString() ) )
          {
            add( "lab.runtime.reference", locator + ".target_checkpoint: unknown checkpoint '" + ckpt.asString() + "'" );
            continue;
          }
          entry.hasCheckpoint = true;
          entry.targetCheckpoint = ckpt.asString();
        }

        m_plan.hints.entries.push_back( std::move( entry ) );
      }
    }

    void parseReproducibility( const Json::Value &runtime )
    {
      if ( !hasKey( runtime, "reproducibility" ) )
        return;
      const Json::Value &repro = runtime[ "reproducibility" ];
      if ( !repro.isObject() )
      {
        add( "lab.runtime.schema", "runtime.reproducibility must be an object" );
        return;
      }
      static const char *kKeys[] = { "require_seed", "deterministic_operators_only", "notes" };
      rejectUnknownKeys( repro, "runtime.reproducibility", kKeys, 3 );
      if ( hasKey( repro, "require_seed" ) )
      {
        if ( !repro[ "require_seed" ].isBool() )
        {
          add( "lab.runtime.schema", "runtime.reproducibility.require_seed must be a boolean" );
          return;
        }
        m_plan.reproducibility.requireSeed = repro[ "require_seed" ].asBool();
      }
      if ( hasKey( repro, "deterministic_operators_only" ) )
      {
        if ( !repro[ "deterministic_operators_only" ].isBool() )
        {
          add( "lab.runtime.schema",
               "runtime.reproducibility.deterministic_operators_only must be a boolean" );
          return;
        }
        m_plan.reproducibility.deterministicOperatorsOnly = repro[ "deterministic_operators_only" ].asBool();
      }
      if ( hasKey( repro, "notes" ) )
      {
        if ( !repro[ "notes" ].isString() )
        {
          add( "lab.runtime.schema", "runtime.reproducibility.notes must be a string" );
          return;
        }
        m_plan.reproducibility.notes = repro[ "notes" ].asString();
      }
    }

    const Json::Value &m_labDoc;
    unsigned m_stepCount = 0;
    std::vector<LabDiag> m_diags;
    LabRuntimePlan m_plan;
    std::vector<Question> m_questions; // parse order mirror for reference checks
};

} // namespace

bool hasRuntimeBlock( const Json::Value &labDoc )
{
  return labDoc.isObject() && labDoc.isMember( "runtime" ) && labDoc[ "runtime" ].isObject();
}

LabResult<LabRuntimePlan> parseRuntimeBlock( const Json::Value &labDoc )
{
  Parser parser( labDoc );
  return parser.run();
}

std::string specFingerprint( std::string_view specBytes )
{
  return sha256Hex( specBytes );
}

} // namespace sicnu::lab
