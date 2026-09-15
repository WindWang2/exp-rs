// model_selector.cpp — F13 Package C implementation.
//
// CV protocol: fold membership is index % folds over the input order
// (caller-owned order; deterministic). A fold whose training complement has
// fewer than minPoints(model) points is skipped. cvRmse is the unweighted
// mean of the per-fold held-out RMSEs.
#include "model_selector.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>
#include <stdexcept>

namespace sicnu::registration {

namespace {

using rs::algorithms::GeometricTransform;
using rs::algorithms::TpsConfig;
using rs::algorithms::TpsInterpolator;
using Pts = std::vector<std::pair<double, double>>;

rs::algorithms::TransformModel toTransformModel(CandidateModel model)
{
    switch (model) {
    case CandidateModel::Translation: return rs::algorithms::TransformModel::Translation;
    case CandidateModel::Similarity: return rs::algorithms::TransformModel::Similarity;
    case CandidateModel::Affine: return rs::algorithms::TransformModel::Affine;
    case CandidateModel::Projective: return rs::algorithms::TransformModel::Projective;
    case CandidateModel::Polynomial2: return rs::algorithms::TransformModel::Polynomial2;
    case CandidateModel::Polynomial3: return rs::algorithms::TransformModel::Polynomial3;
    case CandidateModel::Tps: break;
    }
    return rs::algorithms::TransformModel::Affine; // unreachable; silences warnings
}

int minPointsFor(CandidateModel model)
{
    // TpsInterpolator needs 3 distinct knots (see tps_interpolator.h); all
    // other candidates reuse the parametric minimum-point contract.
    if (model == CandidateModel::Tps)
        return 3;
    return GeometricTransform::minPointsRequired(toTransformModel(model));
}

/// Uniform fit/apply adapter over the ladder.
struct FitAdapter {
    bool ok{false};
    rs::algorithms::TransformResult transform;
    TpsInterpolator tps;

    static FitAdapter fit(CandidateModel model, const Pts& src, const Pts& dst, double tpsLambda)
    {
        FitAdapter f;
        if (src.size() != dst.size() || src.size() < static_cast<std::size_t>(minPointsFor(model)))
            return f;
        if (model == CandidateModel::Tps) {
            f.ok = f.tps.fit(src, dst, TpsConfig{tpsLambda});
        } else {
            f.transform = GeometricTransform::solve(toTransformModel(model), src, dst);
            f.ok = f.transform.success;
        }
        return f;
    }

