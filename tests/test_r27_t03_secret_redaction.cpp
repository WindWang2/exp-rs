// tests/test_r27_t03_secret_redaction.cpp — #1762 (AS-010)
//
// The redactor's key needles were underscore/word forms only: "api-key" and
// "x-api-key" (the header spellings real tools emit) slipped through, and
// secret-shaped VALUES (a Bearer token pasted into prose, a private-key
// block) were never pattern-scanned. Keys are separator-normalized now and
// values carry prefix-anchored secret patterns — while science evidence
// (16-hex findings digests, ordinary prose) must survive untouched.
#include <catch2/catch_test_macros.hpp>

#include "agent_ops/secret_redactor.h"

using namespace sicnu::agent_ops;

TEST_CASE( "hyphen key spellings are redacted (#1762)", "[r27t03][redaction]" )
{
    CHECK( isSecretKey( "api-key" ) );
    CHECK( isSecretKey( "x-api-key" ) );
    CHECK( isSecretKey( "X-Api-Key" ) );
    CHECK( isSecretKey( "auth-token" ) );
    CHECK( isSecretKey( "client-secret" ) );
    CHECK( isSecretKey( "refresh-token" ) );
    CHECK( isSecretKey( "api_key" ) ); // legacy form keeps working
    CHECK( isSecretKey( "password" ) );

    CHECK_FALSE( isSecretKey( "api_version" ) );
    CHECK_FALSE( isSecretKey( "band_ratio" ) );
    // "tokenize" contains "token" — the legacy substring behavior redacted
    // it too; substring semantics are unchanged in this fix (false
    // positives fail safe).
    CHECK( isSecretKey( "tokenize" ) );
}

TEST_CASE( "secret-shaped values are redacted under innocent keys (#1762)",
           "[r27t03][redaction]" )
{
    Json::Value doc( Json::objectValue );
    doc["note_header"] = "Bearer eyJhbGciOiJIUzI1NiJ9.payload.sig";
    doc["pasted_key"] = "ghp_0123456789abcdefghijklmnopqrstuvwxyz";
    doc["openai_style"] = "sk-proj-0123456789abcdef0123456789abcdef";
    doc["ssh_block"] = "-----BEGIN OPENSSH PRIVATE KEY-----\nb3BlbnNzaC1rZXktdjEAAAAA";
    doc["basic_auth"] = "Basic dXNlcjpwYXNzd29yZA==";

    const Json::Value out = redactAndBound( doc, 100000 );
    CHECK( out["note_header"].asString() == "[REDACTED]" );
    CHECK( out["pasted_key"].asString() == "[REDACTED]" );
    CHECK( out["openai_style"].asString() == "[REDACTED]" );
    CHECK( out["ssh_block"].asString() == "[REDACTED]" );
    CHECK( out["basic_auth"].asString() == "[REDACTED]" );
}

TEST_CASE( "science evidence survives value redaction (#1762 control)",
           "[r27t03][redaction]" )
{
    Json::Value doc( Json::objectValue );
    doc["findings_digest"] = "a1b2c3d4e5f60718"; // 16-hex evidence digest
    doc["plan_summary"] = "reproject to UTM zone 50S at 30 m";
    doc["ratio"] = "0.42 (NIR/RED)";
    doc["long_prose"] = "The operator asked for /vsicurl-style remote access "
                        "but the policy denied it; see the audit trail.";

    const Json::Value out = redactAndBound( doc, 100000 );
    CHECK( out["findings_digest"].asString() == "a1b2c3d4e5f60718" );
    CHECK( out["plan_summary"].asString() == doc["plan_summary"].asString() );
    CHECK( out["ratio"].asString() == doc["ratio"].asString() );
    CHECK( out["long_prose"].asString() == doc["long_prose"].asString() );
}
