// capsule_diff.h — typed, section-level comparison of two
// ReproducibilityCapsules.
//
// Diff semantics follow the ADR 0137 doctrine: identity pins define WHAT
// experiment this is; the environment is deliberately not an identity pin.
// Section classification:
//   identity sections (goal, software.revision, capabilities, inputs,
//     parameters, plan, outputs, provenance)
//        → any difference is an IdentityBreak
//   reported sections (environment, evidence, created_utc, capsule_id)
//        → differences are Reported, never punished
// Levels:
//   Identical        — all identity sections equal (metadata drift allowed)
//   EquivalentRerun  — identity equal, only reported sections differ
//   IdentityBreak    — at least one identity section differs
#pragma once

#include "capsule_document.h"

#include <QJsonObject>
#include <QStringList>
#include <QVector>

namespace sicnu::experiment::capsule
{

struct CapsuleSectionDiff
{
    QString section;      ///< document section ("inputs[0].digest" style paths too)
    QString kind;         ///< "identity" | "reported"
    QString left;         ///< compact rendering of the left value
    QString right;        ///< compact rendering of the right value

    QJsonObject toJson() const;
};

struct CapsuleDiffReport
{
    enum class Level
    {
        Identical,
        EquivalentRerun,
        IdentityBreak,
    };

    Level level = Level::Identical;
    QVector<CapsuleSectionDiff> sections;
    /// Evidence lines for the verdict (one per differing section).
    QStringList reasons;

    QString levelToString() const;
    QJsonObject toJson() const;

    static CapsuleDiffReport diff( const CapsuleDocument &a, const CapsuleDocument &b );
};

} // namespace sicnu::experiment::capsule