    std::pair<double, double> apply(CandidateModel model, double x, double y) const
    {
        if (model == CandidateModel::Tps)
            return tps.transform(x, y);
        return GeometricTransform::applyForward(transform, x, y);
    }
};

double rmseOver(const FitAdapter& fit, CandidateModel model, const Pts& src, const Pts& dst)
{
    double acc = 0.0;
    for (std::size_t i = 0; i < src.size(); ++i) {
        const auto [mx, my] = fit.apply(model, src[i].first, src[i].second);
        const double ex = mx - dst[i].first;
        const double ey = my - dst[i].second;
        acc += ex * ex + ey * ey;
    }
    return std::sqrt(acc / static_cast<double>(src.size()));
}

QString candidateName(CandidateModel model)
{
    switch (model) {
    case CandidateModel::Translation: return QStringLiteral("translation");
    case CandidateModel::Similarity: return QStringLiteral("similarity");
    case CandidateModel::Affine: return QStringLiteral("affine");
    case CandidateModel::Projective: return QStringLiteral("projective");
    case CandidateModel::Polynomial2: return QStringLiteral("polynomial2");
    case CandidateModel::Polynomial3: return QStringLiteral("polynomial3");
    case CandidateModel::Tps: return QStringLiteral("tps");
    }
    return QStringLiteral("unknown");
}

} // namespace

QString candidateModelName(CandidateModel model)
{
    return candidateName(model);
}

ModelSelectionReport ModelSelector::select(const Pts& sourcePts, const Pts& targetPts,
                                           const ModelSelectionOptions& options,
                                           const std::atomic_bool* cancel)
{
    if (sourcePts.size() != targetPts.size())
        throw std::invalid_argument("model selector: point count mismatch");
    for (const auto& p : sourcePts)
        if (!std::isfinite(p.first) || !std::isfinite(p.second))
            throw std::invalid_argument("model selector: non-finite source coordinate");
    for (const auto& p : targetPts)
        if (!std::isfinite(p.first) || !std::isfinite(p.second))
            throw std::invalid_argument("model selector: non-finite target coordinate");

    const int folds = std::max(2, options.folds);
    const std::size_t n = sourcePts.size();

    const std::vector<CandidateModel> ladder = {
        CandidateModel::Translation, CandidateModel::Similarity,
        CandidateModel::Affine,      CandidateModel::Projective,
        CandidateModel::Polynomial2, CandidateModel::Polynomial3,
        CandidateModel::Tps};

    ModelSelectionReport report;
    report.evidence.reserve(ladder.size());

    int feasibleCount = 0;
    bool countLimited = true;
    bool anySolverFail = false;
    bool anyIllConditioned = false;
    std::optional<CandidateModel> selected;
    double selectedCvRmse = std::numeric_limits<double>::infinity();

    for (CandidateModel model : ladder) {
        if (cancel && cancel->load()) {
            report = ModelSelectionReport{};
            report.reason = QStringLiteral("cancelled");
            return report;
        }
        ModelCandidateEvidence ev;
        ev.model = model;

        if (n < static_cast<std::size_t>(minPointsFor(model))) {
            ev.rejectedReason = QStringLiteral("too_few_matches");
            report.evidence.push_back(std::move(ev));
            continue;
        }
        countLimited = false;

        // --- k-fold held-out RMSE --------------------------------------
        int usedFolds = 0;
        double cvAcc = 0.0;
        bool foldFailed = false;
        for (int k = 0; k < folds && !foldFailed; ++k) {
            Pts trainS, trainT, testS, testT;
            for (std::size_t i = 0; i < n; ++i) {
                if (static_cast<int>(i % static_cast<std::size_t>(folds)) == k) {
                    testS.push_back(sourcePts[i]);
                    testT.push_back(targetPts[i]);
                } else {
                    trainS.push_back(sourcePts[i]);
                    trainT.push_back(targetPts[i]);
                }
            }
            if (testS.empty())
                continue; // empty held-out split: nothing to evaluate (P1 #2)
            if (static_cast<int>(trainS.size()) < minPointsFor(model))
                continue; // skipped fold
            const FitAdapter foldFit = FitAdapter::fit(model, trainS, trainT, options.tpsLambda);
            if (!foldFit.ok) {
                foldFailed = true;
                break;
            }
            cvAcc += rmseOver(foldFit, model, testS, testT);
            ++usedFolds;
        }
        if (foldFailed || usedFolds == 0) {
            ev.rejectedReason = foldFailed ? QStringLiteral("degenerate_geometry")
                                           : QStringLiteral("too_few_matches");
            anySolverFail = anySolverFail || foldFailed;
            report.evidence.push_back(std::move(ev));
            continue;
        }
        ev.cvRmse = cvAcc / static_cast<double>(usedFolds);
        if (!std::isfinite(ev.cvRmse)) {
            ev.rejectedReason = QStringLiteral("degenerate_geometry");
            report.evidence.push_back(std::move(ev));
            continue;
        }

        // --- full fit + feasibility gates -------------------------------
        const FitAdapter full = FitAdapter::fit(model, sourcePts, targetPts, options.tpsLambda);
        if (!full.ok) {
            ev.rejectedReason = QStringLiteral("degenerate_geometry");
            anySolverFail = true;
            report.evidence.push_back(std::move(ev));
            continue;
        }
        ev.fitRmse = rmseOver(full, model, sourcePts, targetPts);
        if (model != CandidateModel::Tps) {
            ev.conditionNumber = full.transform.conditionNumber;
            if (ev.conditionNumber > options.maxConditionNumber) {
                ev.rejectedReason = QStringLiteral("ill_conditioned");
                anyIllConditioned = true;
                report.evidence.push_back(std::move(ev));
                continue;
            }
        } else {
            ev.bendingEnergy = full.tps.computeBendingEnergy();
        }

        ev.feasible = true;
        ++feasibleCount;

        // --- ladder walk: step up only on real held-out improvement -----
        if (!selected.has_value()
            || ev.cvRmse <= selectedCvRmse * (1.0 - std::max(0.0, options.minImprovement))) {
            selected = model;
            selectedCvRmse = ev.cvRmse;
        }
        report.evidence.push_back(std::move(ev));
    }

    if (feasibleCount == 0) {
        report.status = RegistrationStatus::Refused;
        report.reason = countLimited            ? QStringLiteral("too_few_matches")
                        : anySolverFail         ? QStringLiteral("degenerate_geometry")
                        : anyIllConditioned     ? QStringLiteral("ill_conditioned")
                                                : QStringLiteral("too_few_matches");
        return report;
    }

    report.status = RegistrationStatus::Success;
    report.selected = *selected;
    const FitAdapter finalFit =
        FitAdapter::fit(*selected, sourcePts, targetPts, options.tpsLambda);
    if (*selected == CandidateModel::Tps) {
        report.tpsFit = finalFit.tps;
    } else {
        report.transform = finalFit.transform;
    }
    return report;
}

} // namespace sicnu::registration
