// helper_teaching_fake_grader_cli.cpp — test double for the REAL grading CLI
// exit/transcript contract (sicnu_geo_rs_cli lab --grade). Same shape, no Qt,
// no GDAL: {schema:"sicnu.lab.grade/1", digest, generated_utc, report} written
// to --out AND stdout, exit 0 pass / 1 fail / 2 usage / 3 unverifiable.
// Behavior keyed on artifact bytes: GOOD → pass, BAD → fail, UNVERIFIABLE →
// exit 3; lab id "usage_lab" (or missing artifact) → exit 2.
//
// DETERMINISM ORACLE: this helper must stay byte-aligned with the transcript
// writer in src/agent/output_verifier.cpp (LabGradeResult::toJson). If the
// real contract changes, update BOTH — test_teaching_admin_core fails on
// schema/digest drift via grader_transcript_invalid / exit-verdict
// cross-checks, which is the signal to re-sync.
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

namespace {

// 64 hex chars, fixed: tests assert shape, not value.
constexpr const char *kFakeDigest = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

std::string transcriptBody( const std::string &lab, const std::string &artifact,
                            const std::string &verdict, double score, const std::string &error,
                            const std::string &deductionJson )
{
    std::string body;
    body += "{";
    body += "\"lab_id\":\"" + lab + "\",";
    body += "\"artifact\":\"" + artifact + "\",";
    body += "\"rules\":\"<fake>\",";
    body += "\"verdict\":\"" + verdict + "\",";
    body += "\"score\":" + std::to_string( score ) + ",";
    body += "\"passing_score\":60.0,";
    body += "\"capped_by_blocking\":false,";
    body += "\"deductions\":" + ( deductionJson.empty() ? std::string( "[]" ) : deductionJson ) + ",";
    body += "\"evidence\":[],";
    body += "\"summary\":{}";
    if ( !error.empty() )
        body += ",\"error\":\"" + error + "\"";
    body += "}";
    return body;
}

void emit( const std::string &outPath, const std::string &document )
{
    std::cout << document << "\n";
    std::cout.flush();
    if ( !outPath.empty() )
    {
        std::ofstream out( outPath.c_str(), std::ios::binary | std::ios::trunc );
        out << document;
    }
}

std::string slurp( const std::string &path )
{
    std::ifstream in( path.c_str(), std::ios::binary );
    if ( !in )
        return std::string();
    return std::string( ( std::istreambuf_iterator<char>( in ) ), std::istreambuf_iterator<char>() );
}

std::string jsonEscape( const std::string &s )
{
    std::string out;
    for ( const char c : s )
    {
        if ( c == '"' || c == '\\' )
        {
            out += '\\';
            out += c;
        }
        else if ( static_cast<unsigned char>( c ) < 0x20 )
        {
            char buf[8];
            std::snprintf( buf, sizeof( buf ), "\\u%04x", static_cast<unsigned>( c ) );
            out += buf;
        }
        else
        {
            out += c;
        }
    }
    return out;
}

} // namespace

