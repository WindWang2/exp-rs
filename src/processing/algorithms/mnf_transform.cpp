// src/processing/algorithms/mnf_transform.cpp — complete MNF chain
#include "mnf_transform.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace MnfTransform
{
namespace
{

/// Double-precision cyclic Jacobi eigendecomposition of a symmetric matrix
/// (in place). Eigenvectors land as COLUMNS of @p vectors; eigenvalues
/// unsorted. Converges to machine precision for the B <= kMaxBands scales
/// this kernel accepts.
bool jacobiEigenDouble( std::vector<double> &a, int n, std::vector<double> &eigenvalues,
                        std::vector<double> &vectors )
{
    vectors.assign( static_cast<size_t>( n ) * n, 0.0 );
    for ( int i = 0; i < n; ++i )
        vectors[static_cast<size_t>( i ) * n + i] = 1.0;
    eigenvalues.assign( static_cast<size_t>( n ), 0.0 );
    if ( n <= 0 )
        return true;
    if ( n == 1 )
    {
        eigenvalues[0] = a[0];
        return std::isfinite( a[0] );
    }

    constexpr int kMaxSweeps = 60;
    for ( int sweep = 0; sweep < kMaxSweeps; ++sweep )
    {
        double offDiagonal = 0.0;
        double diagonalScale = 0.0;
        for ( int i = 0; i < n; ++i )
        {
            for ( int j = i + 1; j < n; ++j )
                offDiagonal += a[static_cast<size_t>( i ) * n + j]
                               * a[static_cast<size_t>( i ) * n + j];
            const double d = a[static_cast<size_t>( i ) * n + i];
            diagonalScale += d * d;
        }
        // Relative convergence: the off-diagonal energy is negligible against
        // the diagonal energy (never an absolute-only threshold — covariance
        // scales vary by orders of magnitude across bands).
        if ( offDiagonal <= diagonalScale * 1e-24 )
            break;

        for ( int p = 0; p < n - 1; ++p )
        {
            for ( int q = p + 1; q < n; ++q )
            {
                const double apq = a[static_cast<size_t>( p ) * n + q];
                if ( apq == 0.0 )
                    continue;
                const double app = a[static_cast<size_t>( p ) * n + p];
                const double aqq = a[static_cast<size_t>( q ) * n + q];
                if ( std::abs( apq )
                     <= 1e-18 * ( std::abs( app ) + std::abs( aqq ) + 1e-300 ) )
                    continue;
                const double theta = ( aqq - app ) / ( 2.0 * apq );
                const double t =
                    ( theta >= 0.0 ? 1.0 : -1.0 )
                    / ( std::abs( theta ) + std::sqrt( theta * theta + 1.0 ) );
                const double c = 1.0 / std::sqrt( t * t + 1.0 );
                const double s = t * c;

                for ( int k = 0; k < n; ++k )
                {
                    const double akp = a[static_cast<size_t>( k ) * n + p];
                    const double akq = a[static_cast<size_t>( k ) * n + q];
                    a[static_cast<size_t>( k ) * n + p] = c * akp - s * akq;
                    a[static_cast<size_t>( k ) * n + q] = s * akp + c * akq;
                }
                for ( int k = 0; k < n; ++k )
                {
                    const double apk = a[static_cast<size_t>( p ) * n + k];
                    const double aqk = a[static_cast<size_t>( q ) * n + k];
                    a[static_cast<size_t>( p ) * n + k] = c * apk - s * aqk;
                    a[static_cast<size_t>( q ) * n + k] = s * apk + c * aqk;
                }
                for ( int k = 0; k < n; ++k )
                {
                    const double vkp = vectors[static_cast<size_t>( k ) * n + p];
                    const double vkq = vectors[static_cast<size_t>( k ) * n + q];
                    vectors[static_cast<size_t>( k ) * n + p] = c * vkp - s * vkq;
                    vectors[static_cast<size_t>( k ) * n + q] = s * vkp + c * vkq;
                }
            }
        }
    }

    for ( int i = 0; i < n; ++i )
    {
        eigenvalues[static_cast<size_t>( i )] = a[static_cast<size_t>( i ) * n + i];
        if ( !std::isfinite( eigenvalues[static_cast<size_t>( i )] ) )
            return false;
    }
    return true;
}

QString formatDouble( double v )
{
    char buf[40];
    std::snprintf( buf, sizeof( buf ), "%.17g", v );
    return QString::fromLatin1( buf );
}

/// Canonical digest over mean + both bases (round-trip-exact %.17g text).
QString modelDigest( const Model &model )
{
    QCryptographicHash hash( QCryptographicHash::Sha256 );
    const QString header = QStringLiteral( "%1/v%2/%3\n" )
                             .arg( kKind )
                             .arg( kFormatVersion )
                             .arg( model.bandCount );
    hash.addData( header.toUtf8() );
    auto feed = [&hash]( const std::vector<double> &values ) {
        for ( double v : values )
        {
            hash.addData( formatDouble( v ).toUtf8() );
            hash.addData( "\n", 1 );
        }
    };
    feed( model.mean );
    feed( model.forwardBasis );
    feed( model.inverseBasis );
    return QString::fromLatin1( hash.result().toHex() );
}

} // namespace

RowFeeder::RowFeeder( int bands )
    : m_bands( bands )
{
    const size_t b = static_cast<size_t>( bands );
    m_sum.assign( b, 0.0 );
    m_mean.assign( b, 0.0 );
    m_signalCov.assign( b * b, 0.0 );
    m_noiseCov.assign( b * b, 0.0 );
    m_ddSum.assign( b * b, 0.0 );
    m_diffSum.assign( b, 0.0 );
}

void RowFeeder::addRow( const float *bipRow, int width, const uint8_t *validMask )
{
    if ( m_covFinalized || width <= 0 )
        return;
    const size_t b = static_cast<size_t>( m_bands );

    if ( !m_meanFinalized )
    {
        for ( int p = 0; p < width; ++p )
        {
            if ( validMask && !validMask[p] )
                continue;
            const float *spectrum = bipRow + static_cast<size_t>( p ) * b;
            for ( int i = 0; i < m_bands; ++i )
                m_sum[static_cast<size_t>( i )] += static_cast<double>( spectrum[i] );
            ++m_samples;
        }
        return;
    }

    accumulateSecondMoments( bipRow, width, validMask );
}

void RowFeeder::accumulateSecondMoments( const float *bipRow, int width,
                                         const uint8_t *validMask )
{
    const size_t b = static_cast<size_t>( m_bands );

    // Signal second moments around the frozen mean, valid pixels only.
    for ( int p = 0; p < width; ++p )
    {
        if ( validMask && !validMask[p] )
            continue;
        const float *spectrum = bipRow + static_cast<size_t>( p ) * b;
        for ( int i = 0; i < m_bands; ++i )
        {
            const double xi = static_cast<double>( spectrum[i] )
                              - m_mean[static_cast<size_t>( i )];
            for ( int j = i; j < m_bands; ++j )
            {
                const double xj = static_cast<double>( spectrum[j] )
                                  - m_mean[static_cast<size_t>( j )];
                m_signalCov[static_cast<size_t>( i ) * b + j] += xi * xj;
            }
        }
        ++m_samples;
    }
    for ( int i = 0; i < m_bands; ++i )
        for ( int j = 0; j < i; ++j )
            m_signalCov[static_cast<size_t>( i ) * b + j] =
                m_signalCov[static_cast<size_t>( j ) * b + i];

    // Horizontal shift differences within the row between two adjacent valid
    // pixels (row ends are boundaries — the pair across two row segments is
    // never differenced).
    for ( int p = 1; p < width; ++p )
    {
        if ( validMask && ( !validMask[p - 1] || !validMask[p] ) )
            continue;
        const float *left = bipRow + static_cast<size_t>( p - 1 ) * b;
        const float *right = bipRow + static_cast<size_t>( p ) * b;
        for ( int i = 0; i < m_bands; ++i )
        {
            const double di = static_cast<double>( right[i] ) - static_cast<double>( left[i] );
            m_diffSum[static_cast<size_t>( i )] += di;
            for ( int j = i; j < m_bands; ++j )
            {
                const double dj =
                    static_cast<double>( right[j] ) - static_cast<double>( left[j] );
                m_ddSum[static_cast<size_t>( i ) * b + j] += di * dj;
            }
        }
        ++m_noiseSamples;
    }
    for ( int i = 0; i < m_bands; ++i )
        for ( int j = 0; j < i; ++j )
            m_ddSum[static_cast<size_t>( i ) * b + j] =
                m_ddSum[static_cast<size_t>( j ) * b + i];
}

void RowFeeder::finalizeMean()
{
    if ( m_samples == 0 )
        return;
    for ( int i = 0; i < m_bands; ++i )
        m_mean[static_cast<size_t>( i )] =
            m_sum[static_cast<size_t>( i )] / static_cast<double>( m_samples );
    m_meanFinalized = true;
}

void RowFeeder::finalizeCovariances()
{
    const size_t b = static_cast<size_t>( m_bands );
    const double divisor =
        ( m_samples > 1 ) ? static_cast<double>( m_samples - 1 ) : 1.0;

    for ( int i = 0; i < m_bands; ++i )
        for ( int j = 0; j < m_bands; ++j )
            m_signalCov[static_cast<size_t>( i ) * b + j] /= divisor;

    if ( m_noiseSamples > 1 )
    {
        const double nd = static_cast<double>( m_noiseSamples );
        const double noiseDivisor = nd - 1.0;
        for ( int i = 0; i < m_bands; ++i )
        {
            const double meanDi = m_diffSum[static_cast<size_t>( i )] / nd;
            for ( int j = 0; j < m_bands; ++j )
            {
                const double meanDj = m_diffSum[static_cast<size_t>( j )] / nd;
                double cov = m_ddSum[static_cast<size_t>( i ) * b + j] / noiseDivisor
                             - ( nd / noiseDivisor ) * meanDi * meanDj;
                cov /= 2.0; // d = n1 - n2 with independent equal-variance noise
                m_noiseCov[static_cast<size_t>( i ) * b + j] = cov;
            }
        }
    }
    m_covFinalized = true;
}

bool fit( const RowFeeder &stats, Model *out, QString *errorMessage )
{
    auto fail = [errorMessage]( const QString &msg ) {
        if ( errorMessage )
            *errorMessage = msg;
        return false;
    };

    if ( !out )
        return fail( QStringLiteral( "Model output pointer is null" ) );

    const int b = static_cast<int>( stats.mean().size() );
    if ( b < 2 || b > kMaxBands )
        return fail( QStringLiteral( "Band count %1 outside [2, %2]" ).arg( b ).arg( kMaxBands ) );
    if ( stats.sampleCount() < static_cast<uint64_t>( b + 1 ) )
        return fail( QStringLiteral( "Insufficient valid samples (%1) for %2 bands" )
                         .arg( stats.sampleCount() )
                         .arg( b ) );
    if ( stats.noiseSampleCount() < static_cast<uint64_t>( b ) )
        return fail( QStringLiteral( "Insufficient noise-difference samples (%1)" )
                         .arg( stats.noiseSampleCount() ) );

    Model model;
    model.bandCount = b;
    model.mean = stats.mean();
    for ( double v : model.mean )
        if ( !std::isfinite( v ) )
            return fail( QStringLiteral( "Non-finite band mean" ) );

    // 1. Noise covariance eigendecomposition. A singular noise estimate is a
    //    typed refusal (never a silent pseudo-inverse clamp).
    std::vector<double> noiseCov = stats.noiseCovariance();
    std::vector<double> noiseEigen;
    std::vector<double> noiseVectors; // columns = eigenvectors
    if ( !jacobiEigenDouble( noiseCov, b, noiseEigen, noiseVectors ) )
        return fail( QStringLiteral( "Non-finite noise covariance" ) );
    double maxNoiseEigen = 0.0;
    for ( double v : noiseEigen )
        maxNoiseEigen = std::max( maxNoiseEigen, std::abs( v ) );
    const double noiseFloor = std::max( maxNoiseEigen * 1e-10, 1e-300 );
    for ( int k = 0; k < b; ++k )
    {
        if ( noiseEigen[static_cast<size_t>( k )] <= noiseFloor )
            return fail( QStringLiteral( "Noise covariance is singular (eigenvalue %1 "
                                         "at index %2); MNF requires a non-degenerate "
                                         "noise estimate" )
                             .arg( noiseEigen[static_cast<size_t>( k )] )
                             .arg( k ) );
    }
    model.noiseEigenvalues = noiseEigen;

    // Whitening matrix W[b][k] = Vn[b][k] / sqrt(Ln_k): z_k = sum_b W[b][k]·x_c[b].
    std::vector<double> whitening( static_cast<size_t>( b ) * b, 0.0 );
    for ( int k = 0; k < b; ++k )
    {
        const double scale = std::sqrt( noiseEigen[static_cast<size_t>( k )] );
        for ( int i = 0; i < b; ++i )
            whitening[static_cast<size_t>( i ) * b + k] =
                noiseVectors[static_cast<size_t>( i ) * b + k] / scale;
    }

    // 2. Whitened signal covariance Cw = Wᵀ Σs W, computed as two B³ products:
    //    P = Σs W, then Cw = Wᵀ P.
    const std::vector<double> &signalCov = stats.signalCovariance();
    std::vector<double> pMatrix( static_cast<size_t>( b ) * b, 0.0 );
    for ( int c = 0; c < b; ++c )
    {
        for ( int r = 0; r < b; ++r )
        {
            double sum = 0.0;
            for ( int k = 0; k < b; ++k )
                sum += signalCov[static_cast<size_t>( r ) * b + k]
                       * whitening[static_cast<size_t>( k ) * b + c];
            pMatrix[static_cast<size_t>( r ) * b + c] = sum;
        }
    }
    std::vector<double> whitenedCov( static_cast<size_t>( b ) * b, 0.0 );
    for ( int l = 0; l < b; ++l )
    {
        for ( int k = 0; k < b; ++k )
        {
            double sum = 0.0;
            for ( int c = 0; c < b; ++c )
                sum += whitening[static_cast<size_t>( c ) * b + k]
                       * pMatrix[static_cast<size_t>( c ) * b + l];
            whitenedCov[static_cast<size_t>( k ) * b + l] = sum;
        }
    }

    std::vector<double> snrEigen;
    std::vector<double> signalVectors;
    if ( !jacobiEigenDouble( whitenedCov, b, snrEigen, signalVectors ) )
        return fail( QStringLiteral( "Non-finite whitened signal covariance" ) );

    std::vector<int> order( static_cast<size_t>( b ) );
    for ( int i = 0; i < b; ++i )
        order[static_cast<size_t>( i )] = i;
    std::sort( order.begin(), order.end(), [&]( int lhs, int rhs ) {
        return snrEigen[static_cast<size_t>( lhs )] > snrEigen[static_cast<size_t>( rhs )];
    } );

    model.snr.resize( static_cast<size_t>( b ) );
    model.forwardBasis.assign( static_cast<size_t>( b ) * b, 0.0 );
    model.inverseBasis.assign( static_cast<size_t>( b ) * b, 0.0 );
    for ( int comp = 0; comp < b; ++comp )
    {
        const int ei = order[static_cast<size_t>( comp )];
        model.snr[static_cast<size_t>( comp )] = snrEigen[static_cast<size_t>( ei )];
        // forward row comp: F[comp][i] = sum_k Us[k][ei] * W[i][k]
        for ( int i = 0; i < b; ++i )
        {
            double sum = 0.0;
            for ( int k = 0; k < b; ++k )
                sum += signalVectors[static_cast<size_t>( k ) * b + ei]
                       * whitening[static_cast<size_t>( i ) * b + k];
            model.forwardBasis[static_cast<size_t>( comp ) * b + i] = sum;
        }
        // inverse column comp: C[i][comp] = sum_k Vn[i][k] * sqrt(Ln_k) * Us[k][ei]
        for ( int i = 0; i < b; ++i )
        {
            double sum = 0.0;
            for ( int k = 0; k < b; ++k )
            {
                const double vk = noiseVectors[static_cast<size_t>( i ) * b + k]
                                  * std::sqrt( noiseEigen[static_cast<size_t>( k )] );
                sum += vk * signalVectors[static_cast<size_t>( k ) * b + ei];
            }
            model.inverseBasis[static_cast<size_t>( i ) * b + comp] = sum;
        }
    }

    *out = std::move( model );
    return true;
}

void forward( const Model &model, const float *spectrum, double *out, int components )
{
    const int b = model.bandCount;
    const int count = std::min( components, b );
    for ( int comp = 0; comp < count; ++comp )
    {
        double sum = 0.0;
        const double *row = &model.forwardBasis[static_cast<size_t>( comp ) * b];
        for ( int i = 0; i < b; ++i )
            sum += row[i]
                   * ( static_cast<double>( spectrum[i] ) - model.mean[static_cast<size_t>( i )] );
        out[comp] = sum;
    }
}

void inverse( const Model &model, const double *y, const std::vector<int> &components,
              double *spectrumOut )
{
    const int b = model.bandCount;
    for ( int i = 0; i < b; ++i )
        spectrumOut[i] = model.mean[static_cast<size_t>( i )];
    if ( components.empty() )
    {
        for ( int comp = 0; comp < b; ++comp )
        {
            const double value = y[comp];
            if ( value == 0.0 )
                continue;
            for ( int i = 0; i < b; ++i )
                spectrumOut[i] +=
                    model.inverseBasis[static_cast<size_t>( i ) * b + comp] * value;
        }
        return;
    }
    for ( int comp : components )
    {
        if ( comp < 0 || comp >= b )
            continue;
        const double value = y[comp];
        for ( int i = 0; i < b; ++i )
            spectrumOut[i] += model.inverseBasis[static_cast<size_t>( i ) * b + comp] * value;
    }
}

double reconstructionRmse( const Model &model, const double *y,
                           const std::vector<int> &components )
{
    const int b = model.bandCount;
    std::vector<char> mask( static_cast<size_t>( b ), 1 );
    for ( int comp : components )
        if ( comp >= 0 && comp < b )
            mask[static_cast<size_t>( comp )] = 0;
    double sumSq = 0.0;
    for ( int i = 0; i < b; ++i )
    {
        double residual = 0.0;
        for ( int comp = 0; comp < b; ++comp )
            if ( mask[static_cast<size_t>( comp )] )
                residual += model.inverseBasis[static_cast<size_t>( i ) * b + comp]
                            * y[comp];
        sumSq += residual * residual;
    }
    return std::sqrt( sumSq / static_cast<double>( b ) );
}

void modelToJson( const Model &model, const QString &sourceInput,
                  const QString &parametersJson, qint64 createdAtMs, QJsonObject &root )
{
    root = QJsonObject();
    root[QStringLiteral( "kind" )] = kKind;
    root[QStringLiteral( "version" )] = kFormatVersion;
    root[QStringLiteral( "bandCount" )] = model.bandCount;

    auto doubleArray = []( const std::vector<double> &values ) {
        QJsonArray arr;
        for ( double v : values )
            arr.append( v );
        return arr;
    };
    root[QStringLiteral( "mean" )] = doubleArray( model.mean );
    root[QStringLiteral( "forwardBasis" )] = doubleArray( model.forwardBasis );
    root[QStringLiteral( "inverseBasis" )] = doubleArray( model.inverseBasis );
    root[QStringLiteral( "snr" )] = doubleArray( model.snr );
    root[QStringLiteral( "noiseEigenvalues" )] = doubleArray( model.noiseEigenvalues );
    if ( !model.wavelengthsNm.empty() )
    {
        QJsonArray wl;
        for ( float w : model.wavelengthsNm )
            wl.append( w );
        root[QStringLiteral( "wavelengthsNm" )] = wl;
    }

    QJsonObject prov;
    prov[QStringLiteral( "sourceOperator" )] = QStringLiteral( "rs:mnf" );
    prov[QStringLiteral( "sourceInput" )] = sourceInput;
    prov[QStringLiteral( "parameters" )] = parametersJson;
    prov[QStringLiteral( "createdAtMs" )] = createdAtMs;
    root[QStringLiteral( "provenance" )] = prov;

    root[QStringLiteral( "digest" )] = modelDigest( model );
}

bool modelFromJson( const QJsonObject &root, Model *out, QString *errorMessage )
{
    auto fail = [errorMessage]( const QString &msg ) {
        if ( errorMessage )
            *errorMessage = msg;
        return false;
    };
    if ( !out )
        return fail( QStringLiteral( "Model output pointer is null" ) );
    if ( root[QStringLiteral( "kind" )].toString() != kKind )
        return fail( QStringLiteral( "kind: expected %1, got %2" )
                         .arg( kKind,
                               root[QStringLiteral( "kind" )].toString( "<missing>" ) ) );
    if ( root[QStringLiteral( "version" )].toInt( 0 ) != kFormatVersion )
        return fail( QStringLiteral( "version: expected %1, got %2" )
                         .arg( kFormatVersion )
                         .arg( root[QStringLiteral( "version" )].toInt( 0 ) ) );

    const int b = root[QStringLiteral( "bandCount" )].toInt( 0 );
    if ( b < 2 || b > kMaxBands )
        return fail( QStringLiteral( "bandCount %1 outside [2, %2]" ).arg( b ).arg( kMaxBands ) );

    auto readDoubleArray = [&root]( const char *key, size_t expected, std::vector<double> *dst,
                                    QString *err ) -> bool {
        const QJsonArray arr = root[QLatin1String( key )].toArray();
        if ( arr.size() != static_cast<int>( expected ) )
        {
            *err = QStringLiteral( "%1: size %2 != expected %3" )
                       .arg( key )
                       .arg( arr.size() )
                       .arg( static_cast<qulonglong>( expected ) );
            return false;
        }
        dst->resize( expected );
        for ( int i = 0; i < arr.size(); ++i )
        {
            if ( !arr.at( i ).isDouble() )
            {
                *err = QStringLiteral( "%1[%2]: not a number" ).arg( key ).arg( i );
                return false;
            }
            ( *dst )[static_cast<size_t>( i )] = arr.at( i ).toDouble();
            if ( !std::isfinite( ( *dst )[static_cast<size_t>( i )] ) )
            {
                *err = QStringLiteral( "%1[%2]: non-finite" ).arg( key ).arg( i );
                return false;
            }
        }
        return true;
    };

    Model model;
    model.bandCount = b;
    QString error;
    if ( !readDoubleArray( "mean", static_cast<size_t>( b ), &model.mean, &error ) )
        return fail( error );
    const size_t b2 = static_cast<size_t>( b ) * b;
    if ( !readDoubleArray( "forwardBasis", b2, &model.forwardBasis, &error ) )
        return fail( error );
    if ( !readDoubleArray( "inverseBasis", b2, &model.inverseBasis, &error ) )
        return fail( error );
    if ( !readDoubleArray( "snr", static_cast<size_t>( b ), &model.snr, &error ) )
        return fail( error );
    if ( !readDoubleArray( "noiseEigenvalues", static_cast<size_t>( b ),
                           &model.noiseEigenvalues, &error ) )
        return fail( error );

    const QJsonArray wl = root[QStringLiteral( "wavelengthsNm" )].toArray();
    if ( !wl.isEmpty() )
    {
        if ( wl.size() != b )
            return fail( QStringLiteral( "wavelengthsNm: size %1 != bandCount %2" )
                             .arg( wl.size() )
                             .arg( b ) );
        model.wavelengthsNm.resize( static_cast<size_t>( b ) );
        for ( int i = 0; i < b; ++i )
            model.wavelengthsNm[static_cast<size_t>( i )] =
                static_cast<float>( wl.at( i ).toDouble() );
    }

    const QString storedDigest = root[QStringLiteral( "digest" )].toString();
    if ( storedDigest.isEmpty() )
        return fail( QStringLiteral( "digest: missing" ) );
    if ( modelDigest( model ) != storedDigest )
        return fail( QStringLiteral( "digest: stored %1 != computed %2 (content changed "
                                     "or corrupt)" )
                         .arg( storedDigest, modelDigest( model ) ) );

    *out = std::move( model );
    return true;
}

} // namespace MnfTransform
