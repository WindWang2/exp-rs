// generate_sample_data.cpp — CLI for the lab sample data foundry (goal D1).
//
// sicnu_generate_samples --out=<dir> [--profile=lab|stress] [--seed=<n>]
//                         [--spec=<file-or-dir>] [--verify] [--help]
//
// Exit codes (typed, part of the contract in docs/adr/0164):
//   0  success (or verify passed)
//   1  generation / I/O / GDAL failure
//   2  usage error
//   3  spec refusal (unknown product, missing/invalid spec — never silent)
//   4  verify drift (missing/modified files, manifest mismatch)
//
// Deterministic: same host + same GDAL build + same seed + same profile =>
// byte-identical outputs (see tools/sample_foundry.h for the contract).

#include "sample_foundry.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{

using namespace sicnu::foundry;

void printUsage( std::FILE *out )
{
  std::fprintf( out,
    "%s %s — deterministic lab sample data foundry (docs/labs)\n\n"
    "usage:\n"
    "  sicnu_generate_samples --out=<dir> [--profile=lab|stress] [--seed=<n>]\n"
    "                         [--spec=<file-or-dir>] [--verify] [--help]\n\n"
    "options:\n"
    "  --out=<dir>       output directory (default: data/samples relative to cwd)\n"
    "  --profile=<name>  lab (256x256, default) | stress (2048x2048)\n"
    "  --seed=<n>        32-bit PRNG seed (default: 42)\n"
    "  --spec=<path>     JSON spec (or directory of *.json) declaring the products\n"
    "                    to generate; see docs/adr/0164. Without it: full set.\n"
    "  --verify          re-check an existing directory against its manifest.json\n"
    "  --help            this text\n\n"
    "products (catalog ids for --spec):\n",
    kGeneratorName, kGeneratorVersion );
  for ( Product p : productCatalog() )
    std::fprintf( out, "  %s\n", productName( p ) );
  std::fprintf( out, "\nexit codes: 0 ok | 1 generation/io failure | 2 usage | "
                     "3 spec refusal | 4 verify drift\n" );
}

bool startsWith( const std::string &text, const char *prefix )
{
  const std::size_t n = std::strlen( prefix );
  return text.size() >= n && text.compare( 0, n, prefix ) == 0;
}

/// Strict decimal uint32 parse (no sign, no whitespace, no overflow).
bool parseSeed( const std::string &text, uint32_t *out )
{
  if ( text.empty() || text.size() > 10 )
    return false;
  uint64_t value = 0;
  for ( char c : text )
  {
    if ( c < '0' || c > '9' )
      return false;
    value = value * 10 + static_cast<uint64_t>( c - '0' );
  }
  if ( value > 0xFFFFFFFFull )
    return false;
  *out = static_cast<uint32_t>( value );
  return true;
}

int usageError( const std::string &message )
{
  std::fprintf( stderr, "usage-error: %s\nSee --help.\n", message.c_str() );
  return 2;
}

} // namespace

int main( int argc, char *argv[] )
{
  Options options;
  bool do_verify = false;
  bool has_profile = false;
  bool has_seed = false;
  bool has_spec = false;
  std::string spec_path;

  for ( int i = 1; i < argc; ++i )
  {
    const std::string arg = argv[i];
    if ( arg == "--help" || arg == "-h" )
    {
      printUsage( stdout );
      return 0;
    }
    if ( startsWith( arg, "--out=" ) )
    {
      options.out_dir = arg.substr( 6 );
      if ( options.out_dir.empty() )
        return usageError( "--out= requires a directory" );
      continue;
    }
    if ( startsWith( arg, "--profile=" ) )
    {
      if ( !parseProfile( arg.substr( 10 ), &options.profile ) )
        return usageError( "unknown profile '" + arg.substr( 10 ) + "' (lab|stress)" );
      has_profile = true;
      continue;
    }
    if ( startsWith( arg, "--seed=" ) )
    {
      if ( !parseSeed( arg.substr( 7 ), &options.seed ) )
        return usageError( "invalid seed '" + arg.substr( 7 ) + "' (0..4294967295)" );
      has_seed = true;
      continue;
    }
    if ( startsWith( arg, "--spec=" ) )
    {
      spec_path = arg.substr( 7 );
      has_spec = true;
      continue;
    }
    if ( arg == "--verify" )
    {
      do_verify = true;
      continue;
    }
    return usageError( "unrecognized argument '" + arg + "'" );
  }

  if ( options.out_dir.empty() )
    options.out_dir = "data/samples";

  if ( do_verify && ( has_profile || has_seed || has_spec ) )
    return usageError( "--verify takes only --out (profile/seed/spec generate data)" );

  if ( do_verify )
  {
    VerifyReport report;
    const Outcome outcome = verifyDirectory( options.out_dir, &report );
    if ( !outcome.ok )
    {
      std::fprintf( stderr, "verify-failed: [%s] %s\n", outcome.error.category.c_str(),
                    outcome.error.message.c_str() );
      return 4;
    }
    for ( const std::string &problem : report.problems )
      std::fprintf( stderr, "verify-drift: %s\n", problem.c_str() );
    std::printf( "verify %s: %zu files checked, %zu problem(s)\n",
                 report.problems.empty() ? "ok" : "FAILED", report.files_checked,
                 report.problems.size() );
    return report.problems.empty() ? 0 : 4;
  }

  if ( has_spec )
  {
    SpecRequest request;
    const Outcome outcome = loadSpec( spec_path, &request );
    if ( !outcome.ok )
    {
      std::fprintf( stderr, "spec-refusal: [%s] %s\n", outcome.error.category.c_str(),
                    outcome.error.message.c_str() );
      return 3;
    }
    options.products = request.products;
  }

  GenerateResult result;
  const Outcome outcome = generate( options, &result );
  if ( !outcome.ok )
  {
    std::fprintf( stderr, "generate-failed: [%s] %s\n", outcome.error.category.c_str(),
                  outcome.error.message.c_str() );
    return outcome.error.category == std::string( "usage" ) ? 2 : 1;
  }

  std::printf( "generated %zu file(s) into %s (profile=%s seed=%u)\n",
               result.files.size(), options.out_dir.c_str(),
               profileName( options.profile ), options.seed );
  for ( const EmittedFile &file : result.files )
    std::printf( "  %s  %12llu bytes  %s\n", file.sha256.c_str(),
                 static_cast<unsigned long long>( file.bytes ), file.name.c_str() );
  std::printf( "manifest: %s/%s\n", options.out_dir.c_str(), kManifestName );
  return 0;
}
