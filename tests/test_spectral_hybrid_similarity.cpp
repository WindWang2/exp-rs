// test_spectral_hybrid_similarity.cpp — SID-SAM hybrid kernel known answers.
// Every expected value is computed here from its closed form, independently
// of the kernel under test (which only re-uses master's SAM/SID primitives).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <limits>
#include <vector>

#include "processing/algorithms/spectral_hybrid_similarity.h"

using namespace SpectralHybridSimilarity;
using Catch::Approx;

namespace
{
    constexpr double kPi = 3.14159265358979323846;
}

TEST_CASE( "Hybrid similarity of identical and scaled spectra", "[hybrid][kernel]" )
{
    const std::vector<float> r = { 0.3f, 0.5f, 0.2f, 0.9f, 0.1f };

    SimilarityResult same;
    REQUIRE( similarity( r.data(), r.data(), r.size(), -9999.0f,
                         Form::ProductNormalized, &same ) );
    REQUIRE( same.defined );
    // SAM of float-identical spectra carries acos rounding (~1e-8), not 0.
    CHECK( same.samRadians == Approx( 0.0 ).margin( 1e-6 ) );
    CHECK( same.sidNats == Approx( 0.0 ).margin( 1e-12 ) );
    CHECK( same.hybrid == Approx( 1.0 ).margin( 1e-6 ) );

    // Brightness invariance: scaling one spectrum must not change any measure.
    std::vector<float> scaled( r.size() );
    for ( size_t i = 0; i < r.size(); ++i )
        scaled[i] = 7.5f * r[i];
    SimilarityResult scaledResult;
    REQUIRE( similarity( scaled.data(), r.data(), r.size(), -9999.0f,
                         Form::ProductNormalized, &scaledResult ) );
    REQUIRE( scaledResult.defined );
    CHECK( scaledResult.samRadians == Approx( 0.0 ).margin( 1e-6 ) );
    CHECK( scaledResult.sidNats == Approx( 0.0 ).margin( 1e-6 ) );
    CHECK( scaledResult.hybrid == Approx( 1.0 ).margin( 1e-6 ) );
}

TEST_CASE( "Hybrid similarity closed form for a two-band pair", "[hybrid][kernel]" )
{
    // t = [1, 2], r = [2, 1]:
    //   SAM  = arccos( (1*2 + 2*1) / (sqrt(5) * sqrt(5)) ) = arccos(4/5)
    //   p = [1/3, 2/3], q = [2/3, 1/3]
    //   SID  = 2 * ( (1/3) ln(1/2) + (2/3) ln 2 ) / ... = (2/3) ln 2   (nats)
    const std::vector<float> t = { 1.0f, 2.0f };
    const std::vector<float> r = { 2.0f, 1.0f };

    const double expectedSam = std::acos( 0.8 );
    const double expectedSid = 2.0 / 3.0 * std::log( 2.0 );

    SimilarityResult product;
    REQUIRE( similarity( t.data(), r.data(), 2, -9999.0f,
                         Form::ProductNormalized, &product ) );
    REQUIRE( product.defined );
    CHECK( product.samRadians == Approx( expectedSam ).margin( 1e-12 ) );
    CHECK( product.sidNats == Approx( expectedSid ).margin( 1e-12 ) );
    const double expectedProduct = ( 1.0 - 2.0 * expectedSam / kPi ) / ( 1.0 + expectedSid );
    CHECK( product.hybrid == Approx( expectedProduct ).margin( 1e-12 ) );

    SimilarityResult tanForm;
    REQUIRE( similarity( t.data(), r.data(), 2, -9999.0f, Form::ClassicTan, &tanForm ) );
    REQUIRE( tanForm.defined );
    CHECK( tanForm.hybrid == Approx( expectedSid * std::tan( expectedSam ) ).margin( 1e-12 ) );
}

TEST_CASE( "Orthogonal spectra bound the two forms differently", "[hybrid][kernel]" )
{
    // Disjoint supports: master's SID convention skips zero-probability
    // terms, so the divergence is 0; SAM is exactly pi/2. ProductNormalized
    // therefore reaches its floor (0 = maximally dissimilar direction) while
    // ClassicTan diverges — the documented property that motivated D4.
    const std::vector<float> t = { 1.0f, 0.0f, 1.0f };
    const std::vector<float> r = { 0.0f, 1.0f, 0.0f };

    SimilarityResult product;
    REQUIRE( similarity( t.data(), r.data(), 3, -9999.0f,
                         Form::ProductNormalized, &product ) );
    REQUIRE( product.defined );
    CHECK( product.samRadians == Approx( kPi / 2.0 ).margin( 1e-12 ) );
    CHECK( product.hybrid == Approx( 0.0 ).margin( 1e-12 ) );

    SimilarityResult tanForm;
    REQUIRE( similarity( t.data(), r.data(), 3, -9999.0f, Form::ClassicTan, &tanForm ) );
    REQUIRE( tanForm.defined );
    CHECK( std::isinf( tanForm.hybrid ) );
}

