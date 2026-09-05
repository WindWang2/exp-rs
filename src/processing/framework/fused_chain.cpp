// fused_chain.cpp — fused chain planning and streaming execution.
// See fused_chain.h for the contract. Adapter kernels replicate the exact
// serial math of their operators (verified bit-for-bit by
// tests/test_fused_chain.cpp against the real operators).
#include "fused_chain.h"

#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"
#include "runtime/chunk/chunk_pipeline.h"
#include "workflow/workflow_definition.h"
#include "workflow/workflow_types.h"

#include <QString>

#include <gdal.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>

namespace sicnu::processing
{
namespace
{
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr int kFusedTileDim = 256;

bool isInt( const Json::Value &v )
{
    return v.isIntegral() && !v.isNull();
}

int getInt( const Json::Value &params, const char *key, int fallback )
{
    return params.isMember( key ) && isInt( params[key] ) ? params[key].asInt() : fallback;
}

double getDouble( const Json::Value &params, const char *key, double fallback )
{
    return params.isMember( key ) && params[key].isNumeric() ? params[key].asDouble() : fallback;
}

std::string getString( const Json::Value &params, const char *key, const std::string &fallback )
{
    return params.isMember( key ) && params[key].isString() ? params[key].asString() : fallback;
}

// safeDiv replica (processing/algorithms/math_utils.cpp).
inline float safeDiv( float numerator, float denominator )
{
    return ( denominator == 0.0f ) ? kNaN : ( numerator / denominator );
}

// ---------------------------------------------------------------------------
// Adapters (fail-closed: unsupported parameter shapes return nullopt)
// ---------------------------------------------------------------------------

/// rs:spectral_index — NDVI with explicit nir/red band numbers. Replicates
/// safeDiv(nir - red, nir + red) with the operator's input conditioning
/// (band nodata / non-finite → NaN) applied by the executor's producer.
std::optional<FusedStage> makeNdviStage( const Json::Value &params )
{
    if ( getString( params, "index", "NDVI" ) != "NDVI" )
        return std::nullopt;
    if ( !isInt( params["nir"] ) || !isInt( params["red"] ) )
        return std::nullopt;
    // Scale normalization / postfire variants diverge from the replicated
    // kernel — refuse them (the operator runs normally, unfused).
    if ( params.isMember( "scale" ) || params.isMember( "numericScale" )
         || params.isMember( "postfire" ) || params.isMember( "postNir" ) )
        return std::nullopt;

    const int nirBand = params["nir"].asInt();
    const int redBand = params["red"].asInt();
    if ( nirBand < 1 || redBand < 1 )
        return std::nullopt;

    FusedStage stage;
    stage.headInputBands = { nirBand, redBand };
    stage.outputBands = 1;
    stage.tailOutputDtype = GDT_Float32;
    stage.kernel = []( const std::vector<const float *> &in, int width, int height ) {
        const float *nir = in[0];
        const float *red = in[1];
        std::vector<float> out( static_cast<size_t>( width ) * height );
        for ( size_t i = 0; i < out.size(); ++i )
            out[i] = safeDiv( nir[i] - red[i], nir[i] + red[i] );
        std::vector<std::vector<float>> planes( 1 );
        planes[0] = std::move( out );
        return planes;
    };
    return stage;
}

/// rs:threshold_raster — manual threshold method, no cleanup/MMU. Replicates
/// ChangeDetection::changeMask: NaN input → 255 (nodata), v >= threshold → 1,
/// else 0, written as GDT_Byte.
std::optional<FusedStage> makeThresholdStage( const Json::Value &params )
{
    if ( getString( params, "thresholdMethod", "manual" ) != "manual" )
        return std::nullopt;
    if ( getString( params, "cleanup", "none" ) != "none" )
        return std::nullopt;
    if ( getInt( params, "minAreaPixels", 0 ) != 0 )
        return std::nullopt;

    const double threshold = getDouble( params, "threshold", 0.5 );
    const float thresholdF = static_cast<float>( threshold );

    FusedStage stage;
    stage.headInputBands = { 1 }; // only reached when this is the head stage
    stage.outputBands = 1;
    stage.tailOutputDtype = GDT_Byte;
    stage.kernel = [thresholdF]( const std::vector<const float *> &in, int width, int height ) {
        const float *v = in[0];
        std::vector<float> out( static_cast<size_t>( width ) * height );
        for ( size_t i = 0; i < out.size(); ++i )
            out[i] = std::isnan( v[i] ) ? 255.0f : ( v[i] >= thresholdF ? 1.0f : 0.0f );
        std::vector<std::vector<float>> planes( 1 );
        planes[0] = std::move( out );
        return planes;
    };
    stage.resultExtras["thresholdUsed"] = threshold;
    return stage;
}

/// The step's raster output path (params["output"], literal or placeholder).
std::string rasterOutputOf( const Json::Value &params )
{
    return params.isMember( "output" ) && params["output"].isString() ? params["output"].asString()
                                                                      : std::string();
}

/// The step's raster input path: params["input"] must be a literal path or a
/// "$step.output" placeholder; placeholders inside a CHAIN reference the
/// previous fused step and are resolved by the chain, so planFusedChain only
/// accepts a literal path at the HEAD.
std::string rasterInputOf( const Json::Value &params )
{
    return params.isMember( "input" ) && params["input"].isString() ? params["input"].asString()
                                                                    : std::string();
}

bool isPlaceholder( const std::string &s )
{
    return !s.empty() && ( s[0] == '$' || s.find( "${" ) != std::string::npos );
}

/// Step ids whose declared output feeds @p consumerStepId via an input edge.
std::vector<std::string> producersOf( const sicnu::workflow::WorkflowDefinition &def,
                                      const std::string &consumerStepId )
{
    std::vector<std::string> out;
    for ( const auto &step : def.steps )
    {
        if ( step.id != consumerStepId )
            continue;
        for ( const auto &conn : step.inputs )
            out.push_back( conn.fromStepId );
    }
    return out;
}

int consumerCountOf( const sicnu::workflow::WorkflowDefinition &def, const std::string &stepId )
{
    int count = 0;
    for ( const auto &step : def.steps )
        for ( const auto &conn : step.inputs )
            if ( conn.fromStepId == stepId )
                ++count;
    return count;
}

const sicnu::workflow::StepDef *findStep( const sicnu::workflow::WorkflowDefinition &def,
                                          const std::string &stepId )
{
    for ( const auto &s : def.steps )
        if ( s.id == stepId )
            return &s;
    return nullptr;
}

bool isOperatorStep( const sicnu::workflow::StepDef &step )
{
    return step.kind == sicnu::workflow::StepKind::Operator && !step.operatorId.empty();
}

} // namespace

std::optional<FusedStage> makeFusedStageFor( const std::string &operatorId,
                                             const Json::Value &params )
{
    if ( operatorId == "rs:spectral_index" )
        return makeNdviStage( params );
    if ( operatorId == "rs:threshold_raster" )
        return makeThresholdStage( params );
    return std::nullopt;
}

FusedChainPlan planFusedChain( const sicnu::workflow::WorkflowDefinition &def )
{
    FusedChainPlan plan;

    std::vector<std::string> ordered;
    std::string sortError;
    if ( !sicnu::workflow::topologicalSortSteps( def, ordered, sortError ) )
        return plan;

    for ( const auto &stepId : ordered )
    {
        const sicnu::workflow::StepDef *step = findStep( def, stepId );
        if ( !step || !isOperatorStep( *step ) )
            continue;

        auto stage = makeFusedStageFor( step->operatorId, step->params );
        if ( !stage )
            continue; // this step cannot start/extend a fused chain

        // Start or extend a chain ending at the previous fused candidate.
        if ( plan.stepIds.empty() )
        {
            // Head candidate: its raster input must be a literal path.
            const std::string input = rasterInputOf( step->params );
            if ( input.empty() || isPlaceholder( input ) )
                continue;
            // Fan-out on the head's output splits the chain immediately — a
            // single consumer is required even to start.
            if ( consumerCountOf( def, stepId ) > 1 )
                continue;
            plan.stepIds.push_back( stepId );
            plan.inputPath = input;
            plan.stages.push_back( *stage );
            plan.stageParams.push_back( step->params );
            continue;
        }

        // Extend: plane arity must line up — the previous stage's output
        // plane count is exactly what this stage's kernel consumes (an
        // NDVI-after-threshold chain would read planes that do not exist).
        // Adapters express consumption as headInputBands.size() when they are
        // a head stage; for extension the requirement equals that count.
        const int requiredInputPlanes =
            static_cast<int>( stage->headInputBands.size() );
        if ( !plan.stages.empty() &&
             plan.stages.back().outputBands != requiredInputPlanes )
        {
            if ( plan.stepIds.size() >= 2 )
                return plan;
            plan = FusedChainPlan{};
            continue;
        }
        // Extend: this step must consume ONLY the previous chain tail.
        const std::string &tailId = plan.stepIds.back();
        const auto producers = producersOf( def, stepId );
        if ( producers.size() != 1 || producers[0] != tailId )
        {
            // Chain cannot extend: flush it if long enough, restart here.
            if ( plan.stepIds.size() >= 2 )
                return plan;
            plan = FusedChainPlan{};
            continue;
        }
        // The tail must ALSO not fan out beyond this consumer.
        if ( consumerCountOf( def, tailId ) != 1 )
        {
            if ( plan.stepIds.size() >= 2 )
                return plan;
            plan = FusedChainPlan{};
            continue;
        }
        plan.stepIds.push_back( stepId );
        plan.stages.push_back( *stage );
        plan.stageParams.push_back( step->params );
        if ( plan.stepIds.size() >= 2 )
            break; // one chain per submission pass (representative scope)
    }

    if ( plan.stepIds.size() < 2 )
        return FusedChainPlan{};

    plan.headStepId = plan.stepIds.front();
    plan.tailStepId = plan.stepIds.back();
    plan.tailParams = plan.stageParams.back();
    return plan;
}

namespace
{
/// Band-major float buffer layout helper: plane s at offset s * (width*height).

void runPipeline( const FusedChainPlan &plan,
                  sicnu::operators::RSOperatorContext &context,
                  const GdalDatasetWrapper &source, int width, int height,
                  GdalStreamingOutput &output )
{
    using namespace sicnu::runtime::chunk;
    std::atomic<bool> cancelFlag{ false };

    // Producer: read the head stage's bands per tile, apply the operator's
    // nodata conditioning, and hand over a band-major buffer. The grid and
    // cursor are PER-EXECUTION state captured here — a function-scope static
    // would make a second fused run start where the previous one ended
    // (silently writing zero tiles).
    const auto grid = buildTileGrid( width, height, kFusedTileDim, kFusedTileDim, 0, 1 );
    auto producer = [&source, &plan, width, height, grid, next = 0](
                        TilePayload &out ) mutable -> bool {
        if ( next >= static_cast<int>( grid.size() ) )
            return false;
        const TileSpec &t = grid[static_cast<size_t>( next++ )];
        const int bands = static_cast<int>( plan.stages.front().headInputBands.size() );
        TileSpec spec = t;
        spec.bands = bands;
        auto buffer = std::make_shared<std::vector<float>>( spec.bufferElementCount() );
        const size_t planeSize = static_cast<size_t>( t.width ) * t.height;
        for ( int b = 0; b < bands; ++b )
        {
            float *dst = buffer->data() + static_cast<size_t>( b ) * planeSize;
            if ( !source.readBandWindow( plan.stages.front().headInputBands[static_cast<size_t>( b )],
                                         t.xOffset, t.yOffset, t.width, t.height, dst ) )
                throw std::runtime_error( "fused chain: failed to read input band " +
                                          std::to_string( b ) );
            bool hasNd = false;
            const double nd = source.bandNoDataValue(
                plan.stages.front().headInputBands[static_cast<size_t>( b )], &hasNd );
            if ( hasNd && std::isfinite( nd ) )
            {
                const float ndF = static_cast<float>( nd );
                for ( size_t i = 0; i < planeSize; ++i )
                    if ( dst[i] == ndF || !std::isfinite( dst[i] ) )
                        dst[i] = kNaN;
            }
            else
            {
                for ( size_t i = 0; i < planeSize; ++i )
                    if ( !std::isfinite( dst[i] ) )
                        dst[i] = kNaN;
            }
        }
        out = TilePayload{ spec, std::move( buffer ) };
        return true;
    };

    // Stages: deinterleave planes → kernel → reinterleave.
    std::vector<ChunkPipeline::StageFn> stages;
    stages.reserve( plan.stages.size() );
    for ( const FusedStage &stage : plan.stages )
    {
        stages.push_back( [&stage]( TilePayload &&p ) -> TilePayload {
            const int w = p.spec.width;
            const int h = p.spec.height;
            const size_t planeSize = static_cast<size_t>( w ) * h;
            std::vector<const float *> inPlanes( static_cast<size_t>( p.spec.bands ) );
            for ( int b = 0; b < p.spec.bands; ++b )
                inPlanes[static_cast<size_t>( b )] =
                    p.pixels->data() + static_cast<size_t>( b ) * planeSize;
            std::vector<std::vector<float>> outPlanes = stage.kernel( inPlanes, w, h );
            TilePayload result = p; // copy spec, replace buffer below
            result.spec.bands = static_cast<int>( outPlanes.size() );
            auto merged = std::make_shared<std::vector<float>>();
            merged->reserve( planeSize * outPlanes.size() );
            for ( const auto &plane : outPlanes )
                merged->insert( merged->end(), plane.begin(), plane.end() );
            result.pixels = std::move( merged );
            return result;
        } );
    }

    // Consumer: write the tail planes onto the output raster.
    const int tailDtype = plan.stages.back().tailOutputDtype;
    auto consumer = [&output, tailDtype]( TilePayload &&p ) -> bool {
        GdalBlockStream::Tile tile;
        tile.index = p.spec.index;
        tile.totalTiles = p.spec.totalTiles;
        tile.xOffset = p.spec.xOffset;
        tile.yOffset = p.spec.yOffset;
        tile.width = p.spec.width;
        tile.height = p.spec.height;
        tile.halo = 0;
        tile.bufferWidth = p.spec.width;
        tile.bufferHeight = p.spec.height;
        tile.rasterWidth = p.spec.rasterWidth;
        tile.rasterHeight = p.spec.rasterHeight;
        const size_t planeSize = static_cast<size_t>( p.spec.width ) * p.spec.height;
        for ( int b = 0; b < p.spec.bands; ++b )
        {
            const float *plane = p.pixels->data() + static_cast<size_t>( b ) * planeSize;
            if ( tailDtype == GDT_Byte )
            {
                std::vector<uint8_t> bytes( planeSize );
                for ( size_t i = 0; i < planeSize; ++i )
                    bytes[i] = static_cast<uint8_t>( plane[i] );
                if ( !output.writeTileRaw( b + 1, tile, bytes.data(), GDT_Byte ) )
                    throw std::runtime_error( "fused chain: failed to write output tile" );
            }
            else
            {
                if ( !output.writeTile( b + 1, tile, plane ) )
                    throw std::runtime_error( "fused chain: failed to write output tile" );
            }
        }
        return true;
    };

    ChunkPipeline::Config cfg;
    cfg.queueCapacity = 2;
    ChunkPipeline pipeline( producer, std::move( stages ), consumer, cfg );
    pipeline.setProgressCallback( [&context]( double p ) {
        context.reportProgress( 0.05 + 0.9 * p, "fused chain" );
    } );
    pipeline.run();
}

} // namespace

Json::Value executeFusedChain( const FusedChainPlan &plan,
                               sicnu::operators::RSOperatorContext &context )
{
    if ( plan.stepIds.size() < 2 || plan.stages.size() != plan.stepIds.size() )
        throw std::runtime_error( "fused chain: invalid plan" );

    ensureGdalInit();
    GdalDatasetWrapper source;
    if ( !source.open( QString::fromStdString( plan.inputPath ) ) )
        throw std::runtime_error( "fused chain: cannot open input " + plan.inputPath );

    const int width = source.width();
    const int height = source.height();
    int maxBand = 0;
    for ( int band : plan.stages.front().headInputBands )
        maxBand = std::max( maxBand, band );
    if ( source.bandCount() < maxBand )
        throw std::runtime_error( "fused chain: input lacks band " + std::to_string( maxBand ) );

    GdalStreamingOutput output( QString::fromStdString( rasterOutputOf( plan.tailParams ) ),
                                width, height, plan.stages.back().outputBands,
                                plan.stages.back().tailOutputDtype, source.geoTransform(),
                                source.projection() );
    if ( !output.isOpen() )
        throw std::runtime_error( "fused chain: cannot create output" );
    output.setNoDataValue( plan.stages.back().tailOutputDtype == GDT_Byte ? 255.0 : kNaN );

    try
    {
        runPipeline( plan, context, source, width, height, output );
    }
    catch ( ... )
    {
        output.abandon(); // #647 contract: never leave partial output behind
        throw;
    }
    QString writeError;
    if ( !output.closeWithError( &writeError ) )
        throw std::runtime_error( "fused chain: output close failed: " + writeError.toStdString() );
    context.reportProgress( 1.0, "fused chain complete" );

    Json::Value result;
    result["output"] = rasterOutputOf( plan.tailParams );
    Json::Value fused( Json::arrayValue );
    for ( const auto &id : plan.stepIds )
        fused.append( id );
    result["fused"] = fused;
    result["executionFingerprintContext"] = "fused_chain";
    if ( plan.stages.back().resultExtras.isObject() )
        for ( const auto &key : plan.stages.back().resultExtras.getMemberNames() )
            result[key] = plan.stages.back().resultExtras[key];
    return result;
}

} // namespace sicnu::processing
