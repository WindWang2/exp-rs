#include "seasonality.h"

namespace sicnu::suitability
{

QString seasonForMonth( int month1to12 )
{
    switch ( month1to12 )
    {
        case 12:
        case 1:
        case 2:
            return QStringLiteral( "winter" );
        case 3:
        case 4:
        case 5:
            return QStringLiteral( "spring" );
        case 6:
        case 7:
        case 8:
            return QStringLiteral( "summer" );
        case 9:
        case 10:
        case 11:
            return QStringLiteral( "autumn" );
        default:
            return QString();
    }
}

} // namespace sicnu::suitability
