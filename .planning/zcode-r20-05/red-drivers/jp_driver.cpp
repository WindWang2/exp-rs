// R20-05 #1607-residual RED driver: a nesting-bomb ledger/summary must be a
// typed refusal, never an uncaught jsoncpp RuntimeError.
#include "teaching/lab_completion_ledger.h"
#include "teaching/verdict_stamp.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

int main()
{
    const std::string bomb = "{ \"schema\": \"sicnu.teaching.completion-ledger/1\", \"deep\": "
                             + std::string( 300, '[' ) + std::string( 300, ']' ) + " }";
    const auto dir = std::filesystem::temp_directory_path() / "r2005_jp";
    std::filesystem::create_directories( dir );
    const auto ledgerPath = dir / "ledger.json";
    { std::ofstream out( ledgerPath, std::ios::binary ); out << bomb; }

    const auto refused = sicnu::teaching::LabCompletionLedger::loadVerified( ledgerPath.string() );
    const bool ledgerTypedRefusal = !refused.ok && !refused.issuesZh.empty();
    const bool digestRefusal = sicnu::teaching::verdictSummaryDigest( bomb ).empty();
    std::cout << "ledger typed refusal: " << ( ledgerTypedRefusal ? "yes" : "NO" )
              << " | digest empty refusal: " << ( digestRefusal ? "yes" : "NO" ) << "\n";
    return ( ledgerTypedRefusal && digestRefusal ) ? 0 : 1;
}
