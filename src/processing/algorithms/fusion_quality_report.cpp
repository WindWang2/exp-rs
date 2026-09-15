// fusion_quality_report.cpp — F15 Package F implementation (ADR 0163).
#include "fusion_quality_report.h"

#include "processing/algorithms/pansharpening.h"

#include <QFile>
#include <QString>

#include <json/json.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace rs::fusion {
namespace {

constexpr int kQWindow = 8;

// Wang & Bovik universal image quality index over non-overlapping 8×8
// windows; incomplete edge windows fall back to a single global window for
// tiny inputs. L (dynamic range) is derived from the reference band.
double universalQIndex( const float *x, const float *y, int width, int height )
{
    double refMax = 0.0;
    const size_t n = static_cast<size_t>( width ) * height;
    for ( size_t i = 0; i < n; ++i )
    {
        if ( std::isfinite( y[i] ) )
            refMax = std::max( refMax, std::abs( static_cast<double>( y[i] ) ) );
    }
    const double L = std::max( 1.0, refMax );
    const double C1 = 0.01 * 0.01 * L * L;
    const double C2 = 0.03 * 0.03 * L * L;

    double total = 0.0;
    int64_t windows = 0;
    for ( int r0 = 0; r0 + kQWindow <= height || r0 == 0; r0 += kQWindow )
    {
        const int rh = std::min( kQWindow, height - r0 );
        if ( rh <= 0 )
            break;
        for ( int c0 = 0; c0 + kQWindow <= width || c0 == 0; c0 += kQWindow )
        {
            const int cw = std::min( kQWindow, width - c0 );
            if ( cw <= 0 )
                break;
            double mx = 0, my = 0;
            int64_t cnt = 0;
            for ( int r = r0; r < r0 + rh; ++r )
            {
                for ( int c = c0; c < c0 + cw; ++c )
                {
                    const double xv = x[static_cast<size_t>( r ) * width + c];
                    const double yv = y[static_cast<size_t>( r ) * width + c];
                    if ( std::isfinite( xv ) && std::isfinite( yv ) )
                    {
                        mx += xv;
                        my += yv;
                        ++cnt;
                    }
                }
            }
            if ( cnt < 2 )
                continue;
            mx /= cnt;
            my /= cnt;
            double vxx = 0, vyy = 0, vxy = 0;
            for ( int r = r0; r < r0 + rh; ++r )
            {
                for ( int c = c0; c < c0 + cw; ++c )
                {
                    const double xv = x[static_cast<size_t>( r ) * width + c];
                    const double yv = y[static_cast<size_t>( r ) * width + c];
                    if ( std::isfinite( xv ) && std::isfinite( yv ) )
                    {
                        const double dx = xv - mx;
                        const double dy = yv - my;
                        vxx += dx * dx;
                        vyy += dy * dy;
                        vxy += dx * dy;
                    }
                }
            }
            vxx /= ( cnt - 1 );
            vyy /= ( cnt - 1 );
            vxy /= ( cnt - 1 );
            const double denom = ( mx * mx + my * my + C1 ) * ( vxx + vyy + C2 );
            total += ( ( 2 * mx * my + C1 ) * ( 2 * vxy + C2 ) ) / denom;
            ++windows;
        }
        if ( r0 + kQWindow > height && r0 != 0 )
            break;
    }
    return windows > 0 ? total / windows : 0.0;
}

std::string number( double v )
{
    if ( !std::isfinite( v ) )
        return "null";
    char buf[32];
    std::snprintf( buf, sizeof( buf ), "%.6g", v );
    return buf;
}

} // namespace

