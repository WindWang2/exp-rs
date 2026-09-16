// sar_pair_network.cpp — see sar_pair_network.h
#include "sar_pair_network.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace sicnu::sar
{

namespace
{
constexpr int kMaxScenes = 512;
constexpr long long kMaxPairs = 65536;

/// Union-find with path compression (deterministic).
struct UnionFind
{
    std::vector<int> parent;
    explicit UnionFind( int n ) : parent( n )
    {
        std::iota( parent.begin(), parent.end(), 0 );
    }
    int find( int x )
    {
        while ( parent[x] != x )
        {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    }
    void unite( int a, int b )
    {
        const int ra = find( a );
        const int rb = find( b );
        if ( ra != rb )
            parent[std::max( ra, rb )] = std::min( ra, rb );
    }
};

/// Screening B⊥ at the master scene's orbit mid-time (see header).
bool screeningBaseline( const InSarSceneTruth &master, const InSarSceneTruth &slave,
                        InSarNetworkPair *pairOut, QString *error )
{
    const double tMid =
        0.5 * ( master.orbit.startTime() + master.orbit.endTime() );
    double mx = 0.0, my = 0.0, mz = 0.0, mvx = 0.0, mvy = 0.0, mvz = 0.0;
    if ( !interpolateState( master.orbit, tMid, &mx, &my, &mz, &mvx, &mvy, &mvz ) )
    {
        if ( error )
            *error = QStringLiteral( "BASELINE_STATE_INTERPOLATION_FAILED: master orbit "
                                     "mid-time %1 failed interpolation" )
                         .arg( tMid );
        return false;
    }
    double sx = 0.0, sy = 0.0, sz = 0.0, svx = 0.0, svy = 0.0, svz = 0.0;
    if ( !interpolateState( slave.orbit, tMid, &sx, &sy, &sz, &svx, &svy, &svz ) )
    {
        if ( error )
            *error = QStringLiteral( "BASELINE_STATE_INTERPOLATION_FAILED: slave orbit "
                                     "does not bracket the master mid-time %1 — the "
                                     "acquisitions cannot be screened as a pair" )
                         .arg( tMid );
        return false;
    }

    // Nadir ground point of the master position (height 0), radial LOS.
    double latDeg = 0.0, lonDeg = 0.0, heightM = 0.0;
    Wgs84::ecefToGeodetic( mx, my, mz, &latDeg, &lonDeg, &heightM );
    double gx = 0.0, gy = 0.0, gz = 0.0;
    Wgs84::geodeticToEcef( latDeg, lonDeg, 0.0, &gx, &gy, &gz );
    const double dx = mx - gx, dy = my - gy, dz = mz - gz;
    const double norm = std::sqrt( dx * dx + dy * dy + dz * dz );
    if ( !( norm > 0.0 ) )
    {
        if ( error )
            *error = QStringLiteral( "BASELINE_STATE_INTERPOLATION_FAILED: degenerate "
                                     "nadir LOS" );
        return false;
    }

    InterferometricBaseline baseline;
    if ( !interferometricBaseline( mx, my, mz, sx, sy, sz, dx / norm, dy / norm, dz / norm,
                                   &baseline ) )
    {
        if ( error )
            *error = QStringLiteral( "BASELINE_STATE_INTERPOLATION_FAILED: "
                                     "interferometricBaseline refused the pair geometry" );
        return false;
    }
    pairOut->perpendicularM = baseline.perpendicularM;
    pairOut->parallelM = baseline.parallelM;
    pairOut->magnitudeM = baseline.magnitudeM;
    return true;
}

bool failMsg( QString *error, const QString &code, const QString &message )
{
    if ( error )
        *error = code + QStringLiteral( ": " ) + message;
    return false;
}
} // namespace

bool buildPairNetwork( const std::vector<InSarSceneTruth> &scenes,
                       const PairNetworkParams &params,
                       PairNetworkResult *out, QString *error )
{
    if ( out == nullptr )
        return false;
    if ( scenes.size() < 2 )
        return failMsg( error, QStringLiteral( "SCENE_TRUTH_INVALID" ),
                        QStringLiteral( "a pair network needs at least 2 scenes" ) );
    if ( scenes.size() > kMaxScenes )
        return failMsg( error, QStringLiteral( "SCENE_TRUTH_INVALID" ),
                        QStringLiteral( "%1 scenes exceed the %2-scene network bound" )
                            .arg( scenes.size() )
                            .arg( kMaxScenes ) );
    if ( params.referenceIdx < 0 || params.referenceIdx >= static_cast<int>( scenes.size() ) )
        return failMsg( error, QStringLiteral( "SCENE_TRUTH_INVALID" ),
                        QStringLiteral( "referenceIdx %1 out of range" )
                            .arg( params.referenceIdx ) );

    // Whole-stack truth validation (fail-closed on ANY scene).
    for ( size_t i = 0; i < scenes.size(); ++i )
        if ( !validateSceneTruth( scenes[i], error ) )
            return false; // error already domain-coded

    // Wavelength consistency against the reference scene.
    const double refWavelength = scenes[static_cast<size_t>( params.referenceIdx )].wavelengthUm;
    for ( size_t i = 0; i < scenes.size(); ++i )
    {
        const double relDiff = std::abs( scenes[i].wavelengthUm - refWavelength )
                               / std::max( scenes[i].wavelengthUm, refWavelength );
        if ( relDiff > 1e-9 )
            return failMsg( error, QStringLiteral( "WAVELENGTH_INCOMPATIBLE" ),
                            QStringLiteral( "scene %1 (%2 µm) disagrees with the "
                                            "reference scene (%3 µm) beyond 1e-9 "
                                            "relative" )
                                .arg( i )
                                .arg( scenes[i].wavelengthUm )
                                .arg( refWavelength ) );
    }

    // Candidate pairs by strategy, constraint-filtered.
    std::vector<InSarNetworkPair> pairs;
    const int n = static_cast<int>( scenes.size() );
    auto considerPair = [&]( int masterIdx, int slaveIdx ) -> bool {
        InSarNetworkPair pair;
        pair.masterIdx = masterIdx;
        pair.slaveIdx = slaveIdx;
        pair.temporalDays =
            ( scenes[static_cast<size_t>( slaveIdx )].acquisitionUtcSec
              - scenes[static_cast<size_t>( masterIdx )].acquisitionUtcSec )
            / 86400.0;
        if ( std::isfinite( params.maxTemporalDays )
             && std::abs( pair.temporalDays ) > params.maxTemporalDays )
            return true; // filtered, not an error

        // Absolute-window cross-check when both anchors exist (the pair
        // cannot share an imaged area otherwise).
        const InSarSceneTruth &m = scenes[static_cast<size_t>( masterIdx )];
        const InSarSceneTruth &s = scenes[static_cast<size_t>( slaveIdx )];
        if ( std::isfinite( m.azimuthStartUtcSec ) && std::isfinite( s.azimuthStartUtcSec ) )
        {
            const double mStart = m.azimuthStartUtcSec + m.orbit.startTime();
            const double mEnd = m.azimuthStartUtcSec + m.orbit.endTime();
            const double sStart = s.azimuthStartUtcSec + s.orbit.startTime();
            const double sEnd = s.azimuthStartUtcSec + s.orbit.endTime();
            if ( std::max( mStart, sStart ) >= std::min( mEnd, sEnd ) )
            {
                failMsg( error, QStringLiteral( "ORBIT_EPOCH_MISMATCH" ),
                         QStringLiteral( "scenes %1/%2 orbit windows do not overlap in "
                                         "absolute time" )
                             .arg( masterIdx )
                             .arg( slaveIdx ) );
                return false;
            }
        }

        if ( !screeningBaseline( m, s, &pair, error ) )
            return false;

        const double absPerp = std::abs( pair.perpendicularM );
        if ( std::isfinite( params.minPerpendicularM ) && absPerp < params.minPerpendicularM )
            return true; // filtered
        if ( std::isfinite( params.maxPerpendicularM ) && absPerp > params.maxPerpendicularM )
            return true; // filtered

        pairs.push_back( pair );
        return true;
    };

    if ( params.strategy == PairStrategy::AllPairs )
    {
        for ( int i = 0; i < n; ++i )
            for ( int j = i + 1; j < n; ++j )
                if ( !considerPair( i, j ) )
                    return false;
    }
    else // Consecutive: each scene to its temporal successor
    {
        for ( int i = 0; i + 1 < n; ++i )
            if ( !considerPair( i, i + 1 ) )
                return false;
    }

    if ( static_cast<long long>( pairs.size() ) > kMaxPairs )
        return failMsg( error, QStringLiteral( "SCENE_TRUTH_INVALID" ),
                        QStringLiteral( "%1 eligible pairs exceed the %2-pair network "
                                        "bound" )
                            .arg( pairs.size() )
                            .arg( kMaxPairs ) );
    if ( pairs.empty() )
        return failMsg( error, QStringLiteral( "PAIR_GRAPH_DISCONNECTED" ),
                        QStringLiteral( "no pair survived the constraints — the network "
                                        "is empty (check maxTemporalDays / perpendicular "
                                        "bounds)" ) );

    // Connectivity over the eligible pairs.
    UnionFind uf( n );
    for ( const InSarNetworkPair &pair : pairs )
        uf.unite( pair.masterIdx, pair.slaveIdx );
    std::vector<int> labels( static_cast<size_t>( n ), -1 );
    int componentCount = 0;
    for ( int i = 0; i < n; ++i )
    {
        const int root = uf.find( i );
        if ( labels[static_cast<size_t>( root )] < 0 )
            labels[static_cast<size_t>( root )] = componentCount++;
        labels[static_cast<size_t>( i )] = labels[static_cast<size_t>( root )];
    }

    out->pairs = std::move( pairs );
    out->referenceIdx = params.referenceIdx;
    out->componentCount = componentCount;
    out->componentOfScene = labels;
    out->connected = componentCount == 1;
    out->maxPerpendicularSeenM = 0.0;
    out->maxTemporalSeenDays = 0.0;
    for ( const InSarNetworkPair &pair : out->pairs )
    {
        out->maxPerpendicularSeenM =
            std::max( out->maxPerpendicularSeenM, std::abs( pair.perpendicularM ) );
        out->maxTemporalSeenDays =
            std::max( out->maxTemporalSeenDays, std::abs( pair.temporalDays ) );
    }

    if ( !out->connected && !params.allowDisconnected )
        return failMsg( error, QStringLiteral( "PAIR_GRAPH_DISCONNECTED" ),
                        QStringLiteral( "the constraint-filtered graph has %1 components "
                                        "(scene → component map available via "
                                        "allowDisconnected=true) — inversion needs one "
                                        "connected component containing the reference" )
                            .arg( componentCount ) );
    return true;
}

} // namespace sicnu::sar
