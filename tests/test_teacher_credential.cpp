// Pins the teacher credential gate (SICNU_LAB_TEACHER_TOKEN) in BOTH
// directions. Regression guard for the inverted constant-time comparison
// introduced in #1202 (`return diff;` instead of `return diff == 0;`), which
// accepted any wrong same-length token and rejected the correct one.
//
// Deliberately tiny (Catch2 + sicnu_agent + jsoncpp only) so it keeps
// building and running even when the heavier harness suites do not link.

#include "agent/harness/lab_copilot.h"

#include <QByteArray>
#include <QtGlobal>

#include <json/json.h>

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

using sicnu::agent::harness::teacherCredentialValid;

namespace
{

constexpr const char *kTokenVar = "SICNU_LAB_TEACHER_TOKEN";

/// Sets (or unsets) the host token for one scope and restores the previous
/// value on exit.
class ScopedTeacherToken
{
  public:
    explicit ScopedTeacherToken( const std::optional<std::string> &value )
    {
      if ( qEnvironmentVariableIsSet( kTokenVar ) )
        m_previous = qgetenv( kTokenVar ).toStdString();
      if ( value )
        qputenv( kTokenVar, QByteArray::fromStdString( *value ) );
      else
        qunsetenv( kTokenVar );
    }
    ~ScopedTeacherToken()
    {
      if ( m_previous )
        qputenv( kTokenVar, QByteArray::fromStdString( *m_previous ) );
      else
        qunsetenv( kTokenVar );
    }
    ScopedTeacherToken( const ScopedTeacherToken & ) = delete;
    ScopedTeacherToken &operator=( const ScopedTeacherToken & ) = delete;

  private:
    std::optional<std::string> m_previous;
};

Json::Value withToken( const Json::Value &token )
{
  Json::Value input( Json::objectValue );
  input["teacher_token"] = token;
  return input;
}

} // namespace

TEST_CASE( "teacher credential: the exact host token is accepted", "[teacher_credential][security]" )
{
  ScopedTeacherToken env( std::string( "host-secret-123" ) );
  REQUIRE( teacherCredentialValid( withToken( "host-secret-123" ) ) );
}

TEST_CASE( "teacher credential: a wrong token of the SAME length is rejected",
           "[teacher_credential][security]" )
{
  const std::string secret = "host-secret-123";
  ScopedTeacherToken env( secret );

  // Completely different content, identical length.
  REQUIRE_FALSE( teacherCredentialValid( withToken( std::string( secret.size(), 'x' ) ) ) );

  // Single-byte differences at every position (first, middle, last, ...).
  for ( size_t i = 0; i < secret.size(); ++i )
  {
    std::string wrong = secret;
    wrong[i] = static_cast<char>( wrong[i] ^ 0x01 );
    INFO( "flipped byte index " << i );
    REQUIRE( wrong.size() == secret.size() );
    REQUIRE_FALSE( teacherCredentialValid( withToken( wrong ) ) );
  }
}

TEST_CASE( "teacher credential: a token of a DIFFERENT length is rejected",
           "[teacher_credential][security]" )
{
  const std::string secret = "host-secret-123";
  ScopedTeacherToken env( secret );

  REQUIRE_FALSE( teacherCredentialValid( withToken( "teacher" ) ) );
  REQUIRE_FALSE( teacherCredentialValid( withToken( secret.substr( 0, secret.size() - 1 ) ) ) );
  REQUIRE_FALSE( teacherCredentialValid( withToken( secret + "4" ) ) );
  REQUIRE_FALSE( teacherCredentialValid( withToken( secret + secret ) ) );
}

TEST_CASE( "teacher credential: missing, empty or non-string tokens are rejected",
           "[teacher_credential][security]" )
{
  ScopedTeacherToken env( std::string( "host-secret-123" ) );

  REQUIRE_FALSE( teacherCredentialValid( Json::Value( Json::objectValue ) ) );
  REQUIRE_FALSE( teacherCredentialValid( withToken( "" ) ) );
  REQUIRE_FALSE( teacherCredentialValid( withToken( 123 ) ) );
  REQUIRE_FALSE( teacherCredentialValid( withToken( Json::Value( Json::nullValue ) ) ) );
}

TEST_CASE( "teacher credential: the surface is fail-closed when the host token is unset or empty",
           "[teacher_credential][security]" )
{
  {
    ScopedTeacherToken env( std::nullopt );
    REQUIRE_FALSE( teacherCredentialValid( withToken( "host-secret-123" ) ) );
    REQUIRE_FALSE( teacherCredentialValid( withToken( "" ) ) );
  }
  {
    ScopedTeacherToken env( std::string() );
    REQUIRE_FALSE( teacherCredentialValid( withToken( "host-secret-123" ) ) );
    REQUIRE_FALSE( teacherCredentialValid( withToken( "" ) ) );
  }
}
