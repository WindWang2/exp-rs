// admin_errors.h — typed error codes for teaching_admin orchestration.
#pragma once

#include <QString>

namespace sicnu::teaching_admin {

inline QString adminError( const QString &code, const QString &detail = QString() )
{
    if ( detail.isEmpty() )
        return code;
    return code + QStringLiteral( ": " ) + detail;
}

} // namespace sicnu::teaching_admin
