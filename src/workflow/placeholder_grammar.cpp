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

    size_t startPos = i;
    if ( i + 1 < len && text[i + 1] == '{' )
    {
      size_t closePos = text.find( '}', i + 2 );
      if ( closePos != std::string::npos )
      {
        std::string raw = text.substr( startPos, closePos - startPos + 1 );
        std::string content = text.substr( i + 2, closePos - ( i + 2 ) );
        PlaceholderRef ref = parseSingleBracedRef( raw, content );
        if ( ref.isValid() )
        {
          results.push_back( ref );
        }
        i = closePos + 1;
        continue;
      }
    }

    // Unbraced form: $stepId.portName or $stepId
    size_t j = i + 1;
    while ( j < len && isIdChar( text[j] ) )
    {
      ++j;
    }
    if ( j > i + 1 )
    {
      std::string stepId = text.substr( i + 1, j - i - 1 );
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
      PlaceholderRef ref;
      ref.rawRef = text.substr( startPos, endPos - startPos );
      ref.stepId = stepId;
      ref.portName = portName;
      if ( ref.isValid() )
      {
        results.push_back( ref );
      }
      i = endPos;
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

  auto refs = parsePlaceholders( text );
  if ( refs.empty() )
    return text;

  std::sort( refs.begin(), refs.end(), []( const PlaceholderRef &a, const PlaceholderRef &b ) {
    return a.rawRef.length() > b.rawRef.length();
  } );

  std::string result = text;
  for ( const auto &ref : refs )
  {
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
    {
      size_t pos = 0;
      while ( ( pos = result.find( ref.rawRef, pos ) ) != std::string::npos )
      {
        result.replace( pos, ref.rawRef.length(), replacement );
        pos += replacement.length();
      }
    }
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
