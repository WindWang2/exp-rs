/***************************************************************************
  geospatial/common.h
  Geospatial I/O Foundation 4.0 — shared error vocabulary for the Qt-free core.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_COMMON_H
#define SICNU_GEOSPATIAL_COMMON_H

#include <json/json.h>

#include <stdexcept>
#include <string>
#include <utility>

namespace sicnu::geo
{

/// Structured error codes for the geospatial I/O foundation layer.
///
/// The layer never silently guesses or silently drops: every failure mode that
/// matters to data fidelity maps to one of these codes and travels with a
/// human-readable message plus optional structured details.
enum class ErrorCode
{
  InvalidArgument,   ///< caller contract violation (bad window, empty path, ...)
  OpenFailed,        ///< dataset could not be opened (missing, unreadable, malformed)
  DriverMissing,     ///< required GDAL driver is not available in this build
  MissingCrs,        ///< dataset carries no CRS and no explicit fallback was given
  InvalidCrs,        ///< CRS present but unresolvable / malformed
  TransformFailed,   ///< coordinate transformation refused or failed
  WriteFailed,       ///< writing pixels/geometry/metadata failed
  FidelityLoss,      ///< requested conversion would silently lose information
  Unsupported,       ///< operation not supported by driver/profile/dataset
  Cancelled,         ///< caller-requested cancellation
  IoError,           ///< filesystem/OS level failure (permissions, disk full, ...)
};

/// Error with structured code + message + optional JSON details.
///
/// The operator layer translates this into `RSOperatorError`; the CLI maps it
/// to stable exit codes; the doctor embeds it into diagnostics.
class GeoError : public std::runtime_error
{
  public:
    GeoError( ErrorCode code, const std::string &message, Json::Value details = Json::Value() )
      : std::runtime_error( message ), mCode( code ), mDetails( std::move( details ) ) {}

    ErrorCode code() const { return mCode; }
    const Json::Value &details() const { return mDetails; }

    Json::Value toJson() const
    {
      Json::Value json;
      json["code"] = errorCodeName( mCode );
      json["message"] = what();
      if ( !mDetails.isNull() )
        json["details"] = mDetails;
      return json;
    }

    static const char *errorCodeName( ErrorCode code )
    {
      switch ( code )
      {
        case ErrorCode::InvalidArgument: return "invalid_argument";
        case ErrorCode::OpenFailed: return "open_failed";
        case ErrorCode::DriverMissing: return "driver_missing";
        case ErrorCode::MissingCrs: return "missing_crs";
        case ErrorCode::InvalidCrs: return "invalid_crs";
        case ErrorCode::TransformFailed: return "transform_failed";
        case ErrorCode::WriteFailed: return "write_failed";
        case ErrorCode::FidelityLoss: return "fidelity_loss";
        case ErrorCode::Unsupported: return "unsupported";
        case ErrorCode::Cancelled: return "cancelled";
        case ErrorCode::IoError: return "io_error";
      }
      return "unknown";
    }

  private:
    ErrorCode mCode = ErrorCode::InvalidArgument;
    Json::Value mDetails;
};

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_COMMON_H
