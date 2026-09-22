// fault_diagnosis.cpp — the teaching-side diagnosis mirror.
#include "fault_diagnosis.h"

#include "fault_observables.h"

#include <cmath>

namespace sicnu::faultlab
{

namespace
{

bool numberIs( const ObservableSet &set, const std::string &id, double &out )
{
    const Observable *observable = findObservable( set, id );
    if ( observable == nullptr || observable->kind != ObservableKind::Number )
    {
        return false;
    }
    out = observable->number;
    return true;
}

bool textIs( const ObservableSet &set, const std::string &id, std::string &out )
{
    const Observable *observable = findObservable( set, id );
    if ( observable == nullptr || observable->kind != ObservableKind::Text )
    {
        return false;
    }
    out = observable->text;
    return true;
}

bool differs( const ObservableSet &clean, const ObservableSet &faulted, const std::string &id,
              double &delta )
{
    double cleanValue = 0.0;
    double faultedValue = 0.0;
    if ( !numberIs( clean, id, cleanValue ) || !numberIs( faulted, id, faultedValue ) )
    {
        return false;
    }
    delta = faultedValue - cleanValue;
    return std::fabs( delta ) > kObservableTolerance;
}

} // namespace

std::string diagnoseTransition( const ObservableSet &clean, const ObservableSet &faulted )
{
    // Most specific first, mirroring the lab diagnostic brain's ordering.

    // all_negative_index: the lab brain fires when the index maximum is
    // negative; a role swap flips every per-pixel value, so the faulted
    // maximum is the exact negation of the clean minimum.
    double faultedMax = 0.0;
    double cleanMax = 0.0;
    if ( numberIs( faulted, "index_max", faultedMax ) && faultedMax < 0.0 )
    {
        if ( !numberIs( clean, "index_max", cleanMax ) || cleanMax >= 0.0 )
        {
            return "all_negative_index";
        }
    }

    double faultedFinite = 0.0;
    double cleanFinite = 0.0;
    if ( numberIs( faulted, "finite_fraction", faultedFinite ) && faultedFinite <= 0.0 )
    {
        if ( !numberIs( clean, "finite_fraction", cleanFinite ) || cleanFinite > 0.0 )
        {
            return "all_nodata";
        }
    }

    double faultedKappa = 0.0;
    double cleanKappa = 0.0;
    if ( numberIs( faulted, "kappa", faultedKappa ) && std::fabs( faultedKappa ) < 0.1 )
    {
        if ( numberIs( clean, "kappa", cleanKappa ) && std::fabs( cleanKappa ) > 0.5 )
        {
            return "kappa_near_zero";
        }
    }

    std::string faultedCrs;
    std::string cleanCrs;
    if ( textIs( faulted, "crs", faultedCrs ) && textIs( clean, "crs", cleanCrs ) &&
         faultedCrs != cleanCrs )
    {
        return "crs_mismatch";
    }

    // scale_stripes: the lab brain fires when two inputs' pixel sizes
    // differ by more than 1%. The same rule here — a pure origin shift
    // keeps pixel size (detected by the origin delta, not by a signature:
    // stripes are a resampling symptom the observation level cannot see).
    double cleanPixelX = 0.0;
    double faultedPixelX = 0.0;
    if ( numberIs( clean, "geo_transform.pixel_x", cleanPixelX ) &&
         numberIs( faulted, "geo_transform.pixel_x", faultedPixelX ) && cleanPixelX > 0.0 &&
         faultedPixelX > 0.0 &&
         std::fabs( faultedPixelX / cleanPixelX - 1.0 ) > 0.01 )
    {
        return "scale_stripes";
    }

    // No canonical signature covers this transition.
    return kDiagnosisUnmatched;
}

} // namespace sicnu::faultlab
