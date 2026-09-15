// src/workflow/contract_checker.cpp — port contract inspection (D17)
#include "workflow/contract_checker.h"

#include <algorithm>
#include <cmath>

namespace sicnu::workflow {
namespace {

constexpr const char *kWildcard = "*";

// Radiometric upgrade order; anything outside the ladder (Index, Mask,
// Categorical, None, ...) is not contract-checked for state.
int radiometricRank( const QString &state )
{
    if ( state == QLatin1String( "DN" ) )
        return 0;
    if ( state == QLatin1String( "Radiance" ) )
        return 1;
    if ( state == QLatin1String( "TOA" ) || state == QLatin1String( "BOA" ) )
        return 2;
    return -1; // unconstrained / non-ladder
}

const PortFact *findPort( const QVector<PortFact> &ports, const QString &name )
{
    for ( const PortFact &port : ports )
        if ( port.portName == name )
            return &port;
    return nullptr;
}

} // namespace

QString contractMismatchTypeString( ContractMismatchType type )
{
    switch ( type )
    {
        case ContractMismatchType::CrsMismatch:
            return QStringLiteral( "CrsMismatch" );
        case ContractMismatchType::ResolutionMismatch:
            return QStringLiteral( "ResolutionMismatch" );
        case ContractMismatchType::RadiometricStateMismatch:
            return QStringLiteral( "RadiometricStateMismatch" );
        case ContractMismatchType::DataTypeMismatch:
            return QStringLiteral( "DataTypeMismatch" );
        case ContractMismatchType::DimensionMismatch:
            return QStringLiteral( "DimensionMismatch" );
    }
    return QStringLiteral( "Unknown" );
}

QVector<ContractViolation> inspectContracts( const WorkflowDefinition &def )
{
    QVector<ContractViolation> violations;

    for ( const EdgeFact &edge : def.edges )
    {
        const NodeFact *source = def.findNode( edge.sourceNodeId );
        const NodeFact *target = def.findNode( edge.targetNodeId );
        if ( !source || !target )
            continue; // dangling wiring is WorkflowIR::validateSemantics' domain

        const PortFact *out = findPort( source->outputPorts, edge.sourcePortName );
        const PortFact *in = findPort( target->inputPorts, edge.targetPortName );
        if ( !out || !in )
            continue; // same: syntax-level, reported by the IR validator

        auto add = [&]( ContractMismatchType type, const QString &expected, const QString &actual ) {
            ContractViolation violation;
            violation.edgeId = edge.edgeId;
            violation.sourceNodeId = edge.sourceNodeId;
            violation.targetNodeId = edge.targetNodeId;
            violation.sourcePortName = edge.sourcePortName;
            violation.targetPortName = edge.targetPortName;
            violation.mismatchType = type;
            violation.expectedSpecification = expected;
            violation.actualSpecification = actual;
            violation.description = QStringLiteral( "edge '%1': %2 (expected %3, actual %4)" )
                                        .arg( edge.edgeId, contractMismatchTypeString( type ), expected, actual );
            violations.append( violation );
        };

        // 1. CRS.
        if ( !in->crs.isEmpty() && in->crs != QLatin1String( kWildcard ) && !out->crs.isEmpty()
             && out->crs != QLatin1String( kWildcard ) && out->crs != in->crs )
        {
            add( ContractMismatchType::CrsMismatch, in->crs, out->crs );
        }

        // 2. Resolution (only when both sides carry concrete values).
        if ( in->resolutionX > 0.0 && out->resolutionX > 0.0
             && ( std::abs( in->resolutionX - out->resolutionX ) > 1e-6
                  || std::abs( in->resolutionY - out->resolutionY ) > 1e-6 ) )
        {
            add( ContractMismatchType::ResolutionMismatch,
                 QStringLiteral( "%1x%2" ).arg( in->resolutionX ).arg( in->resolutionY ),
                 QStringLiteral( "%1x%2" ).arg( out->resolutionX ).arg( out->resolutionY ) );
        }

        // 3. Radiometric ladder (upgrades only; see header rationale).
        const int outRank = radiometricRank( out->radiometricState );
        const int inRank = radiometricRank( in->radiometricState );
        if ( outRank >= 0 && inRank > outRank )
        {
            add( ContractMismatchType::RadiometricStateMismatch, in->radiometricState, out->radiometricState );
        }

        // 4. Data type.
        if ( !in->dataType.isEmpty() && in->dataType != QLatin1String( kWildcard ) && !out->dataType.isEmpty()
             && out->dataType != QLatin1String( kWildcard ) && out->dataType != in->dataType )
        {
            add( ContractMismatchType::DataTypeMismatch, in->dataType, out->dataType );
        }
    }
    return violations;
}

} // namespace sicnu::workflow
