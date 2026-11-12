#include <QCoreApplication>
#include <QtCrypto>
#include <qt6keychain/keychain.h>

// CI only compiles and links this probe. Constructing both dependency
// types keeps their libraries in the link; no password job is started.
int main( int argc, char **argv )
{
    QCoreApplication app( argc, argv );
    QCA::Initializer crypto;
    QKeychain::ReadPasswordJob job( QStringLiteral( "sicnu-ci-abi" ) );
    return 0;
}
