// scientific_contracts.cpp — see scientific_contracts.h for the contracts.
#include "scientific_contracts.h"

#include <cmath>

namespace sicnu::processing::contracts
{

QJsonObject NumericDomainContract::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "regime" ),
                 regime == NumericScaleRegime::DnScale ? QStringLiteral( "dn_scale" )
                                                       : QStringLiteral( "unit_reflectance" ) );
    json.insert( QStringLiteral( "divisor" ), divisor );
    json.insert( QStringLiteral( "resolved_by" ), resolvedBy );
    json.insert( QStringLiteral( "observed_max_abs" ), observedMaxAbs );
    return json;
}

NumericDomainContract domainFromDeclaredScale( double declaredScale )
{
    NumericDomainContract contract;
    // A declared scale is authoritative regardless of magnitude: the product
    // stamped SICNU_NUMERIC_SCALE to say exactly how its stored values map
    // to unit reflectance.
    contract.divisor = ( std::isfinite( declaredScale ) && declaredScale > 0.0 ) ? declaredScale : 1.0;
    contract.regime = std::abs( contract.divisor - 1.0 ) > 1e-9
                          ? NumericScaleRegime::DnScale
                          : NumericScaleRegime::UnitReflectance;
    contract.resolvedBy = QStringLiteral( "declared-metadata" );
    return contract;
}

NumericDomainContract domainFromMaxAbsSample( double maxAbsSample )
{
    NumericDomainContract contract;
    contract.observedMaxAbs = maxAbsSample;
    if ( maxAbsSample > kDnScaleThreshold )
    {
        contract.regime = NumericScaleRegime::DnScale;
        contract.divisor = kCanonicalDnDivisor;
        contract.resolvedBy = QStringLiteral( "dataset-statistics" );
    }
    else
    {
        contract.regime = NumericScaleRegime::UnitReflectance;
        contract.divisor = 1.0;
        contract.resolvedBy = QStringLiteral( "dataset-statistics" );
    }
    return contract;
}

NumericDomainContract defaultUnitDomain()
{
    NumericDomainContract contract;
    contract.regime = NumericScaleRegime::UnitReflectance;
    contract.divisor = 1.0;
    contract.resolvedBy = QStringLiteral( "default-unit" );
    return contract;
}

} // namespace sicnu::processing::contracts
