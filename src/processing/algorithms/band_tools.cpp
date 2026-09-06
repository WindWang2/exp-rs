// band_tools.cpp — File-level orchestration for the small band tools.
#include "band_tools.h"

#include "processing/algorithms/image_enhancement.h"
#include "processing/gdal/gdal_block_stream.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include <gdal.h>

#include <cmath>
#include <limits>

namespace
{
constexpr int kTileDim = ImageEnhancementStreaming::kTileDim;

bool fail( QString *errorMessage, const QString &message )
{
    if ( errorMessage )
        *errorMessage = message;
    return false;
}
}

float BandTools::bandNodata( GdalDatasetWrapper &src, int b )
{
    bool hasNd = false;
    const double nd = src.bandNoDataValue( b, &hasNd );
    return ( hasNd && std::isfinite( nd ) ) ? static_cast<float>( nd )
                                            : std::numeric_limits<float>::quiet_NaN();
}

bool BandTools::processBandRatioFile( const QString &sourcePath, const QString &outputPath,
                                      int numeratorBand, int denominatorBand,
                                      QString *errorMessage )
{
    if ( numeratorBand == denominatorBand )
        return fail( errorMessage, QStringLiteral( "分子波段与分母波段不能相同。" ) );

    GdalDatasetWrapper src;
    if ( !src.open( sourcePath ) )
        return fail( errorMessage, QStringLiteral( "无法打开输入栅格。" ) );

    const int bandCount = src.bandCount();
    if ( numeratorBand < 1 || numeratorBand > bandCount
         || denominatorBand < 1 || denominatorBand > bandCount )
        return fail( errorMessage, QStringLiteral( "波段序号超出范围（1–%1）。" ).arg( bandCount ) );

    // Stream only the involved band pair (BIP tiles) — O(tile) memory, so the
    // legacy dialog's 2 GiB soft cap (silent rejection of large scenes) is gone.
    const std::vector<int> pair = { numeratorBand, denominatorBand };
    GdalStreamingOutput dst( outputPath, src.width(), src.height(), 1, GDT_Float32,
                             src.geoTransform(), src.projection() );
    if ( !dst.isOpen() )
        return fail( errorMessage, QStringLiteral( "无法创建输出栅格。" ) );

    GdalMultibandBlockStream stream( src, pair, kTileDim, kTileDim );
    std::vector<float> numerator( static_cast<size_t>( kTileDim ) * kTileDim );
    std::vector<float> denominator( static_cast<size_t>( kTileDim ) * kTileDim );
    std::vector<float> out( static_cast<size_t>( kTileDim ) * kTileDim );

    const bool ok = stream.forEach( [&]( const GdalBlockStream::Tile & tile, const float *bip ) {
        const size_t n = static_cast<size_t>( tile.width ) * tile.height;
        for ( size_t i = 0; i < n; ++i )
        {
            numerator[i] = bip[i * 2];
            denominator[i] = bip[i * 2 + 1];
        }
        ImageEnhancementStreaming::bandRatioTile( numerator.data(), denominator.data(),
                                                  out.data(), n );
        return dst.writeTile( 1, tile, out.data() );
    } );
    if ( !ok )
    {
        dst.abandon();
        return fail( errorMessage, QStringLiteral( "波段比值计算失败。" ) );
    }

    QString closeError;
    if ( !dst.closeWithError( &closeError ) )
        return fail( errorMessage, closeError );
    return true;
}

