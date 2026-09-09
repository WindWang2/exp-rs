// src/agent/harness/harness_error.cpp
#include "harness_error.h"

#include <utility>

namespace sicnu::agent::harness {

namespace {

struct CodeInfo {
  const char *category;
  RetryClass retryClass;
};

/// The authoritative code table. A code must be added here before producers
/// may emit it — `isKnownErrorCode` enforces the closed vocabulary.
const CodeInfo *codeInfo( const std::string &code )
{
  static const struct Entry {
    const char *code;
    CodeInfo info;
  } kEntries[] = {
    // Mission-mandated codes.
    { "DATASET_NOT_FOUND",       { "validation", RetryClass::None } },
    { "BAND_ROLE_UNRESOLVED",    { "validation", RetryClass::None } },
    { "CRS_MISMATCH",            { "validation", RetryClass::None } },
    { "GRID_MISMATCH",           { "validation", RetryClass::None } },
    { "INVALID_RADIOMETRY",      { "validation", RetryClass::None } },
    { "INSUFFICIENT_MEMORY",     { "environment", RetryClass::Manual } },
    { "MODEL_INCOMPATIBLE",      { "validation", RetryClass::None } },
    { "MODEL_NOT_READY",         { "environment", RetryClass::Manual } },
    { "EXECUTION_FAILED",        { "runtime", RetryClass::Manual } },
    { "CANCELLED",               { "runtime", RetryClass::None } },
    { "OUTPUT_INVALID",          { "runtime", RetryClass::None } },
    { "MAP_PREFLIGHT_FAILED",    { "validation", RetryClass::None } },
    // Harness-internal codes (same style, same table).
    { "PREFLIGHT_BLOCKED",       { "validation", RetryClass::None } },
    { "ENTITY_AMBIGUOUS",        { "validation", RetryClass::None } },
    { "INTENT_AMBIGUOUS",        { "validation", RetryClass::None } },
    { "INVALID_PLAN",            { "validation", RetryClass::None } },
    { "INVALID_PARAMETER",       { "validation", RetryClass::None } },
    { "TRANSIENT_FAILURE",       { "io", RetryClass::Transient } },
    { "IO_ERROR",                { "io", RetryClass::Transient } },
    { "PATH_OUTSIDE_WORKSPACE",  { "validation", RetryClass::None } },
    { "WORKFLOW_NOT_FOUND",      { "validation", RetryClass::None } },
    { "TOOL_NOT_FOUND",          { "validation", RetryClass::None } },
    { "TIME_ORDER_INVALID",      { "validation", RetryClass::None } },
    { "MODALITY_MISMATCH",       { "validation", RetryClass::None } },
    { "POLARIZATION_MISMATCH",   { "validation", RetryClass::None } },
    { "CALIBRATION_MISMATCH",    { "validation", RetryClass::None } },
    { "TRAINING_INVALID",        { "validation", RetryClass::None } },
    { "NOT_SUPPORTED",           { "runtime", RetryClass::None } },
  };
  static const Entry *kBegin = kEntries;
  static const Entry *kEnd = kEntries + sizeof( kEntries ) / sizeof( kEntries[0] );
  for ( const Entry *it = kBegin; it != kEnd; ++it )
  {
    if ( code == it->code )
      return &it->info;
  }
  return nullptr;
}

} // namespace

std::string errorCategoryForCode( const std::string &code )
{
  if ( const CodeInfo *info = codeInfo( code ) )
    return info->category;
  return "runtime";
}

RetryClass retryClassForCode( const std::string &code )
{
  if ( const CodeInfo *info = codeInfo( code ) )
    return info->retryClass;
  return RetryClass::Manual;
}

const char *retryClassToString( RetryClass retryClass )
{
  switch ( retryClass )
  {
    case RetryClass::None:
      return "none";
    case RetryClass::Manual:
      return "manual";
    case RetryClass::Transient:
      return "transient";
  }
  return "manual";
}

bool isKnownErrorCode( const std::string &code )
{
  return codeInfo( code ) != nullptr;
}

Json::Value suggestedAction( const std::string &action, Json::Value arguments )
{
  Json::Value a( Json::objectValue );
  a["action"] = action;
  a["arguments"] = arguments.isNull() ? Json::Value( Json::objectValue ) : std::move( arguments );
  return a;
}

Json::Value HarnessError::toJson() const
{
  Json::Value v( Json::objectValue );
  v["code"] = code;
  v["summary"] = summary;
  v["details"] = details.isNull() ? Json::Value( Json::objectValue ) : details;
  v["recoverable"] = recoverable;
  v["suggested_actions"] = suggestedActions.isNull() ? Json::Value( Json::arrayValue )
                                                     : suggestedActions;
  v["category"] = errorCategoryForCode( code );
  v["retry_class"] = retryClassToString( retryClassForCode( code ) );
  return v;
}

HarnessError HarnessError::make( const std::string &code, const std::string &summary )
{
  HarnessError e;
  e.code = code;
  e.summary = summary;
  return e;
}

HarnessError HarnessError::make( const std::string &code, const std::string &summary,
                                 Json::Value details )
{
  HarnessError e = make( code, summary );
  e.details = std::move( details );
  return e;
}

HarnessError HarnessError::make( const std::string &code, const std::string &summary,
                                 Json::Value details, bool recoverable,
                                 Json::Value suggestedActions )
{
  HarnessError e = make( code, summary );
  e.details = std::move( details );
  e.recoverable = recoverable;
  e.suggestedActions = std::move( suggestedActions );
  return e;
}

HarnessError HarnessError::makeWithAction( const std::string &code, const std::string &summary,
                                           const std::string &action, Json::Value arguments )
{
  Json::Value actions( Json::arrayValue );
  actions.append( suggestedAction( action, std::move( arguments ) ) );
  return make( code, summary, Json::Value( Json::objectValue ), true, std::move( actions ) );
}

Json::Value errorEnvelope( const HarnessError &error )
{
  Json::Value v( Json::objectValue );
  v["success"] = false;
  Json::Value err( error.toJson() );
  err["message"] = error.summary;
  err["retryable"] = retryClassForCode( error.code ) == RetryClass::Transient;
  v["error"] = std::move( err );
  return v;
}

HarnessError normalizeLegacyError( const std::string &legacyCode, const std::string &message )
{
  if ( isKnownErrorCode( legacyCode ) )
    return HarnessError::make( legacyCode, message );

  // Historical codes from the spatial-tool and MCP surfaces (ADR 0122/0128).
  if ( legacyCode == "NOT_FOUND" || legacyCode == "FILE_NOT_FOUND" )
    return HarnessError::make( error_codes::kDatasetNotFound, message );
  if ( legacyCode == "PATH_OUTSIDE_WORKSPACE" )
    return HarnessError::make( error_codes::kPathOutsideWorkspace, message );
  if ( legacyCode == "MODEL_NOT_FOUND" )
    return HarnessError::make( error_codes::kModelNotReady, message );
  if ( legacyCode == "DATA_IO" || legacyCode == "IO" )
    return HarnessError::make( error_codes::kTransientFailure, message );
  if ( legacyCode == "INVALID_PARAMETER" || legacyCode == "VALIDATION" )
    return HarnessError::make( error_codes::kInvalidParameter, message );
  if ( legacyCode == "CANCELLED" || legacyCode == "CANCELED" )
    return HarnessError::make( error_codes::kCancelled, message );
  if ( legacyCode == "INSUFFICIENT_MEMORY" || legacyCode == "RAM" )
    return HarnessError::make( error_codes::kInsufficientMemory, message );

  // Unmapped codes still surface with a stable outer shape; the original
  // string is preserved in details so nothing is lost.
  Json::Value details( Json::objectValue );
  details["legacy_code"] = legacyCode;
  return HarnessError::make( error_codes::kExecutionFailed, message, details );
}

} // namespace sicnu::agent::harness
