// src/agentbench/failure_taxonomy.cpp
#include "failure_taxonomy.h"

namespace sicnu::agentbench
{

std::vector<std::string> allFailureClasses()
{
	using namespace failure_classes;
	return {
		kNone,
		kNotStarted,
		kIncomplete,
		kScopeViolation,
		kBudgetExhausted,
		kInvalidScience,
		kVerificationFailed,
		kSilentFailure,
		kRecoveryFailed,
		kClaimMismatch,
		kImpossibleTask,
	};
}

bool isValidFailureClass( const std::string &failureClass )
{
	for ( const std::string &candidate : allFailureClasses() )
	{
		if ( candidate == failureClass )
			return true;
	}
	return false;
}

} // namespace sicnu::agentbench