bool BandTools::processRgbToIhsFile( const QString &sourcePath, const QString &outputPath,
                                     int redBand, int greenBand, int blueBand,
                                     QString *errorMessage )
{
    GdalDatasetWrapper src;
    if ( !src.open( sourcePath ) )
        return fail( errorMessage, QStringLiteral( "无法打开输入栅格。" ) );

    const int bandCount = src.bandCount();
    if ( redBand < 1 || redBand > bandCount || greenBand < 1 || greenBand > bandCount
         || blueBand < 1 || blueBand > bandCount )
        return fail( errorMessage, QStringLiteral( "波段序号超出范围（1–%1）。" ).arg( bandCount ) );

    // Resolve the three bands' declared sentinels so IHS masks real NoData
    // (panel #380 semantics) instead of propagating garbage components.
    const std::vector<int> triple = { redBand, greenBand, blueBand };
    const float ndR = bandNodata( src, redBand );
    const float ndG = bandNodata( src, greenBand );
    const float ndB = bandNodata( src, blueBand );

    GdalStreamingOutput dst( outputPath, src.width(), src.height(), 3, GDT_Float32,
                             src.geoTransform(), src.projection() );
    if ( !dst.isOpen() )
        return fail( errorMessage, QStringLiteral( "无法创建输出栅格。" ) );

    GdalMultibandBlockStream stream( src, triple, kTileDim, kTileDim );
    std::vector<float> outI( static_cast<size_t>( kTileDim ) * kTileDim );
    std::vector<float> outH( static_cast<size_t>( kTileDim ) * kTileDim );
    std::vector<float> outS( static_cast<size_t>( kTileDim ) * kTileDim );

    const bool ok = stream.forEach( [&]( const GdalBlockStream::Tile & tile, const float *bip ) {
        const size_t n = static_cast<size_t>( tile.width ) * tile.height;
        ImageEnhancementStreaming::ihsTransformTile( bip, ndR, ndG, ndB,
                                                     outI.data(), outH.data(), outS.data(), n );
        return dst.writeTile( 1, tile, outI.data() )
               && dst.writeTile( 2, tile, outH.data() )
               && dst.writeTile( 3, tile, outS.data() );
    } );
    if ( !ok )
    {
        dst.abandon();
        return fail( errorMessage, QStringLiteral( "IHS 变换失败。" ) );
    }

    QString closeError;
    if ( !dst.closeWithError( &closeError ) )
        return fail( errorMessage, closeError );
    return true;
}

bool BandTools::processExtractBandsFile( const QString &sourcePath, const QString &outputPath,
                                         const QVector<int> &bands,
                                         QString *errorMessage )
{
    if ( bands.isEmpty() )
        return fail( errorMessage, QStringLiteral( "请至少选择一个波段。" ) );

    GdalDatasetWrapper src;
    if ( !src.open( sourcePath ) )
        return fail( errorMessage, QStringLiteral( "无法打开输入栅格。" ) );

    const int bandCount = src.bandCount();
    for ( int b : bands )
    {
        if ( b < 1 || b > bandCount )
            return fail( errorMessage, QStringLiteral( "波段序号超出范围（1–%1）。" ).arg( bandCount ) );
    }

    // Pure band copy: stream each requested band tile-by-tile into its output
    // plane (georeference copied from the source; output Float32, matching the
    // legacy single-band dialog path).
    GdalStreamingOutput dst( outputPath, src.width(), src.height(), bands.size(), GDT_Float32,
                             src.geoTransform(), src.projection() );
    if ( !dst.isOpen() )
        return fail( errorMessage, QStringLiteral( "无法创建输出栅格。" ) );

    int outBand = 0;
    for ( int b : bands )
    {
        ++outBand;
        GdalBlockStream stream( src, b, kTileDim, kTileDim );
        const bool ok = stream.forEach( [&]( const GdalBlockStream::Tile & tile, const float *pixels ) {
            return dst.writeTile( outBand, tile, pixels );
        } );
        if ( !ok )
        {
            dst.abandon();
            return fail( errorMessage, QStringLiteral( "提取波段 %1 失败。" ).arg( b ) );
        }
    }

    QString closeError;
    if ( !dst.closeWithError( &closeError ) )
        return fail( errorMessage, closeError );
    return true;
}

bool BandTools::processContrastStretchFile( const QString &sourcePath, const QString &outputPath,
                                            const StretchSpec &spec,
                                            QString *errorMessage )
{
    GdalDatasetWrapper src;
    if ( !src.open( sourcePath ) )
        return fail( errorMessage, QStringLiteral( "无法打开输入栅格。" ) );

    const int bandCount = src.bandCount();
    GdalStreamingOutput dst( outputPath, src.width(), src.height(), bandCount, GDT_Float32,
                             src.geoTransform(), src.projection() );
    if ( !dst.isOpen() )
        return fail( errorMessage, QStringLiteral( "无法创建输出栅格。" ) );

    ImageEnhancementStreaming::StretchParams params;
    params.kind = spec.kind;
    params.clipPercent = spec.clipPercent;
    params.stddevK = spec.stddevK;
    params.piecewisePoints = spec.piecewisePoints;

    for ( int b = 1; b <= bandCount; ++b )
    {
        QString bandError;
        if ( !ImageEnhancementStreaming::streamBandStretch( src, b, bandNodata( src, b ), params,
                                                            dst, kTileDim, &bandError ) )
        {
            dst.abandon();
            return fail( errorMessage, bandError );
        }
    }

    QString closeError;
    if ( !dst.closeWithError( &closeError ) )
        return fail( errorMessage, closeError );
    return true;
}