int main( int argc, char **argv )
{
    std::string lab, artifact, outPath;
    for ( int i = 1; i < argc; ++i )
    {
        const std::string arg = argv[i];
        if ( arg == "--lab" && i + 1 < argc )
            lab = argv[++i];
        else if ( arg == "--grade" && i + 1 < argc )
            artifact = argv[++i];
        else if ( arg == "--out" && i + 1 < argc )
            outPath = argv[++i];
    }

    if ( lab.empty() || artifact.empty() )
    {
        std::cerr << "lab: --lab and --grade are required\n";
        return 2;
    }

    // usage: unknown lab id
    if ( lab == "usage_lab" )
    {
        std::cerr << "lab: unknown lab: usage_lab\n";
        const std::string document =
          std::string( "{\"schema\":\"sicnu.lab.grade/1\",\"digest\":\"" ) + kFakeDigest
          + "\",\"generated_utc\":\"2026-01-01T00:00:00Z\",\"report\":"
          + transcriptBody( lab, artifact, "unverifiable", 0.0, "unknown lab: usage_lab", "" ) + "}";
        emit( outPath, document );
        return 2;
    }

    // broken authority: exit says pass, transcript says fail — the adapter
    // must refuse this (grader_exit_verdict_mismatch), never trust either.
    if ( lab == "mismatch_lab" )
    {
        const std::string document =
          std::string( "{\"schema\":\"sicnu.lab.grade/1\",\"digest\":\"" ) + kFakeDigest
          + "\",\"generated_utc\":\"2026-01-01T00:00:00Z\",\"report\":"
          + transcriptBody( lab, artifact, "fail", 10.0, "", "" ) + "}";
        emit( outPath, document );
        return 0;
    }

    const std::string bytes = slurp( artifact );
    if ( bytes.empty() )
    {
        // missing/unreadable artifact → usage
        std::cerr << "lab: artifact missing or unreadable: " << artifact << "\n";
        const std::string document =
          std::string( "{\"schema\":\"sicnu.lab.grade/1\",\"digest\":\"" ) + kFakeDigest
          + "\",\"generated_utc\":\"2026-01-01T00:00:00Z\",\"report\":"
          + transcriptBody( lab, artifact, "unverifiable", 0.0,
                            "artifact missing or unreadable: " + jsonEscape( artifact ), "" )
          + "}";
        emit( outPath, document );
        return 2;
    }

    if ( bytes.find( "UNVERIFIABLE" ) != std::string::npos )
    {
        const std::string document =
          std::string( "{\"schema\":\"sicnu.lab.grade/1\",\"digest\":\"" ) + kFakeDigest
          + "\",\"generated_utc\":\"2026-01-01T00:00:00Z\",\"report\":"
          + transcriptBody( lab, artifact, "unverifiable", 0.0, "cannot open as raster", "" ) + "}";
        emit( outPath, document );
        return 3;
    }

    if ( bytes.find( "CRASH" ) != std::string::npos )
    {
        // Regression lane for the crash contract: emit a *complete valid*
        // transcript, then die abnormally. A runScript that ignores
        // exitStatus would read this as a normal exit — the adapter must
        // answer unavailable/grader_crashed instead.
        const std::string document =
          std::string( "{\"schema\":\"sicnu.lab.grade/1\",\"digest\":\"" ) + kFakeDigest
          + "\",\"generated_utc\":\"2026-01-01T00:00:00Z\",\"report\":"
          + transcriptBody( lab, artifact, "pass", 99.0, "", "" ) + "}";
        emit( outPath, document );
        std:: fflush( stdout );
        std:: raise( SIGSEGV );
        return 99; // unreachable
    }

    if ( bytes.find( "BAD" ) != std::string::npos )
    {
        // Two deductions: the top one must be chosen by MAX WEIGHT (a2, 60),
        // not by array order (a1 first) — locks the lab_batch_runner parity.
        const std::string deduction =
          std::string( "[{\"assertion_id\":\"a1\",\"kind\":\"range\",\"severity\":\"normal\","
                       "\"weight\":10.0,\"observed\":\"40\",\"expected\":\">= 60\",\"delta\":null,"
                       "\"message\":\"low weight fail\"},"
                       "{\"assertion_id\":\"a2\",\"kind\":\"mean_sigma\",\"severity\":\"normal\","
                       "\"weight\":60.0,\"observed\":\"40\",\"expected\":\">= 60\",\"delta\":null,"
                       "\"message\":\"below pass line\"}]" );
        const std::string document =
          std::string( "{\"schema\":\"sicnu.lab.grade/1\",\"digest\":\"" ) + kFakeDigest
          + "\",\"generated_utc\":\"2026-01-01T00:00:00Z\",\"report\":"
          + transcriptBody( lab, artifact, "fail", 40.0, "", deduction ) + "}";
        emit( outPath, document );
        return 1;
    }

    // default (incl. GOOD) → pass
    const std::string document =
      std::string( "{\"schema\":\"sicnu.lab.grade/1\",\"digest\":\"" ) + kFakeDigest
      + "\",\"generated_utc\":\"2026-01-01T00:00:00Z\",\"report\":"
      + transcriptBody( lab, artifact, "pass", 88.5, "", "" ) + "}";
    emit( outPath, document );
    return 0;
}
