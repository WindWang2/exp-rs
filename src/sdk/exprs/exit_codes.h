/***************************************************************************
 * exprs/exit_codes.h — machine-readable CLI exit code contract
 *
 * The headless CLI (sicnu_geo_rs_cli) is a scripting/Agent surface: its
 * exit codes are a public contract. Legacy behaviour (0 = success,
 * 1 = generic failure) is preserved; the codes below extend it.
 ***************************************************************************/
#pragma once

namespace exprs {

enum class ExitCode
{
    Ok = 0,                 ///< success
    GenericError = 1,       ///< unclassified failure (legacy value preserved)
    ValidationFailure = 2,  ///< input failed schema/contract validation
    ExecutionFailure = 3,   ///< an operator/workflow step failed at run time
    Cancelled = 4,          ///< the operation was cancelled by the user/signal
    MissingDependency = 5,  ///< a required plugin/model/tool is unavailable
    InvalidInput = 6,       ///< malformed arguments or unreadable input files
    RuntimeUnavailable = 7, ///< the execution runtime failed to initialize
};

inline int exitCodeValue( ExitCode code ) { return static_cast<int>( code ); }

} // namespace exprs