TEST_CASE( "Nodata and degenerate spectra are unscorable, not errors", "[hybrid][kernel]" )
{
    const std::vector<float> t = { 0.2f, -9999.0f, 0.3f };
    const std::vector<float> r = { 0.2f, 0.5f, 0.3f };
    SimilarityResult result;
    REQUIRE( similarity( t.data(), r.data(), 3, -9999.0f,
                         Form::ProductNormalized, &result ) );
    CHECK_FALSE( result.defined );
    CHECK( std::isnan( result.hybrid ) );

    const std::vector<float> zero = { 0.0f, 0.0f };
    REQUIRE( similarity( zero.data(), r.data(), 2, -9999.0f,
                         Form::ProductNormalized, &result ) );
    CHECK_FALSE( result.defined );
}

TEST_CASE( "Wavelength grid guard refuses non-comparable grids", "[hybrid][kernel]" )
{
    const std::vector<float> t = { 0.2f, 0.5f, 0.3f };
    const std::vector<float> r = { 0.2f, 0.5f, 0.3f };
    const std::vector<float> gridA = { 500.0f, 600.0f, 700.0f };
    const std::vector<float> gridB = { 2100.0f, 2200.0f, 2300.0f }; // disjoint
    const std::vector<float> badGrid = { 500.0f, 700.0f, 600.0f }; // not increasing
    const std::vector<float> shortGrid = { 500.0f, 600.0f }; // wrong size
    QString err;

    SimilarityResult result;
    CHECK_FALSE( similarity( t.data(), r.data(), 3, -9999.0f, Form::ProductNormalized,
                             &result, &err, &gridA, &gridB ) );
    CHECK( err.contains( "do not overlap" ) );

    CHECK_FALSE( similarity( t.data(), r.data(), 3, -9999.0f, Form::ProductNormalized,
                             &result, &err, &gridA, &badGrid ) );
    CHECK( err.contains( "strictly increasing" ) );

    CHECK_FALSE( similarity( t.data(), r.data(), 3, -9999.0f, Form::ProductNormalized,
                             &result, &err, &gridA, &shortGrid ) );
    CHECK( err.contains( "does not match band count" ) );

    CHECK_FALSE( similarity( t.data(), r.data(), 3, -9999.0f, Form::ProductNormalized,
                             &result, &err, &gridA, nullptr ) );
    CHECK( err.contains( "both spectra or neither" ) );

    // Matching grids pass.
    REQUIRE( similarity( t.data(), r.data(), 3, -9999.0f, Form::ProductNormalized,
                         &result, &err, &gridA, &gridA ) );
    REQUIRE( result.defined );
}

TEST_CASE( "Hybrid classification labels pixels to the most similar reference", "[hybrid][operators]" )
{
    // Three-band references: vegetation-like ramp vs flat bright.
    const std::vector<float> refs = {
        0.1f, 0.3f, 0.6f, // class 0: increasing ramp
        0.5f, 0.5f, 0.5f, // class 1: flat
    };
    const std::vector<float> pixels = {
        0.1f, 0.3f, 0.6f, // -> class 0 (exact)
        0.2f, 0.6f, 1.2f, // -> class 0 (scaled ramp)
        0.5f, 0.5f, 0.5f, // -> class 1 (exact)
        -9999.0f, 0.0f, 0.0f, // nodata pixel -> label -1
    };
    std::vector<int> labels( 4, -2 );
    std::vector<float> scores( 4, 0.0f );
    QString err;
    REQUIRE( classify( pixels.data(), 4, 3, refs.data(), 2, labels.data(),
                       scores.data(), Form::ProductNormalized, -9999.0f, &err ) );
    CHECK( labels[0] == 0 );
    CHECK( labels[1] == 0 );
    CHECK( labels[2] == 1 );
    CHECK( labels[3] == -1 );
    CHECK( std::isnan( scores[3] ) );
    // The hybrid is brightness-invariant by construction: an exact match and
    // a positively-scaled copy of the reference both reach the maximal score.
    CHECK( scores[0] == Approx( scores[2] ).margin( 1e-6 ) );
    CHECK( scores[0] > 0.999f );

    // Invalid arguments refuse.
    CHECK_FALSE( classify( pixels.data(), 0, 3, refs.data(), 2, labels.data(),
                           nullptr, Form::ProductNormalized, -9999.0f, &err ) );
    CHECK_FALSE( classify( pixels.data(), 4, 0, refs.data(), 2, labels.data(),
                           nullptr, Form::ProductNormalized, -9999.0f, &err ) );
    CHECK_FALSE( classify( pixels.data(), 4, 3, refs.data(), 0, labels.data(),
                           nullptr, Form::ProductNormalized, -9999.0f, &err ) );
}

TEST_CASE( "Form parsing is fail-closed", "[hybrid][kernel]" )
{
    Form form = Form::ProductNormalized;
    REQUIRE( formFromText( "product_normalized", &form ) );
    CHECK( form == Form::ProductNormalized );
    REQUIRE( formFromText( "classic_tan", &form ) );
    CHECK( form == Form::ClassicTan );
    CHECK_FALSE( formFromText( "Product_Normalized", &form ) ); // exact names only
    CHECK_FALSE( formFromText( "hybrid", &form ) );
}
