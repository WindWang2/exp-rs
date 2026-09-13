// tests/support/offline_probe.h — offline-degradation guard for
// transport-dependent test binaries (D10 verification-baseline-green,
// ADR 0146).
//
// Contract: a test binary that needs a transport (loopback TCP fixtures,
// /vsicurl/ reads) must NEVER die by wall-clock timeout when that transport
// is missing from the environment. `timeout` is never an acceptable steady
// state; the binary must report a machine-readable SKIP instead:
//
//     stdout: sicnu-skip: <reason-code>
//     exit:   77 (GNU "skipped test" convention)
//
// The verification ladder maps this to the distinct `skipped` verdict —
// never `passed`, never `timeout` (ADR 0146; the READINESS vocabulary is
// unchanged: not-built / skipped / timeout / failed / passed stay distinct).
//
// Before any test runs the guard:
//   1. honours SICNU_FORCE_OFFLINE=1 — forced skip; this is how the skip
//      path itself is tested on healthy machines;
//   2. applies proxy hygiene — loopback fixtures never need a proxy, so
//      127.0.0.1/localhost are appended to no_proxy/NO_PROXY. This FIXES the
//      "machine-room proxy env poisons every loopback /vsicurl/ read" class
//      instead of skipping it;
//   3. probes loopback TCP with bounded timeouts (bind → listen → connect →
//      accept → close, all under poll() deadlines). Every stage has a hard
//      bound: the guard itself can never be the hang it exists to prevent.
//
// Frozen reason codes (ADR 0146; extend, never reword):
//   forced-offline         — SICNU_FORCE_OFFLINE=1 in the environment
//   loopback-unavailable   — the bounded loopback TCP probe failed
#ifndef SICNU_TESTS_SUPPORT_OFFLINE_PROBE_H
#define SICNU_TESTS_SUPPORT_OFFLINE_PROBE_H

