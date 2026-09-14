// src/workflow/contract_checker.h — operator port contract inspection (D17, ADR 0162)
#pragma once

//
// Checks every edge's data contract: the producing port's facts against the
// consuming port's requirements. Only violations with a closed-form repair
// are reported (the check and the repair table share one vocabulary):
//
//   - CrsMismatch:            concrete CRS differs (wildcard "*" passes).
//   - ResolutionMismatch:     concrete resolutions differ by > 1e-6.
//   - RadiometricStateMismatch: the consuming port requires a higher
//     radiometric product than the producing port delivers
//     (DN < Radiance < TOA/BOA). Downgrades (e.g. BOA into a DN input)
//     are deliberately NOT flagged: they are science-changing decisions,
//     not auto-repairable contract breaks.
//   - DataTypeMismatch:       concrete data types differ.
//   - DimensionMismatch:      enumerated for the closed vocabulary but not
//     derivable from PortFact today (no extent facts); never emitted.
//
// Deterministic: violations enumerate edges in document order, mismatch
// kinds in the fixed order above.
//

#include <QVector>
#include <QString>

#include "workflow/workflow_ir_v2.h"

namespace sicnu::workflow {

enum class ContractMismatchType
{
    CrsMismatch,
    ResolutionMismatch,
    RadiometricStateMismatch,
    DataTypeMismatch,
    DimensionMismatch
};

QString contractMismatchTypeString( ContractMismatchType type ); // "CrsMismatch", ...

struct ContractViolation
{
    QString edgeId;
    QString sourceNodeId;
    QString targetNodeId;
    QString sourcePortName;
    QString targetPortName;
    ContractMismatchType mismatchType;
    QString expectedSpecification; // what the consuming port requires
    QString actualSpecification;   // what the producing port delivers
    QString description;
};

/// Inspects every edge; empty result = all contracts satisfied.
QVector<ContractViolation> inspectContracts( const WorkflowDefinition &def );

} // namespace sicnu::workflow
