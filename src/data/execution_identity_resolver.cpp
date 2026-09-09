// execution_identity_resolver.cpp — see execution_identity_resolver.h.
#include "execution_identity_resolver.h"

namespace sicnu::data
{
namespace
{
InputIdentityResolver g_resolver;
}

InputIdentityResolver *executionIdentityResolver()
{
    return g_resolver ? &g_resolver : nullptr;
}

InputIdentityResolver *setExecutionIdentityResolver( InputIdentityResolver resolver )
{
    g_resolver = std::move( resolver );
    return g_resolver ? &g_resolver : nullptr;
}

} // namespace sicnu::data