#include <catch2/interfaces/catch_interfaces_reporter.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace sicnu::testsupport::offline
{

inline bool forcedOffline()
{
  const char *value = std::getenv( "SICNU_FORCE_OFFLINE" );
  return value && *value && std::strcmp( value, "0" ) != 0;
}

/// True when the current environment exempts loopback from proxies. Used by
/// the suites as a regression guard: if the hygiene pass regresses, this
/// assertion fails instead of a machine room of reads hanging in curl.
inline bool loopbackProxyExempted()
{
  for ( const char *var : { "no_proxy", "NO_PROXY" } )
  {
    const char *current = std::getenv( var );
    if ( !current )
      continue;
    const std::string value = current;
    if ( value.find( "127.0.0.1" ) != std::string::npos ||
         value.find( "localhost" ) != std::string::npos )
      return true;
  }
  return false;
}

/// Platform-agnostic socket/poll shims for the bounded probe. Windows
/// exposes sockets as SOCKET handles and WSAPoll (ws2tcpip.h); POSIX uses
/// int fds and poll().
#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
using PollDesc = WSAPOLLFD;
inline int pollSockets( PollDesc *fds, ULONG count, int timeoutMs )
{
  return WSAPoll( fds, count, timeoutMs );
}
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
using PollDesc = pollfd;
inline int pollSockets( PollDesc *fds, nfds_t count, int timeoutMs )
{
  return ::poll( fds, count, timeoutMs );
}
#endif

/// Loopback fixtures must bypass proxies: libcurl (hence /vsicurl/) honours
/// http_proxy/ALL_PROXY for every host unless no_proxy says otherwise, and a
/// machine-room proxy env turns a 127.0.0.1 fixture into a hang or a typed
/// failure unrelated to the code under test. Appends to no_proxy/NO_PROXY
/// rather than clearing the proxy — explicit traffic keeps its proxy.
inline void ensureLoopbackProxyHygiene()
{
  for ( const char *var : { "no_proxy", "NO_PROXY" } )
  {
    const char *current = std::getenv( var );
    const std::string value = current ? current : "";
    const bool covered = value.find( "127.0.0.1" ) != std::string::npos ||
                         value.find( "localhost" ) != std::string::npos;
    if ( covered )
      continue;
    std::string updated = value;
    if ( !updated.empty() && updated.back() != ',' )
      updated += ',';
    updated += "127.0.0.1,localhost";
#ifdef _WIN32
    _putenv_s( var, updated.c_str() );
#else
    setenv( var, updated.c_str(), 1 );
#endif
  }
}

/// Bounded loopback TCP self-probe. Returns nullptr when a full
/// bind→listen→connect→accept cycle succeeds; otherwise a stable reason
/// fragment. Every syscall path is deadline-bounded via poll().
inline const char *probeLoopbackTcp( int timeoutMs )
{
#ifdef _WIN32
  WSADATA wsa;
  if ( WSAStartup( MAKEWORD( 2, 2 ), &wsa ) != 0 )
    return "loopback-unavailable:winsock-init";
#endif
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
  addr.sin_port = 0;

  auto closeFd = []( SocketHandle fd )
  {
    if ( fd == kInvalidSocket )
      return;
#ifdef _WIN32
    closesocket( fd );
#else
    ::close( fd );
#endif
  };
  auto setNonBlocking = []( SocketHandle fd ) -> bool
  {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket( fd, FIONBIO, &mode ) == 0;
#else
    const int flags = fcntl( fd, F_GETFL, 0 );
    return flags >= 0 && fcntl( fd, F_SETFL, flags | O_NONBLOCK ) == 0;
#endif
  };

  const SocketHandle listener = ::socket( AF_INET, SOCK_STREAM, 0 );
  if ( listener == kInvalidSocket )
    return "loopback-unavailable:socket";
  if ( ::bind( listener, reinterpret_cast<sockaddr *>( &addr ), sizeof( addr ) ) != 0 )
  {
    closeFd( listener );
    return "loopback-unavailable:bind";
  }
  if ( ::listen( listener, 1 ) != 0 )
  {
    closeFd( listener );
    return "loopback-unavailable:listen";
  }

  sockaddr_in bound{};
  socklen_t boundLen = sizeof( bound );
  if ( ::getsockname( listener, reinterpret_cast<sockaddr *>( &bound ), &boundLen ) != 0 )
  {
    closeFd( listener );
    return "loopback-unavailable:getsockname";
  }

  const SocketHandle client = ::socket( AF_INET, SOCK_STREAM, 0 );
  if ( client == kInvalidSocket )
  {
    closeFd( listener );
    return "loopback-unavailable:socket";
  }
  if ( !setNonBlocking( client ) )
  {
    closeFd( client );
    closeFd( listener );
    return "loopback-unavailable:nonblock";
  }
  if ( ::connect( client, reinterpret_cast<sockaddr *>( &bound ), sizeof( bound ) ) != 0 )
  {
    PollDesc pfd{ client, POLLOUT, 0 };
    if ( pollSockets( &pfd, 1, timeoutMs ) <= 0 )
    {
      closeFd( client );
      closeFd( listener );
      return "loopback-unavailable:connect-timeout";
    }
    int soError = 0;
    socklen_t soLen = sizeof( soError );
    ::getsockopt( client, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>( &soError ), &soLen );
    if ( soError != 0 )
    {
      closeFd( client );
      closeFd( listener );
      return "loopback-unavailable:connect";
    }
  }

  PollDesc lfd{ listener, POLLIN, 0 };
  if ( pollSockets( &lfd, 1, timeoutMs ) <= 0 )
  {
    closeFd( client );
    closeFd( listener );
    return "loopback-unavailable:accept-timeout";
  }
  const SocketHandle accepted = ::accept( listener, nullptr, nullptr );
  if ( accepted == kInvalidSocket )
  {
    closeFd( client );
    closeFd( listener );
    return "loopback-unavailable:accept";
  }
  closeFd( accepted );
  closeFd( client );
  closeFd( listener );
  return nullptr;
}

/// SKIP emission per the ADR 0146 contract: sentinel line on stdout (the
/// machine-readable channel), detail on stderr, exit 77. _Exit: the guard
/// may fire from inside the Catch2 run where test fixtures own resources;
/// skip semantics must not depend on teardown order.
inline void skipWithReason( const char *reason, const char *detail )
{
  std::fputs( "sicnu-skip: ", stdout );
  std::fputs( reason, stdout );
  std::fputc( '\n', stdout );
  std::fflush( stdout );
  std::fputs( "sicnu-skip-detail: ", stderr );
  std::fputs( detail ? detail : reason, stderr );
  std::fputc( '\n', stderr );
  std::fflush( stderr );
  std::_Exit( 77 );
}

/// Catch2 event listener: probes once when the test run actually starts.
/// Test discovery (--list-tests) never triggers it, so ctest registration
/// keeps working on hosts where the transport is missing.
class OfflineGuardListener : public Catch::EventListenerBase
{
public:
  explicit OfflineGuardListener( Catch::IConfig const *config )
    : Catch::EventListenerBase( config )
  {
  }

  void testRunStarting( Catch::TestRunInfo const &info ) override
  {
    ( void ) info;
    if ( forcedOffline() )
      skipWithReason( "forced-offline", "SICNU_FORCE_OFFLINE is set" );
    ensureLoopbackProxyHygiene();
    if ( const char *failure = probeLoopbackTcp( 2000 ) )
      skipWithReason( "loopback-unavailable", failure );
  }
};

} // namespace sicnu::testsupport::offline

/// Registers the guard with this binary's Catch2 session.
#define SICNU_OFFLINE_GUARD() \
  CATCH_REGISTER_LISTENER( ::sicnu::testsupport::offline::OfflineGuardListener )

#endif // SICNU_TESTS_SUPPORT_OFFLINE_PROBE_H
