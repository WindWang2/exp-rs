#include "preflight/asset_state_adapter.h"

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
    // passport facts and stay unset (typed unknown downstream).
    f.temporalSceneCount = 0;
    f.temporalTruncated = false;

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
    result.status = FactStatus::Available;
    result.facts = projectAssetState( *state );
    return result;
}

} // namespace sicnu::preflight
