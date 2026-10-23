// Link stubs for the R20-05 disc_driver ONLY: discovery() touches neither
// publication nor digests; these satisfy the two externals batch_assessment
// pulls in so the driver can exercise the filesystem accounting path.
#include "platform/durable_sidecar.h"
#include <QJsonObject>
namespace sicnu::platform::sidecar
{
WriteResult write( const WriteRequest & ) { return {}; }
}
namespace sicnu::data
{
QByteArray canonicalizeJsonRfc8785( const QJsonObject & )
{
    return QByteArray();
}
} // namespace sicnu::data