std::string FusionQualityReport::toJson() const
{
    Json::Value root( Json::objectValue );
    root["schema"] = "exp-rs/fusion-quality-report@1";
    Json::Value wald( Json::objectValue );
    wald["ergas"] = ergas;
    wald["meanCc"] = meanCc;
    wald["rmse"] = rmse;
    wald["ssim"] = ssim;
    root["wald"] = wald;
    Json::Value indices( Json::objectValue );
    indices["q"] = qIndex;
    indices["rase"] = rase;
    root["indices"] = indices;
    Json::Value distortion( Json::objectValue );
    Json::Value meanR( Json::arrayValue );
    Json::Value stdR( Json::arrayValue );
    for ( double v : meanRatio )
        meanR.append( v );
    for ( double v : stdRatio )
        stdR.append( v );
    distortion["meanRatio"] = meanR;
    distortion["stdRatio"] = stdR;
    root["spectralDistortion"] = distortion;
    Json::Value verdict( Json::objectValue );
    verdict["passed"] = passed;
    Json::Value vio( Json::arrayValue );
    for ( const std::string &v : violations )
        vio.append( v );
    verdict["violations"] = vio;
    root["verdict"] = verdict;

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    return Json::writeString( builder, root );
}

// ---- Streaming accumulator -------------------------------------------------

void FusionQualityAccumulator::beginBand()
{
    band_ = BandAccum{};
}

void FusionQualityAccumulator::addWindow( const float *x, const float *y, int w, int h )
{
    if ( !x || !y || w <= 0 || h <= 0 )
        return;
    const size_t n = static_cast<size_t>( w ) * h;
    for ( size_t i = 0; i < n; ++i )
    {
        const double xv = x[i];
        const double yv = y[i];
        if ( std::isfinite( xv ) && std::isfinite( yv ) )
        {
            band_.sx += xv;
            band_.sy += yv;
            band_.sxx += xv * xv;
            band_.syy += yv * yv;
            band_.sxy += xv * yv;
            band_.mse += ( xv - yv ) * ( xv - yv );
            band_.maxY = std::max( band_.maxY, std::abs( yv ) );
            ++band_.n;
        }
    }
    // Q over non-overlapping 8×8 blocks of this window. The window-local
    // dynamic range drives the C1/C2 stabilizers (identical to the in-memory
    // path when the whole image is fed as one window).
    band_.qSum += universalQIndex( x, y, w, h );
    ++band_.qWindows;
}

void FusionQualityAccumulator::endBand()
{
    bands_.push_back( band_ );
    band_ = BandAccum{};
}

