// src/processing/algorithms/endmember_analysis.cpp — endmember set analysis
#include "endmember_analysis.h"

#include "spectral_classification.h"
#include "spectral_resampling.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace EndmemberAnalysis
{

namespace
{
    constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

    bool rowFiniteNonZero( const float *row, int bands, double *normSq )
    {
        double sum = 0.0;
        for ( int b = 0; b < bands; ++b )
        {
            if ( !std::isfinite( row[b] ) )
                return false;
            sum += static_cast<double>( row[b] ) * static_cast<double>( row[b] );
        }
        if ( normSq )
            *normSq = sum;
        return sum > 0.0;
    }
} // namespace

bool angleMatrix( const float *endmembers, int nEndmembers, int bands,
                  std::vector<double> *matrix,
                  QString *errorMessage )
{
    if ( !endmembers || !matrix || nEndmembers <= 0 || bands <= 0 )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Invalid endmember angle-matrix arguments" );
        return false;
    }
    for ( int e = 0; e < nEndmembers; ++e )
    {
        double normSq = 0.0;
        if ( !rowFiniteNonZero( endmembers + static_cast<size_t>( e ) * bands, bands, &normSq ) )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral(
                    "Endmember %1 is non-finite or has zero norm; angles are undefined" )
                                    .arg( e );
            return false;
        }
    }

    const float nodata = SpectralClassification::kNoDataSentinel;
    matrix->assign( static_cast<size_t>( nEndmembers ) * nEndmembers, 0.0 );
    std::vector<float> spectrumA( static_cast<size_t>( bands ) );
    std::vector<float> spectrumB( static_cast<size_t>( bands ) );
    for ( int a = 0; a < nEndmembers; ++a )
    {
        std::copy( endmembers + static_cast<size_t>( a ) * bands,
                   endmembers + static_cast<size_t>( a + 1 ) * bands,
                   spectrumA.begin() );
        for ( int c = a + 1; c < nEndmembers; ++c )
        {
            std::copy( endmembers + static_cast<size_t>( c ) * bands,
                       endmembers + static_cast<size_t>( c + 1 ) * bands,
                       spectrumB.begin() );
            const double angle = SpectralClassification::spectralAngle(
                spectrumA.data(), spectrumB.data(), static_cast<size_t>( bands ), nodata );
            ( *matrix )[static_cast<size_t>( a ) * nEndmembers + c] = angle;
            ( *matrix )[static_cast<size_t>( c ) * nEndmembers + a] = angle;
        }
    }
    return true;
}

bool reduceEndmembers( const float *endmembers, int nEndmembers, int bands,
                       const ReduceConfig &config,
                       const std::vector<int> *ppiCounts,
                       ReduceResult *result,
                       QString *errorMessage )
{
    if ( !endmembers || !result || nEndmembers <= 0 || bands <= 0 )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Invalid endmember reduction arguments" );
        return false;
    }
    if ( config.mergeAngleDegrees < 0.0 || !std::isfinite( config.mergeAngleDegrees ) )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "mergeAngleDegrees must be a finite value >= 0" );
        return false;
    }
    if ( ppiCounts && static_cast<int>( ppiCounts->size() ) != nEndmembers )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "ppiCounts size does not match the endmember count" );
        return false;
    }
    if ( nEndmembers > 512 )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral(
                "Endmember reduction is capped at 512 inputs (got %1); reduce the "
                "dictionary first" )
                                .arg( nEndmembers );
        return false;
    }
    for ( int e = 0; e < nEndmembers; ++e )
    {
        double normSq = 0.0;
        if ( !rowFiniteNonZero( endmembers + static_cast<size_t>( e ) * bands, bands, &normSq ) )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral(
                    "Endmember %1 is non-finite or has zero norm" )
                                    .arg( e );
            return false;
        }
    }

    std::vector<double> angles;
    if ( !angleMatrix( endmembers, nEndmembers, bands, &angles, errorMessage ) )
        return false;

    // Union-find style agglomerative merging with cluster membership lists;
    // clusters start as singletons, the closest pair merges while below the
    // threshold, and inter-cluster distances are the size-weighted average
    // of member angles (average link).
    struct Cluster
    {
        std::vector<int> members;
    };
    std::vector<Cluster> clusters;
    clusters.reserve( static_cast<size_t>( nEndmembers ) );
    for ( int e = 0; e < nEndmembers; ++e )
        clusters.push_back( { { e } } );

    auto clusterDistance = [&]( int a, int b ) -> double
    {
        double sum = 0.0;
        for ( int ma : clusters[static_cast<size_t>( a )].members )
            for ( int mb : clusters[static_cast<size_t>( b )].members )
                sum += angles[static_cast<size_t>( ma ) * nEndmembers + mb];
        const double size = static_cast<double>(
            clusters[static_cast<size_t>( a )].members.size()
            * clusters[static_cast<size_t>( b )].members.size() );
        return sum / size;
    };

    const double thresholdRadians = config.mergeAngleDegrees * kDegToRad;
    std::vector<double> mergeAngles;
    while ( clusters.size() >= 2 )
    {
        int bestA = -1, bestB = -1;
        double bestDistance = std::numeric_limits<double>::infinity();
        for ( size_t a = 0; a < clusters.size(); ++a )
        {
            for ( size_t b = a + 1; b < clusters.size(); ++b )
            {
                const double d = clusterDistance( static_cast<int>( a ), static_cast<int>( b ) );
                if ( d < bestDistance )
                {
                    bestDistance = d;
                    bestA = static_cast<int>( a );
                    bestB = static_cast<int>( b );
                }
            }
        }
        if ( bestA < 0 || bestDistance >= thresholdRadians )
            break;
        // Merge b into a (stable order), record the merge distance.
        mergeAngles.push_back( bestDistance );
        clusters[static_cast<size_t>( bestA )].members.insert(
            clusters[static_cast<size_t>( bestA )].members.end(),
            clusters[static_cast<size_t>( bestB )].members.begin(),
            clusters[static_cast<size_t>( bestB )].members.end() );
        clusters.erase( clusters.begin() + bestB );
    }

    // Representative selection: highest PPI count (ties: lowest source index).
    result->endmembers.clear();
    result->representativeOf.clear();
    result->clusterSizes.clear();
    result->clusterOf.assign( static_cast<size_t>( nEndmembers ), -1 );
    result->mergeAngles = std::move( mergeAngles );

    for ( size_t c = 0; c < clusters.size(); ++c )
    {
        int representative = clusters[c].members.front();
        int bestCount = ppiCounts ? ( *ppiCounts )[static_cast<size_t>( representative )] : 0;
        for ( int member : clusters[c].members )
        {
            const int count = ppiCounts ? ( *ppiCounts )[static_cast<size_t>( member )] : 0;
            if ( count > bestCount
                 || ( count == bestCount && member < representative ) )
            {
                bestCount = count;
                representative = member;
            }
        }
        const int clusterId = static_cast<int>( c );
        for ( int member : clusters[c].members )
            result->clusterOf[static_cast<size_t>( member )] = clusterId;
        result->representativeOf.push_back( representative );
        result->clusterSizes.push_back( static_cast<int>( clusters[c].members.size() ) );
        result->endmembers.insert(
            result->endmembers.end(),
            endmembers + static_cast<size_t>( representative ) * bands,
            endmembers + static_cast<size_t>( representative + 1 ) * bands );
    }
    return true;
}

