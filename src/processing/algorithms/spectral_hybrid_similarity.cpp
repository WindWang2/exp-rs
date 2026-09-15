// src/processing/algorithms/spectral_hybrid_similarity.cpp — SID-SAM hybrid
#include "spectral_hybrid_similarity.h"

#include "spectral_classification.h"
#include "spectral_wavelength.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace SpectralHybridSimilarity
{

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    /// Validate a caller-provided nm grid: positive, finite, strictly
    /// increasing (the contract of SpectralWavelength::Grid for already-nm
    /// values). @a what names the failing side for the error message.
    bool validGrid( const std::vector<float> &grid, const char *what, QString *errorMessage )
    {
        for ( size_t i = 0; i < grid.size(); ++i )
        {
            if ( !( grid[i] > 0.0f ) || !std::isfinite( grid[i] ) )
            {
                if ( errorMessage )
                    *errorMessage = QStringLiteral(
                        "Invalid wavelength grid (%1): wavelengths must be positive and finite" )
                                        .arg( QString::fromLatin1( what ) );
                return false;
            }
            if ( i > 0 && !( grid[i] > grid[i - 1] ) )
            {
                if ( errorMessage )
                    *errorMessage = QStringLiteral(
                        "Invalid wavelength grid (%1): wavelengths must be strictly increasing" )
                                        .arg( QString::fromLatin1( what ) );
                return false;
            }
        }
        return true;
    }
} // namespace

const char *formText( Form form )
{
    switch ( form )
    {
        case Form::ProductNormalized:
            return "product_normalized";
        case Form::ClassicTan:
            return "classic_tan";
    }
    return "unknown";
}

bool formFromText( const std::string &text, Form *out )
{
    if ( text == "product_normalized" )
    {
        *out = Form::ProductNormalized;
        return true;
    }
    if ( text == "classic_tan" )
    {
        *out = Form::ClassicTan;
        return true;
    }
    return false;
}

bool similarity( const float *t, const float *r, size_t bands, float nodata,
                 Form form, SimilarityResult *result,
                 QString *errorMessage,
                 const std::vector<float> *wavelengthsT,
                 const std::vector<float> *wavelengthsR )
{
    if ( !t || !r || bands == 0 || !result )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Invalid hybrid similarity arguments" );
        return false;
    }

    // Wavelength reconciliation guard: when both grids are present they must
    // describe the same, non-degenerate spectral range. A disjoint or
    // degenerate overlap means the two spectra are not band-comparable —
    // refusing beats emitting a number whose magnitude is meaningless.
    const bool hasGridT = wavelengthsT && !wavelengthsT->empty();
    const bool hasGridR = wavelengthsR && !wavelengthsR->empty();
    if ( hasGridT || hasGridR )
    {
        if ( hasGridT != hasGridR )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral(
                    "Wavelength grids must be provided for both spectra or neither" );
            return false;
        }
        if ( wavelengthsT->size() != bands || wavelengthsR->size() != bands )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral( "Wavelength grid size does not match band count" );
            return false;
        }
        if ( !validGrid( *wavelengthsT, "test spectrum", errorMessage )
             || !validGrid( *wavelengthsR, "reference spectrum", errorMessage ) )
            return false;
        SpectralWavelength::Grid gridT;
        SpectralWavelength::Grid gridR;
        gridT.centersNm = *wavelengthsT;
        gridR.centersNm = *wavelengthsR;
        std::string reason;
        if ( !SpectralWavelength::rangesOverlap( gridT, gridR, &reason ) )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral( "Spectral ranges do not overlap: %1" )
                                    .arg( QString::fromStdString( reason ) );
            return false;
        }
    }

    // Single source of truth for SAM and SID: the master classification
    // kernels. This file owns only the combination.
    const double sam = SpectralClassification::spectralAngle( t, r, bands, nodata );
    const double sid = SpectralClassification::spectralDivergence( t, r, bands, nodata );

    result->samRadians = sam;
    result->sidNats = sid;
    if ( !std::isfinite( sam ) || !std::isfinite( sid ) )
    {
        result->hybrid = std::numeric_limits<double>::quiet_NaN();
        result->defined = false;
        return true;
    }

    switch ( form )
    {
        case Form::ProductNormalized:
        {
            const double samPrime = std::clamp( 1.0 - 2.0 * sam / kPi, 0.0, 1.0 );
            const double sidPrime = 1.0 / ( 1.0 + sid );
            result->hybrid = samPrime * sidPrime;
            break;
        }
        case Form::ClassicTan:
            result->hybrid = sid * std::tan( sam );
            break;
    }
    if ( !std::isfinite( result->hybrid ) )
    {
        // ClassicTan at exactly theta = pi/2 (orthogonal spectra) diverges.
        // That is a property of the form, not an error: report it as +inf
        // so ordering still works (ClassicTan: lower = more similar).
        result->hybrid = std::numeric_limits<double>::infinity();
    }
    result->defined = true;
    return true;
}

bool classify( const float *pixels, size_t count, int bands,
               const float *refs, int refCount,
               int *labels, float *scores,
               Form form, float nodata,
               QString *errorMessage )
{
    if ( !pixels || !refs || !labels || count == 0 || bands <= 0 || refCount <= 0 )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Invalid hybrid similarity classification arguments" );
        return false;
    }

    // The hybrid is monotone in "more similar" for both forms
    // (ProductNormalized increases, ClassicTan decreases), so classification
    // scans for the best score per form.
    const bool higherIsBetter = ( form == Form::ProductNormalized );

    for ( size_t p = 0; p < count; ++p )
    {
        const float *pixel = pixels + p * static_cast<size_t>( bands );
        int best = -1;
        float bestScore = 0.0f;
        for ( int c = 0; c < refCount; ++c )
        {
            SimilarityResult result;
            if ( !similarity( pixel, refs + static_cast<size_t>( c ) * bands, bands,
                              nodata, form, &result ) )
            {
                if ( errorMessage )
                    *errorMessage = QStringLiteral( "Hybrid similarity failed on pixel %1" )
                                        .arg( static_cast<qulonglong>( p ) );
                return false;
            }
            if ( !result.defined )
                continue;
            const float score = static_cast<float>( result.hybrid );
            if ( best < 0
                 || ( higherIsBetter ? score > bestScore : score < bestScore ) )
            {
                best = c;
                bestScore = score;
            }
        }
        labels[p] = best;
        if ( scores )
            scores[p] = best >= 0 ? bestScore
                                  : std::numeric_limits<float>::quiet_NaN();
    }
    return true;
}

} // namespace SpectralHybridSimilarity