FusionQualityReport FusionQualityAccumulator::finalize( int bandCount, int width, int height,
                                                        double scaleRatio,
                                                        const FusionQualityThresholds &thresholds )
{
    FusionQualityReport report;
    auto violate = [&report]( const std::string &m ) { report.violations.push_back( m ); };

    if ( width <= 0 || height <= 0 )
        violate( "invalid dimensions" );
    if ( bandCount <= 0 )
        violate( "no bands provided" );
    if ( static_cast<int>( bands_.size() ) != bandCount )
        violate( "band count mismatch between fused and reference (" +
                 std::to_string( bands_.size() ) + " vs " + std::to_string( bandCount ) + ")" );
    if ( !report.violations.empty() )
    {
        report.passed = false;
        return report;
    }

    const size_t n = static_cast<size_t>( width ) * height;
    double mseSum = 0.0, qSum = 0.0, refMeanSum = 0.0;
    int qBands = 0;
    double ergasTermSum = 0.0;
    double ccSum = 0.0;
    int ergasBands = 0;
    report.meanRatio.assign( static_cast<size_t>( bandCount ), 1.0 );
    report.stdRatio.assign( static_cast<size_t>( bandCount ), 1.0 );
    double rmseSum = 0.0;

    for ( int b = 0; b < bandCount; ++b )
    {
        const BandAccum &acc = bands_[static_cast<size_t>( b )];
        if ( acc.n == 0 )
        {
            violate( "band " + std::to_string( b ) + " has no valid pixels" );
            continue;
        }
        const double mx = acc.sx / acc.n;
        const double my = acc.sy / acc.n;
        const double vx = std::max( 0.0, acc.sxx / acc.n - mx * mx );
        const double vy = std::max( 0.0, acc.syy / acc.n - my * my );
        const double cov = acc.sxy / acc.n - mx * my;
        const double rmse = std::sqrt( acc.mse / acc.n );
        report.meanRatio[static_cast<size_t>( b )] = my != 0.0 ? mx / my : 1.0;
        report.stdRatio[static_cast<size_t>( b )] =
            vy > 1e-300 ? std::sqrt( vx ) / std::sqrt( vy ) : 1.0;
        refMeanSum += my;
        mseSum += acc.mse;
        rmseSum += rmse;
        if ( acc.qWindows > 0 )
        {
            qSum += acc.qSum / acc.qWindows;
            ++qBands;
        }
        if ( std::abs( my ) > 1e-12 )
        {
            ergasTermSum += ( rmse / my ) * ( rmse / my );
            ++ergasBands;
        }
        // Pearson CC from the accumulated moments. Zero-variance inputs
        // (constant bands) make CC undefined: treat exact agreement as CC=1
        // and any disagreement as CC=0 so degenerate rasters are judged
        // honestly instead of tripping the guard.
        const double denom = std::sqrt( vx * vy );
        if ( denom > 1e-300 )
            ccSum += cov / denom;
        else
            ccSum += std::sqrt( acc.mse / acc.n ) <= 1e-9 * ( 1.0 + std::abs( my ) ) ? 1.0
                                                                                    : 0.0;
    }

    const double bandCountD = static_cast<double>( bandCount );
    report.rmse = rmseSum / bandCountD;
    report.meanCc = ccSum / bandCountD;
    report.qIndex = qBands > 0 ? qSum / qBands : 0.0;
    report.ergas = ergasBands > 0
                       ? 100.0 * scaleRatio * std::sqrt( ergasTermSum / ergasBands )
                       : 0.0;
    // SSIM global form per band, averaged (dynamic range L = per-band running
    // max of the reference, the standard stabilizer choice).
    double ssimSum = 0.0;
    for ( int b = 0; b < bandCount; ++b )
    {
        const BandAccum &acc = bands_[static_cast<size_t>( b )];
        if ( acc.n == 0 )
            continue;
        const double L = std::max( 1.0, acc.maxY );
        const double C1 = ( 0.01 * L ) * ( 0.01 * L );
        const double C2 = ( 0.03 * L ) * ( 0.03 * L );
        const double mx = acc.sx / acc.n;
        const double my = acc.sy / acc.n;
        const double vx = std::max( 0.0, acc.sxx / acc.n - mx * mx );
        const double vy = std::max( 0.0, acc.syy / acc.n - my * my );
        const double cov = acc.sxy / acc.n - mx * my;
        ssimSum += ( ( 2 * mx * my + C1 ) * ( 2 * cov + C2 ) ) /
                   ( ( mx * mx + my * my + C1 ) * ( vx + vy + C2 ) );
    }
    report.ssim = ssimSum / bandCountD;

    const double refMean = refMeanSum / bandCountD;
    if ( std::abs( refMean ) > 1e-12 )
        report.rase = 100.0 / refMean * report.rmse;

    if ( report.ergas > thresholds.maxErgas )
        violate( "ERGAS " + number( report.ergas ) + " exceeds the limit " +
                 number( thresholds.maxErgas ) );
    if ( report.meanCc < thresholds.minMeanCc )
        violate( "mean CC " + number( report.meanCc ) + " below the floor " +
                 number( thresholds.minMeanCc ) );
    if ( report.qIndex < thresholds.minQ )
        violate( "Q index " + number( report.qIndex ) + " below the floor " +
                 number( thresholds.minQ ) );
    for ( int b = 0; b < bandCount; ++b )
    {
        if ( std::abs( report.meanRatio[static_cast<size_t>( b )] - 1.0 ) >
             thresholds.maxMeanRatioDeviation )
            violate( "band " + std::to_string( b ) + " mean ratio " +
                     number( report.meanRatio[static_cast<size_t>( b )] ) +
                     " deviates more than " + number( thresholds.maxMeanRatioDeviation ) +
                     " from 1 (spectral shift)" );
        if ( std::abs( report.stdRatio[static_cast<size_t>( b )] - 1.0 ) >
             thresholds.maxStdRatioDeviation )
            violate( "band " + std::to_string( b ) + " std ratio " +
                     number( report.stdRatio[static_cast<size_t>( b )] ) +
                     " deviates more than " + number( thresholds.maxStdRatioDeviation ) +
                     " from 1 (contrast drift)" );
    }
    report.passed = report.violations.empty();
    return report;
}