bool projectToSensor( const float *endmembers, int nEndmembers, int bands,
                      const float *srcWavelengthsNm,
                      const float *dstWavelengthsNm, const float *dstFwhmNm,
                      int dstBands, bool requireFull,
                      ProjectionResult *result,
                      QString *errorMessage )
{
    if ( !endmembers || !result || nEndmembers <= 0 || bands <= 0
         || !srcWavelengthsNm || !dstWavelengthsNm || dstBands <= 0 )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral(
                "Invalid sensor projection arguments (wavelength metadata is mandatory)" );
        return false;
    }
    for ( int b = 0; b < bands; ++b )
    {
        const float wl = srcWavelengthsNm[b];
        if ( !( wl > 0.0f ) || !std::isfinite( wl )
             || ( b > 0 && !( wl > srcWavelengthsNm[b - 1] ) ) )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral(
                    "Source wavelengths must be positive, finite and strictly increasing" );
            return false;
        }
    }
    for ( int b = 0; b < dstBands; ++b )
    {
        const float wl = dstWavelengthsNm[b];
        if ( !( wl > 0.0f ) || !std::isfinite( wl )
             || ( b > 0 && !( wl > dstWavelengthsNm[b - 1] ) ) )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral(
                    "Target wavelengths must be positive, finite and strictly increasing" );
            return false;
        }
    }
    if ( dstFwhmNm )
    {
        for ( int b = 0; b < dstBands; ++b )
        {
            if ( !( dstFwhmNm[b] > 0.0f ) || !std::isfinite( dstFwhmNm[b] ) )
            {
                if ( errorMessage )
                    *errorMessage = QStringLiteral(
                        "Target FWHM values must be positive and finite" );
                return false;
            }
        }
    }

    result->spectra.assign( static_cast<size_t>( nEndmembers ) * dstBands,
                            std::numeric_limits<float>::quiet_NaN() );
    result->wavelengthsNm.assign( dstWavelengthsNm, dstWavelengthsNm + dstBands );
    if ( dstFwhmNm )
        result->fwhmNm.assign( dstFwhmNm, dstFwhmNm + dstBands );
    result->fullyCovered.assign( static_cast<size_t>( nEndmembers ), 0 );
    result->fullyCoveredCount = 0;

    for ( int e = 0; e < nEndmembers; ++e )
    {
        const float *src = endmembers + static_cast<size_t>( e ) * bands;
        float *dst = result->spectra.data() + static_cast<size_t>( e ) * dstBands;
        if ( dstFwhmNm )
        {
            if ( !SpectralResampling::resampleSpectrumGaussian(
                     src, srcWavelengthsNm, bands, dstWavelengthsNm, dstFwhmNm,
                     dstBands, dst ) )
            {
                if ( errorMessage )
                    *errorMessage = QStringLiteral( "Gaussian SRF resampling failed for endmember %1" )
                                        .arg( e );
                return false;
            }
        }
        else
        {
            if ( !SpectralResampling::resampleSpectrum(
                     src, srcWavelengthsNm, bands, dstWavelengthsNm, dstBands, dst ) )
            {
                if ( errorMessage )
                    *errorMessage = QStringLiteral( "Linear resampling failed for endmember %1" )
                                        .arg( e );
                return false;
            }
        }
        bool full = true;
        for ( int b = 0; b < dstBands; ++b )
        {
            if ( !std::isfinite( dst[b] ) )
            {
                full = false;
                break;
            }
        }
        result->fullyCovered[static_cast<size_t>( e )] = full ? 1 : 0;
        if ( full )
            ++result->fullyCoveredCount;
    }

    if ( requireFull && result->fullyCoveredCount != nEndmembers )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral(
                "Only %1 of %2 endmembers are fully covered by the source range; "
                "refusing (requireFull)" )
                                .arg( result->fullyCoveredCount )
                                .arg( nEndmembers );
        return false;
    }
    return true;
}

} // namespace EndmemberAnalysis
