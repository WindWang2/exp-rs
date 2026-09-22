// src/recipes/recipe_diagnostics.cpp
#include "recipes/recipe_diagnostics.h"

namespace sicnu::recipes {

const char *diagnosticSeverityString( DiagnosticSeverity severity )
{
  switch ( severity )
  {
    case DiagnosticSeverity::Error:   return "error";
    case DiagnosticSeverity::Warning: return "warning";
    case DiagnosticSeverity::Info:    return "info";
  }
  return "warning";
}

Json::Value RecipeDiagnostic::toJson() const
{
  Json::Value doc( Json::objectValue );
  doc["code"] = code;
  doc["severity"] = diagnosticSeverityString( severity );
  if ( !stageId.empty() )
    doc["stage_id"] = stageId;
  if ( !field.empty() )
    doc["field"] = field;
  doc["message"] = message;
  return doc;
}

bool hasErrors( const RecipeDiagnostics &diagnostics )
{
  for ( const auto &d : diagnostics )
    if ( d.severity == DiagnosticSeverity::Error )
      return true;
  return false;
}

Json::Value diagnosticsToJson( const RecipeDiagnostics &diagnostics )
{
  Json::Value out( Json::arrayValue );
  for ( const auto &d : diagnostics )
    out.append( d.toJson() );
  return out;
}

std::vector<std::string> diagnosticStrings( const RecipeDiagnostics &diagnostics )
{
  std::vector<std::string> out;
  out.reserve( diagnostics.size() );
  for ( const auto &d : diagnostics )
  {
    std::string line = d.code;
    line += "[" + std::string( diagnosticSeverityString( d.severity ) ) + "]";
    if ( !d.stageId.empty() )
      line += " " + d.stageId;
    if ( !d.field.empty() )
      line += "." + d.field;
    line += ": " + d.message;
    out.push_back( std::move( line ) );
  }
  return out;
}

} // namespace sicnu::recipes
