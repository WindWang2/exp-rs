#include "preflight/asset_state_adapter.h"

#include <algorithm>

namespace sicnu::preflight {

SlotFacts projectAssetState( const sicnu::state::RemoteSensingAssetState &state )
{
    SlotFacts f;
    f.assetId = state.assetId;
    f.kind = sicnu::state::assetKindToString( state.kind );
    f.modality = sicnu::state::modalityToString( state.sensor.modality );

    // Geometry.
    f.hasCrs = state.geometry.hasCrs;
    f.crsAuthid = state.geometry.crsAuthid;
    f.crsWkt = state.geometry.crsWkt;
    f.crsProjected = state.geometry.crsProjected;
    f.hasPixelSize = state.geometry.hasPixelSize;
    f.pixelSizeX = state.geometry.pixelSizeX;
    f.pixelSizeY = state.geometry.pixelSizeY;
    f.hasSize = state.geometry.hasSize;
    f.width = state.geometry.width;
    f.height = state.geometry.height;
    f.hasExtent = state.geometry.hasExtent;
    f.extentMinX = state.geometry.minX;
    f.extentMinY = state.geometry.minY;
    f.extentMaxX = state.geometry.maxX;
    f.extentMaxY = state.geometry.maxY;

    // Radiometric state (normalized unit vocabulary owned by the resolver).
    f.radiometricUnit = state.radiometric.unit;

    // Validity / quality.
    f.noDataPolicy = state.validity.noDataPolicy;
    f.hasCloudCover = state.validity.hasCloudCover;
    f.cloudCoverPercent = state.validity.cloudCoverPercent;
    f.qualityMaskInfo = state.validity.qualityMaskInfo;

    // Bands (the passport keeps them sorted by index).
    for ( const auto &band : state.bands )
    {
        BandFacts out;
        out.index = band.index;
        out.role = band.role;
        out.hasWavelengthNm = band.hasWavelengthNm;
        out.wavelengthNm = band.wavelengthNm;
        out.dataType = band.dataType;
        f.bands.push_back( out );
    }

    // Acquisition.
    f.hasAcquisitionTime = state.acquisition.valid && !state.acquisition.timeIso.empty();
    f.acquisitionTimeIso = state.acquisition.timeIso;

    // Temporal references are pointers to collections; counts/dates are not
    // passport facts. The refs themselves ARE facts and stay projected (sorted
    // by collection id) so a temporal provider can resolve them into scene
    // counts/dates — or typed unknowns — without re-reading the passport.
    // The passport's own truncation flag is kept honestly: a narrowed ref
    // list must never read as complete.
    f.temporalSceneCount = 0;
    f.temporalTruncated = state.temporalRefsTruncated;
    f.temporalInvalidTimeCount = 0;
    for ( const auto &ref : state.temporalRefs )
        if ( !ref.collectionId.empty() )
            f.temporalCollectionRefs.push_back( ref.collectionId );
    std::sort( f.temporalCollectionRefs.begin(), f.temporalCollectionRefs.end() );

    // Leakage-relevant derivation identity.
    if ( state.provenance.isDerived )
    {
        for ( const auto &input : state.provenance.inputs )
            if ( !input.assetId.empty() )
                f.derivedFromAssetIds.push_back( input.assetId );
    }

    // Model manifest.
    f.hasModelManifest = state.modelDerived.present && !state.modelDerived.modelKind.empty();
    f.modelKind = state.modelDerived.modelKind;

    return f;
}

StateAssetFactsProvider::StateAssetFactsProvider( Resolver resolver )
    : resolver_( std::move( resolver ) )
{
}

SlotFactsResult StateAssetFactsProvider::slotFacts( const std::string &assetRef ) const
{
    SlotFactsResult result;
    if ( !resolver_ )
    {
        result.status = FactStatus::Unavailable;
        result.detail = "no passport resolver wired";
        return result;
    }
    const auto state = resolver_( assetRef );
    if ( !state.has_value() )
    {
        result.status = FactStatus::Unknown;
        result.detail = "no passport resolved for reference";
        return result;
    }
    // A passport that cannot name its asset kind is an unresolved fact, not
    // an exotic input kind: the raster rules skip kinds outside their closed
    // vocabulary, so projecting AssetKind::Unknown as Available would turn
    // every strategy check off and end in a placeholder ok.
    if ( state->kind == sicnu::state::AssetKind::Unknown )
    {
        result.status = FactStatus::Unknown;
        result.detail = "passport does not resolve an asset kind";
        return result;
    }
    // A passport whose lifecycle is not "ready" does not vouch for its own
    // facts: missing/stale/error passports must not be judged as observed
    // values, or preflight contradicts the passport's own claim.
    if ( state->lifecycle != sicnu::state::AssetLifecycle::Ready )
    {
        result.status = FactStatus::Unknown;
        result.detail = "passport lifecycle is " +
                        sicnu::state::assetLifecycleToString( state->lifecycle ) +
                        "; facts may not describe usable data";
        return result;
    }
    result.status = FactStatus::Available;
    result.facts = projectAssetState( *state );
    return result;
}

} // namespace sicnu::preflight
