/***************************************************************************
 * src/data/offline_mode.cpp — Qt-flavored offline gate facade (goal D7)
 *
 * The single source of truth for the flag state lives in the Qt-free
 * geospatial layer (geospatial/remote/offline_gate.{h,cpp}) where httpFetch
 * enforces it; this facade keeps the sicnu_data/CLI call sites readable.
 ***************************************************************************/
#include "offline_mode.h"

#include "geospatial/remote/offline_gate.h"

namespace sicnu::data::offline {

void setEnabled( bool offline )
{
    sicnu::geo::offline::setEnabled( offline );
}

bool enabled()
{
    return sicnu::geo::offline::enabled();
}

bool enabledFromEnv()
{
    return sicnu::geo::offline::enabledFromEnv();
}

bool isRemoteTarget( const QString &source )
{
    return sicnu::geo::offline::isRemoteTarget( source.toStdString() );
}

QString refusalMessage( const QString &source )
{
    return QString::fromStdString(
      sicnu::geo::offline::refusalMessage( source.toStdString() ) );
}

void applyGdalNetworkDeny()
{
    sicnu::geo::offline::applyGdalNetworkDeny();
}

void clearGdalNetworkDeny()
{
    sicnu::geo::offline::clearGdalNetworkDeny();
}

} // namespace sicnu::data::offline
