// stack_registrator.cpp — F13 Package E implementation.
//
// Solver: dense normal equations over the unconstrained scenes (reference
// pinned), assembled from weighted edge constraints, solved by Gaussian
// elimination with partial pivoting. Scene count is capped at
// StackOptions::maxScenes (logical cap), keeping the dense solve bounded:
// (2·maxScenes)² doubles at most.
#include "stack_registrator.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <utility>

namespace sicnu::registration {

namespace {
constexpr double kEps = 1e-12;

struct IndexedEdge {
    int from{-1};
    int to{-1};
    double tx{0.0};
    double ty{0.0};
    double weight{0.0};
};

double observationWeight(const StackPairObservation& e)
{
    return std::max(0.0, e.confidence) * static_cast<double>(std::max(1, e.inlierCount));
}

} // namespace

QString StackRegistrator::suggestReference(const std::vector<QString>& sceneIds,
                                           const std::vector<StackPairObservation>& observations)
{
    if (sceneIds.empty())
        return {};
    std::map<QString, double> weight;
    for (const auto& id : sceneIds)
        weight[id] = 0.0;
    for (const auto& e : observations) {
        const double w = observationWeight(e);
        if (weight.count(e.fromId))
            weight[e.fromId] += w;
        if (weight.count(e.toId))
            weight[e.toId] += w;
    }
    QString best;
    double bestW = -1.0;
    for (const auto& id : sceneIds) {
        if (weight[id] > bestW + 1e-12) {
            bestW = weight[id];
            best = id;
        }
    }
    return best;
}

StackSolution StackRegistrator::solveTranslations(const std::vector<QString>& sceneIds,
                                                  const std::vector<StackPairObservation>& obs,
                                                  const StackOptions& options,
                                                  const std::atomic_bool* cancel)
{
    if (sceneIds.empty())
        throw std::invalid_argument("stack registrator: empty scene list");
    if (static_cast<int>(sceneIds.size()) > options.maxScenes) {
        StackSolution s;
        s.reason = QStringLiteral("cap_exhausted");
        return s;
    }

    std::map<QString, int> index;
    for (int i = 0; i < static_cast<int>(sceneIds.size()); ++i)
        index[sceneIds[static_cast<std::size_t>(i)]] = i;

    QString referenceId = options.referenceId;
    if (referenceId.isEmpty())
        referenceId = suggestReference(sceneIds, obs);
    if (!index.count(referenceId))
        throw std::invalid_argument("stack registrator: unknown reference id");
    const int ref = index[referenceId];
    const int n = static_cast<int>(sceneIds.size());

    // Validate + weight observations; unknown ids are rejected up front.
    std::vector<IndexedEdge> edges;
    edges.reserve(obs.size());
    for (const auto& e : obs) {
        if (!index.count(e.fromId) || !index.count(e.toId))
            throw std::invalid_argument("stack registrator: observation references unknown scene");
        if (e.fromId == e.toId)
            continue; // self-observation carries no constraint
        IndexedEdge ie;
        ie.from = index[e.fromId];
        ie.to = index[e.toId];
        ie.tx = e.tx;
        ie.ty = e.ty;
        ie.weight = observationWeight(e);
        if (ie.weight > kEps)
            edges.push_back(ie);
    }

    StackSolution sol;
    sol.referenceId = referenceId;

    // BFS connectivity from the reference over accepted edges.
    std::vector<int> hops(static_cast<std::size_t>(n), -1);
    {
        std::vector<std::vector<int>> adj(static_cast<std::size_t>(n));
        for (const auto& e : edges) {
            adj[static_cast<std::size_t>(e.from)].push_back(e.to);
            adj[static_cast<std::size_t>(e.to)].push_back(e.from);
        }
        std::vector<int> queue;
        queue.push_back(ref);
        hops[static_cast<std::size_t>(ref)] = 0;
        for (std::size_t qi = 0; qi < queue.size(); ++qi) {
            const int u = queue[qi];
            for (int v : adj[static_cast<std::size_t>(u)]) {
                if (hops[static_cast<std::size_t>(v)] == -1) {
                    hops[static_cast<std::size_t>(v)] = hops[static_cast<std::size_t>(u)] + 1;
                    queue.push_back(v);
                }
            }
        }
    }
    int connectedCount = 0;
    for (int i = 0; i < n; ++i)
        if (hops[static_cast<std::size_t>(i)] >= 0)
            ++connectedCount;
    sol.disconnectedScenes = n - connectedCount;

    if (edges.empty()) {
        sol.status = RegistrationStatus::Refused;
        sol.reason = QStringLiteral("too_few_matches");
        return sol;
    }

    // ---- Normal equations over connected, non-reference scenes ----------
    // Edge (f, t) constrains offset(t) - offset(f) = measurement, weighted.
    // Unknowns exist only for scenes in the connected subgraph: a
    // disconnected scene has no constraints and would leave an all-zero row
    // (singular system) — it stays reported as disconnected instead.
    std::vector<int> unknownOf(static_cast<std::size_t>(n), -1);
    int uCount = 0;
    for (int i = 0; i < n; ++i)
        if (i != ref && hops[static_cast<std::size_t>(i)] >= 0)
            unknownOf[static_cast<std::size_t>(i)] = uCount++;
    const int dim = 2 * uCount;

    std::vector<double> A(static_cast<std::size_t>(dim) * dim, 0.0);
    std::vector<double> b(static_cast<std::size_t>(dim), 0.0);
    auto accumulate = [&](int row, int col, double v) {
        A[static_cast<std::size_t>(row) * dim + static_cast<std::size_t>(col)] += v;
    };

    for (const auto& e : edges) {
        const int uf = unknownOf[static_cast<std::size_t>(e.from)];
        const int ut = unknownOf[static_cast<std::size_t>(e.to)];
        for (int axis = 0; axis < 2; ++axis) {
            const double meas = axis == 0 ? e.tx : e.ty;
            const int rTx = ut >= 0 ? 2 * ut + axis : -1;
            const int rFx = uf >= 0 ? 2 * uf + axis : -1;
            std::vector<std::pair<int, double>> cols;
            if (rTx >= 0)
                cols.emplace_back(rTx, 1.0);
            if (rFx >= 0)
                cols.emplace_back(rFx, -1.0);
            for (const auto& [rc, cv] : cols) {
                b[static_cast<std::size_t>(rc)] += e.weight * cv * meas;
                for (const auto& [cc, ccv] : cols)
                    accumulate(rc, cc, e.weight * cv * ccv);
            }
        }
    }

    // Gaussian elimination with partial pivoting (dim == 0 is trivially
    // solved: the single scene is the pinned reference).
    std::vector<double> x(static_cast<std::size_t>(dim), 0.0);
    bool solvable = true;
    for (int col = 0; col < dim && solvable; ++col) {
        if (cancel && cancel->load()) {
            StackSolution s;
            s.referenceId = referenceId;
            s.reason = QStringLiteral("cancelled");
            return s;
        }
        int piv = col;
        for (int r = col + 1; r < dim; ++r)
            if (std::abs(A[static_cast<std::size_t>(r) * dim + col])
                > std::abs(A[static_cast<std::size_t>(piv) * dim + col]))
                piv = r;
        if (std::abs(A[static_cast<std::size_t>(piv) * dim + col]) < 1e-10) {
            solvable = false;
            break;
        }
        if (piv != col) {
            for (int c = 0; c < dim; ++c)
                std::swap(A[static_cast<std::size_t>(piv) * dim + c],
                          A[static_cast<std::size_t>(col) * dim + c]);
            std::swap(b[static_cast<std::size_t>(piv)], b[static_cast<std::size_t>(col)]);
        }
        for (int r = col + 1; r < dim; ++r) {
            const double f = A[static_cast<std::size_t>(r) * dim + col]
                             / A[static_cast<std::size_t>(col) * dim + col];
            if (f == 0.0)
                continue;
            for (int c = col; c < dim; ++c)
                A[static_cast<std::size_t>(r) * dim + c] -=
                    f * A[static_cast<std::size_t>(col) * dim + c];
            b[static_cast<std::size_t>(r)] -= f * b[static_cast<std::size_t>(col)];
        }
    }
    if (solvable) {
        for (int r = dim - 1; r >= 0; --r) {
            double acc = b[static_cast<std::size_t>(r)];
            for (int c = r + 1; c < dim; ++c)
                acc -=
                    A[static_cast<std::size_t>(r) * dim + c] * x[static_cast<std::size_t>(c)];
            x[static_cast<std::size_t>(r)] = acc / A[static_cast<std::size_t>(r) * dim + r];
        }
    }

    if (!solvable) {
        sol.status = RegistrationStatus::Refused;
        sol.reason = QStringLiteral("degenerate_geometry");
        return sol;
    }

    // ---- Per-scene solutions + post-adjustment edge residual metrics ----
    sol.scenes.resize(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        StackSceneSolution ss;
        ss.sceneId = sceneIds[static_cast<std::size_t>(i)];
        ss.hopCount = hops[static_cast<std::size_t>(i)];
        ss.connected = ss.hopCount >= 0;
        const int u = unknownOf[static_cast<std::size_t>(i)];
        if (u >= 0) { // connected, non-reference: read the solved unknowns
            ss.offsetX = x[static_cast<std::size_t>(2 * u)];
            ss.offsetY = x[static_cast<std::size_t>(2 * u + 1)];
        }
        sol.scenes[static_cast<std::size_t>(i)] = ss;
    }

    double maxRes = 0.0;
    double sqAcc = 0.0;
    int resCount = 0;
    for (const auto& e : edges) {
        const auto& sf = sol.scenes[static_cast<std::size_t>(e.from)];
        const auto& st = sol.scenes[static_cast<std::size_t>(e.to)];
        const double ex = (st.offsetX - sf.offsetX) - e.tx;
        const double ey = (st.offsetY - sf.offsetY) - e.ty;
        const double r = std::hypot(ex, ey);
        maxRes = std::max(maxRes, r);
        sqAcc += r * r;
        ++resCount;
        sol.scenes[static_cast<std::size_t>(e.from)].maxEdgeResidual =
            std::max(sol.scenes[static_cast<std::size_t>(e.from)].maxEdgeResidual, r);
        sol.scenes[static_cast<std::size_t>(e.to)].maxEdgeResidual =
            std::max(sol.scenes[static_cast<std::size_t>(e.to)].maxEdgeResidual, r);
    }
    sol.maxEdgeResidual = maxRes;
    sol.rmsEdgeResidual = resCount > 0 ? std::sqrt(sqAcc / static_cast<double>(resCount)) : 0.0;

    sol.status = RegistrationStatus::Success;
    return sol;
}

} // namespace sicnu::registration
