// registration_types.cpp — F13 shared contracts.
#include "registration_types.h"

namespace sicnu::registration {

QString statusToString(RegistrationStatus status)
{
    switch (status) {
    case RegistrationStatus::Success:
        return QStringLiteral("success");
    case RegistrationStatus::LowConfidence:
        return QStringLiteral("low_confidence");
    case RegistrationStatus::Refused:
        return QStringLiteral("refused");
    case RegistrationStatus::Failed:
        return QStringLiteral("failed");
    }
    return QStringLiteral("unknown");
}

} // namespace sicnu::registration