FusionQualityReport evaluateFusionQuality( const std::vector<const float *> &degradedFusedBands,
                                           const std::vector<const float *> &originalMsBands,
                                           int width, int height, double scaleRatio,
                                           const FusionQualityThresholds &thresholds )
{
    // Degenerate inputs surface as violations through the same finalize path.
    if ( degradedFusedBands.size() != originalMsBands.size() || degradedFusedBands.empty() ||
         width <= 0 || height <= 0 )
    {
        FusionQualityReport report;
        if ( width <= 0 || height <= 0 )
            report.violations.push_back( "invalid dimensions" );
        else if ( degradedFusedBands.empty() )
            report.violations.push_back( "no bands provided" );
        else
            report.violations.push_back( "band count mismatch between fused and reference (" +
                                         std::to_string( degradedFusedBands.size() ) + " vs " +
                                         std::to_string( originalMsBands.size() ) + ")" );
        report.passed = false;
        return report;
    }

    FusionQualityAccumulator acc;
    for ( size_t b = 0; b < degradedFusedBands.size(); ++b )
    {
        acc.beginBand();
        acc.addWindow( degradedFusedBands[b], originalMsBands[b], width, height );
        acc.endBand();
    }
    return acc.finalize( static_cast<int>( degradedFusedBands.size() ), width, height,
                         scaleRatio, thresholds );
}

bool writeTextFileAtomic( const std::string &path, const std::string &content,
                          std::string *errorMessage )
{
    // Write to a sibling temp file, then rename over the target: readers of
    // `path` only ever see a complete file, and a crash leaves the previous
    // artifact (or nothing) behind — never a truncated one.
    const QString qPath = QString::fromStdString( path );
    const QString tmpPath = qPath + QStringLiteral( ".tmp" );

    QFile file( tmpPath );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
    {
        if ( errorMessage )
            *errorMessage = "cannot open " + tmpPath.toStdString() + " for writing";
        return false;
    }
    const QByteArray bytes( content.data(), static_cast<qsizetype>( content.size() ) );
    if ( file.write( bytes ) != bytes.size() )
    {
        file.close();
        file.remove();
        if ( errorMessage )
            *errorMessage = "short write on " + tmpPath.toStdString();
        return false;
    }
    file.flush();
    file.close();

    // POSIX ::rename replaces the target atomically (no reader-visible gap).
    if ( file.rename( qPath ) )
        return true;
    // Windows-style fallback: rename cannot replace an existing file there,
    // so remove it first (a brief non-atomic window is unavoidable on that
    // platform and only when the target already existed).
    if ( QFile::exists( qPath ) && !QFile::remove( qPath ) )
    {
        if ( errorMessage )
            *errorMessage = "cannot replace existing " + path;
        return false;
    }
    if ( !file.rename( qPath ) )
    {
        QFile::remove( tmpPath );
        if ( errorMessage )
            *errorMessage = "rename " + tmpPath.toStdString() + " -> " + path + " failed";
        return false;
    }
    return true;
}

} // namespace rs::fusion
