// src/workflow/placeholder_grammar.cpp — Unified placeholder grammar module
#include "placeholder_grammar.h"

#include <QString>

#include <cctype>
#include <regex>

namespace sicnu::workflow {

namespace {

bool isIdChar( char c )
{
  return std::isalnum( static_cast<unsigned char>( c ) ) || c == '_' || c == '-';
}

PlaceholderRef parseSingleBracedRef( const std::string &raw, const std::string &content )
{
  PlaceholderRef ref;
  ref.rawRef = raw;

  if ( content.rfind( "env.", 0 ) == 0 )
  {
    ref.isEnvVar = true;
    ref.envVarName = content.substr( 4 );
    return ref;
  }

  if ( content.rfind( "task.", 0 ) == 0 ) // starts with task.
  {
    std::string rest = content.substr( 5 );
    if ( rest.rfind( "parent.", 0 ) == 0 )
    {
      ref.isParentKeyword = true;
      ref.portName = rest.substr( 7 );
      if ( ref.portName.empty() )
        ref.portName = "output";
    }
    else if ( rest == "parent" )
    {
      ref.isParentKeyword = true;
      ref.portName = "output";
    }
    else
    {
      auto dotPos = rest.find( '.' );
      std::string idStr = ( dotPos != std::string::npos ) ? rest.substr( 0, dotPos ) : rest;
      std::string portStr = ( dotPos != std::string::npos ) ? rest.substr( dotPos + 1 ) : "output";
      try
      {
        ref.parentTaskId = std::stol( idStr );
        ref.portName = portStr.empty() ? "output" : portStr;
      }
      catch ( ... )
      {
        ref.stepId = content;
      }
    }
  }
  else
  {
    auto dotPos = content.find( '.' );
    if ( dotPos != std::string::npos )
    {
      ref.stepId = content.substr( 0, dotPos );
      ref.portName = content.substr( dotPos + 1 );
      if ( ref.portName.empty() )
        ref.portName = "output";
    }
    else
    {
      // No dot: check if uppercase environment variable (e.g. ${WORK}, ${LANDSAT_MTL_OR_SCENE_DIR})
      bool allUpperOrUnderscore = !content.empty();
      for ( char c : content )
      {
        if ( !std::isupper( static_cast<unsigned char>( c ) ) && !std::isdigit( static_cast<unsigned char>( c ) ) && c != '_' )
        {
          allUpperOrUnderscore = false;
          break;
        }
      }
      if ( allUpperOrUnderscore )
      {
        ref.isEnvVar = true;
        ref.envVarName = content;
      }
      else
      {
        ref.stepId = content;
        ref.portName = "output";
      }
    }
  }
  return ref;
}

// Matches the placeholder token starting at @p start (which must be '$').
// On a match, @p consumed receives the token length and the returned ref
// carries rawRef plus the parsed fields; an invalid ref (e.g. "${}") still
// consumes its token so it is never re-scanned as a nested placeholder.
// Returns an invalid ref with @p consumed == 0 when no token starts here.
PlaceholderRef matchPlaceholderAt( const std::string &text, size_t start, size_t &consumed )
{
  consumed = 0;
  const size_t len = text.size();
  if ( start >= len || text[start] != '$' )
    return PlaceholderRef();

  if ( start + 1 < len && text[start + 1] == '{' )
  {
    size_t closePos = text.find( '}', start + 2 );
    if ( closePos != std::string::npos )
    {
      std::string raw = text.substr( start, closePos - start + 1 );
      std::string content = text.substr( start + 2, closePos - ( start + 2 ) );
      consumed = closePos - start + 1;
      return parseSingleBracedRef( raw, content );
    }
  }

  // Unbraced form: $stepId.portName or $stepId
  size_t j = start + 1;
  while ( j < len && isIdChar( text[j] ) )
  {
    ++j;
  }
  if ( j > start + 1 )
  {
    std::string stepId = text.substr( start + 1, j - start - 1 );
    std::string portName = "output";
    size_t endPos = j;
    if ( j < len && text[j] == '.' )
    {
      size_t k = j + 1;
      while ( k < len && isIdChar( text[k] ) )
      {
        ++k;
      }
      if ( k > j + 1 )
      {
        portName = text.substr( j + 1, k - j - 1 );
        endPos = k;
      }
    }
    consumed = endPos - start;
    PlaceholderRef ref;
    ref.rawRef = text.substr( start, consumed );
    ref.stepId = stepId;
    ref.portName = portName;
    return ref;
  }

  return PlaceholderRef();
}

} // namespace

std::vector<PlaceholderRef> parsePlaceholders( const std::string &text )
{
  std::vector<PlaceholderRef> results;
  if ( text.empty() )
    return results;

  size_t i = 0;
  const size_t len = text.size();

  while ( i < len )
  {
    if ( text[i] != '$' )
    {
      ++i;
      continue;
    }

    size_t consumed = 0;
    const PlaceholderRef ref = matchPlaceholderAt( text, i, consumed );
    if ( consumed > 0 )
    {
      if ( ref.isValid() )
      {
        results.push_back( ref );
      }
      i += consumed;
      continue;
    }

    ++i;
  }

  return results;
}

std::string resolvePlaceholderPort( const Json::Value &resultPayload,
                                    const std::string &canonicalOutputPath,
                                    const std::string &portName )
{
  // 1. Exact port key, string, non-empty.
  if ( resultPayload.isObject() && resultPayload.isMember( portName )
       && resultPayload[portName].isString() )
  {
    const std::string s = resultPayload[portName].asString();
    if ( !s.empty() )
      return s;
  }
  // 2. Canonical/default output: the task's detected output path when the
  //    caller has one, else the payload's "output" convention.
  if ( !canonicalOutputPath.empty() )
    return canonicalOutputPath;
  if ( resultPayload.isObject() && resultPayload.isMember( "output" )
       && resultPayload["output"].isString() )
  {
    const std::string s = resultPayload["output"].asString();
    if ( !s.empty() )
      return s;
  }
  // 3. Case-insensitive portName scan of the payload.
  if ( resultPayload.isObject() )
  {
    for ( const auto &name : resultPayload.getMemberNames() )
    {
      if ( QString::fromStdString( name ).compare( QString::fromStdString( portName ),
                                                   Qt::CaseInsensitive ) == 0
           && resultPayload[name].isString() )
      {
        const std::string s = resultPayload[name].asString();
        if ( !s.empty() )
          return s;
      }
    }
  }
  return std::string();
}

std::string substitutePlaceholders( const std::string &text,
                                    const std::function<std::string( const PlaceholderRef &ref )> &resolver )
{
  if ( !resolver || text.empty() )
    return text;

  // Single left-to-right pass over the ORIGINAL text: every resolved value is
  // appended to the output and never re-scanned. Re-scanning already-substituted
  // text let a resolved value that itself contains another ref's rawRef (e.g. a
  // user-selected input path holding a literal '$') be silently rewritten by a
  // later ref (#1594).
  std::string result;
  result.reserve( text.size() );

  size_t i = 0;
  const size_t len = text.size();
  while ( i < len )
  {
    if ( text[i] != '$' )
    {
      result.push_back( text[i] );
      ++i;
      continue;
    }

    size_t consumed = 0;
    const PlaceholderRef ref = matchPlaceholderAt( text, i, consumed );
    if ( consumed == 0 || !ref.isValid() )
    {
      // Not a valid placeholder: copy the token verbatim so a malformed token
      // is never re-scanned as a nested one; a bare '$' just passes through.
      const size_t copyLen = ( consumed > 0 ) ? consumed : 1;
      result.append( text, i, copyLen );
      i += copyLen;
      continue;
    }

    std::string replacement = resolver( ref );
    if ( ( replacement.empty() || replacement == ref.rawRef ) && ref.isEnvVar )
    {
      const char *val = std::getenv( ref.envVarName.c_str() );
      if ( val )
      {
        replacement = std::string( val );
      }
    }
    if ( !replacement.empty() && replacement != ref.rawRef )
      result += replacement;
    else
      result += ref.rawRef;
    i += consumed;
  }
  return result;
}

std::vector<StepConnection> inferStepConnections( const std::string &paramKey, const std::string &paramValue )
{
  std::vector<StepConnection> connections;
  auto refs = parsePlaceholders( paramValue );
  for ( const auto &ref : refs )
  {
    if ( !ref.isEnvVar && !ref.stepId.empty() )
    {
      StepConnection conn;
      conn.fromStepId = ref.stepId;
      conn.fromPort = ref.portName;
      conn.toPort = paramKey;
      connections.push_back( conn );
    }
  }
  return connections;
}

} // namespace sicnu::workflow
