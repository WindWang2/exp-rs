// gcp_manager.cpp — D14 Package A implementation (ADR 0159).
#include "processing/algorithms/gcp_manager.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <limits>

namespace rs::core {

namespace {

bool allFinite(const GcpPoint& pt)
{
    return std::isfinite(pt.sourceX) && std::isfinite(pt.sourceY) &&
           std::isfinite(pt.targetX) && std::isfinite(pt.targetY);
}

double cross(double ox, double oy, double ax, double ay, double bx, double by)
{
    return (ax - ox) * (by - oy) - (ay - oy) * (bx - ox);
}

/// Andrew monotone chain hull, counter-clockwise, degenerate points dropped.
std::vector<std::pair<double, double>> monotoneChain(const std::vector<std::pair<double, double>>& pts)
{
    auto sorted = pts;
    std::ranges::sort(sorted);
    const auto n = sorted.size();
    if (n < 3) {
        return sorted;
    }
    std::vector<std::pair<double, double>> hull(2 * n + 1);
    size_t k = 0;
    for (size_t i = 0; i < n; ++i) {
        while (k >= 2 && cross(hull[k - 2].first, hull[k - 2].second,
                               hull[k - 1].first, hull[k - 1].second,
                               sorted[i].first, sorted[i].second) <= 0.0)
            --k;
        hull[k++] = sorted[i];
    }
    const size_t lower = k + 1;
    for (size_t i = n - 1; i-- > 0;) {
        while (k >= lower && cross(hull[k - 2].first, hull[k - 2].second,
                                   hull[k - 1].first, hull[k - 1].second,
                                   sorted[i].first, sorted[i].second) <= 0.0)
            --k;
        hull[k++] = sorted[i];
    }
    hull.resize(k - 1);
    return hull;
}

double shoelaceArea(const std::vector<std::pair<double, double>>& polygon)
{
    if (polygon.size() < 3)
        return 0.0;
    double sum = 0.0;
    for (size_t i = 0; i < polygon.size(); ++i) {
        const auto& a = polygon[i];
        const auto& b = polygon[(i + 1) % polygon.size()];
        sum += a.first * b.second - b.first * a.second;
    }
    return std::abs(sum) / 2.0;
}

struct DelaunayVertex {
    double x{0.0};
    double y{0.0};
    const GcpPoint* point{nullptr};
};

struct DelaunayTri {
    int a{-1};
    int b{-1};
    int c{-1};
};

bool circumCircle(const DelaunayVertex& a, const DelaunayVertex& b, const DelaunayVertex& c,
                  double& cx, double& cy, double& r2)
{
    const double d = 2.0 * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
    if (std::abs(d) < 1e-12)
        return false;
    const double a2 = a.x * a.x + a.y * a.y;
    const double b2 = b.x * b.x + b.y * b.y;
    const double c2 = c.x * c.x + c.y * c.y;
    cx = (a2 * (b.y - c.y) + b2 * (c.y - a.y) + c2 * (a.y - b.y)) / d;
    cy = (a2 * (c.x - b.x) + b2 * (a.x - c.x) + c2 * (b.x - a.x)) / d;
    r2 = (a.x - cx) * (a.x - cx) + (a.y - cy) * (a.y - cy);
    return true;
}

/// Bowyer-Watson triangulation; returns index triples into `vertices`.
std::vector<DelaunayTri> bowyerWatson(const std::vector<DelaunayVertex>& vertices)
{
    std::vector<DelaunayTri> tris;
    if (vertices.size() < 3)
        return tris;

    double minX = vertices[0].x, maxX = vertices[0].x;
    double minY = vertices[0].y, maxY = vertices[0].y;
    for (const auto& v : vertices) {
        minX = std::min(minX, v.x);
        maxX = std::max(maxX, v.x);
        minY = std::min(minY, v.y);
        maxY = std::max(maxY, v.y);
    }
    const double dmax = std::max(maxX - minX, maxY - minY);
    if (!(dmax > 0.0))
        return tris;
    const double midX = (minX + maxX) / 2.0;
    const double midY = (minY + maxY) / 2.0;

    // Super-triangle far outside the point set.
    auto super = vertices;
    super.push_back({midX - 20.0 * dmax, midY - dmax, nullptr});
    super.push_back({midX, midY + 20.0 * dmax, nullptr});
    super.push_back({midX + 20.0 * dmax, midY - dmax, nullptr});
    const int n = static_cast<int>(vertices.size());
    tris.push_back({n, n + 1, n + 2});

    for (int i = 0; i < n; ++i) {
        std::vector<DelaunayTri> bad;
        std::vector<DelaunayTri> good;
        for (const auto& t : tris) {
            double cx = 0.0, cy = 0.0, r2 = 0.0;
            bool inside = false;
            if (circumCircle(super[t.a], super[t.b], super[t.c], cx, cy, r2)) {
                const double dx = super[i].x - cx;
                const double dy = super[i].y - cy;
                inside = dx * dx + dy * dy <= r2;
            }
            (inside ? bad : good).push_back(t);
        }
        // Boundary edges of the cavity appear in exactly one bad triangle.
        std::vector<std::pair<int, int>> boundary;
        for (const auto& t : bad) {
            const std::pair<int, int> edges[3] = {{t.a, t.b}, {t.b, t.c}, {t.c, t.a}};
            for (const auto& e : edges) {
                bool shared = false;
                for (const auto& o : bad) {
                    if (o.a == t.a && o.b == t.b && o.c == t.c)
                        continue;
                    const std::pair<int, int> oe[3] = {{o.a, o.b}, {o.b, o.c}, {o.c, o.a}};
                    for (const auto& e2 : oe) {
                        if ((e2.first == e.first && e2.second == e.second) ||
                            (e2.first == e.second && e2.second == e.first)) {
                            shared = true;
                            break;
                        }
                    }
                    if (shared)
                        break;
                }
                if (!shared)
                    boundary.push_back(e);
            }
        }
        tris = std::move(good);
        for (const auto& e : boundary)
            tris.push_back({e.first, e.second, i});
    }

    // Drop every triangle still touching the super-triangle.
    tris.erase(std::remove_if(tris.begin(), tris.end(),
                              [n](const DelaunayTri& t) {
                                  return t.a >= n || t.b >= n || t.c >= n;
                              }),
               tris.end());
    return tris;
}

double triangleAspectRatio(const DelaunayVertex& a, const DelaunayVertex& b, const DelaunayVertex& c)
{
    const double area = std::abs(cross(a.x, a.y, b.x, b.y, c.x, c.y)) / 2.0;
    if (area < 1e-12)
        return std::numeric_limits<double>::infinity(); // degenerate = worst-case diagnostic
    const double ab = std::hypot(b.x - a.x, b.y - a.y);
    const double bc = std::hypot(c.x - b.x, c.y - b.y);
    const double ca = std::hypot(a.x - c.x, a.y - c.y);
    const double rCircum = ab * bc * ca / (4.0 * area);
    const double rIn = area / ((ab + bc + ca) / 2.0);
    if (rIn < 1e-15)
        return std::numeric_limits<double>::infinity();
    return rCircum / (2.0 * rIn);
}

QString numberToString(double v)
{
    return QString::number(v, 'g', 17);
}

} // namespace

bool GcpManager::addPoint(const GcpPoint& pt)
{
    if (pt.id.isEmpty() || !allFinite(pt))
        return false;
    const auto it = std::ranges::find_if(mPoints,
                                         [&pt](const GcpPoint& existing) { return existing.id == pt.id; });
    if (it != mPoints.end())
        return false;
    mPoints.push_back(pt);
    return true;
}

bool GcpManager::removePoint(const QString& id)
{
    const auto it = std::ranges::find_if(mPoints,
                                         [&id](const GcpPoint& pt) { return pt.id == id; });
    if (it == mPoints.end())
        return false;
    mPoints.erase(it);
    return true;
}

bool GcpManager::updatePoint(const GcpPoint& pt)
{
    if (pt.id.isEmpty() || !allFinite(pt))
        return false;
    const auto it = std::ranges::find_if(mPoints,
                                         [&pt](const GcpPoint& existing) { return existing.id == pt.id; });
    if (it == mPoints.end())
        return false;
    *it = pt;
    return true;
}

void GcpManager::setPointEnabled(const QString& id, bool enabled)
{
    const auto it = std::ranges::find_if(mPoints,
                                         [&id](const GcpPoint& pt) { return pt.id == id; });
    if (it != mPoints.end())
        it->enabled = enabled;
}

void GcpManager::clear()
{
    mPoints.clear();
}

std::vector<GcpPoint> GcpManager::activePoints() const
{
    std::vector<GcpPoint> out;
    out.reserve(mPoints.size());
    for (const auto& pt : mPoints)
        if (pt.enabled)
            out.push_back(pt);
    return out;
}

std::vector<GcpPoint> GcpManager::allPoints() const
{
    return mPoints;
}

std::optional<GcpPoint> GcpManager::findPoint(const QString& id) const
{
    const auto it = std::ranges::find_if(mPoints,
                                         [&id](const GcpPoint& pt) { return pt.id == id; });
    if (it == mPoints.end())
        return std::nullopt;
    return *it;
}

size_t GcpManager::size() const noexcept
{
    return mPoints.size();
}

size_t GcpManager::activeCount() const noexcept
{
    return static_cast<size_t>(std::count_if(mPoints.begin(), mPoints.end(),
                                             [](const GcpPoint& pt) { return pt.enabled; }));
}

std::vector<std::pair<double, double>> GcpManager::computeConvexHull() const
{
    std::vector<std::pair<double, double>> pts;
    pts.reserve(activeCount());
    for (const auto& pt : activePoints())
        pts.emplace_back(pt.sourceX, pt.sourceY);
    return monotoneChain(pts);
}

std::vector<std::tuple<QString, QString, QString>> GcpManager::computeDelaunayTriangles() const
{
    // Collapse duplicate positions so the triangulation never sees zero-area
    // degeneracies. The active vector is a named local: DelaunayVertex keeps
    // pointers into it.
    const std::vector<GcpPoint> active = activePoints();
    std::vector<DelaunayVertex> vertices;
    for (const auto& pt : active) {
        bool dup = false;
        for (const auto& v : vertices) {
            if (std::hypot(pt.sourceX - v.x, pt.sourceY - v.y) < 1e-9) {
                dup = true;
                break;
            }
        }
        if (!dup)
            vertices.push_back({pt.sourceX, pt.sourceY, &pt});
    }

    std::vector<std::tuple<QString, QString, QString>> out;
    for (const auto& t : bowyerWatson(vertices)) {
        out.emplace_back(vertices[t.a].point->id, vertices[t.b].point->id, vertices[t.c].point->id);
    }
    return out;
}

GcpDistributionMetrics GcpManager::evaluateDistribution(double imageWidth, double imageHeight) const
{
    GcpDistributionMetrics metrics;
    const auto active = activePoints();
    metrics.activeCount = static_cast<int>(active.size());

    metrics.convexHullArea = shoelaceArea(computeConvexHull());
    const double imageArea = imageWidth * imageHeight;
    if (imageArea > 0.0)
        metrics.coverageRatio = metrics.convexHullArea / imageArea;

    // Clark-Evans nearest neighbour index (undefined below 2 points → 0).
    if (active.size() >= 2 && imageArea > 0.0) {
        double sumNN = 0.0;
        for (const auto& p : active) {
            double best = std::numeric_limits<double>::infinity();
            for (const auto& q : active) {
                if (&p == &q)
                    continue;
                best = std::min(best, std::hypot(p.sourceX - q.sourceX, p.sourceY - q.sourceY));
            }
            if (std::isfinite(best))
                sumNN += best;
        }
        const double rA = sumNN / static_cast<double>(active.size());
        const double density = static_cast<double>(active.size()) / imageArea;
        const double rE = 1.0 / (2.0 * std::sqrt(density));
        metrics.meanNearestNeighborDist = rA;
        metrics.expectedNearestNeighborDist = rE;
        if (rE > 0.0)
            metrics.clarkEvansIndex = rA / rE;
    }

    // Worst Delaunay triangle aspect ratio.
    double worstAspect = 0.0;
    {
        std::vector<DelaunayVertex> vertices;
        for (const auto& pt : active) {
            bool dup = false;
            for (const auto& v : vertices) {
                if (std::hypot(pt.sourceX - v.x, pt.sourceY - v.y) < 1e-9) {
                    dup = true;
                    break;
                }
            }
            if (!dup)
                vertices.push_back({pt.sourceX, pt.sourceY, &pt});
        }
        for (const auto& t : bowyerWatson(vertices))
            worstAspect = std::max(worstAspect, triangleAspectRatio(vertices[t.a], vertices[t.b], vertices[t.c]));
    }
    metrics.maxDelaunayAspectRatio = worstAspect;
    metrics.globalRmse = computeGlobalRmse();
    return metrics;
}

void GcpManager::updateResiduals(const std::vector<std::pair<double, double>>& transformedCoords)
{
    if (transformedCoords.size() != activeCount())
        return;
    size_t index = 0;
    for (auto& pt : mPoints) {
        if (!pt.enabled)
            continue;
        const auto& [tx, ty] = transformedCoords[index++];
        pt.residualX = tx - pt.targetX;
        pt.residualY = ty - pt.targetY;
        pt.residualTotal = std::hypot(pt.residualX, pt.residualY);
    }
}

double GcpManager::computeGlobalRmse() const
{
    double sumSq = 0.0;
    for (const auto& pt : mPoints) {
        if (!pt.enabled)
            continue;
        sumSq += pt.residualX * pt.residualX + pt.residualY * pt.residualY;
    }
    const size_t n = activeCount();
    if (n == 0)
        return 0.0;
    return std::sqrt(sumSq / static_cast<double>(n));
}

bool GcpManager::saveToCsv(const QString& filePath) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;
    QTextStream stream(&file);
    stream << "id,source_x,source_y,target_x,target_y,residual_x,residual_y,residual_total,enabled\n";
    for (const auto& pt : mPoints) {
        stream << pt.id << ',' << numberToString(pt.sourceX) << ',' << numberToString(pt.sourceY)
               << ',' << numberToString(pt.targetX) << ',' << numberToString(pt.targetY)
               << ',' << numberToString(pt.residualX) << ',' << numberToString(pt.residualY)
               << ',' << numberToString(pt.residualTotal) << ',' << (pt.enabled ? "1" : "0") << '\n';
    }
    // A truncated CSV on a full disk must not report success: flush and
    // surface stream/file errors (short writes used to pass silently).
    stream.flush();
    return stream.status() == QTextStream::Ok
           && file.error() == QFileDevice::NoError;
}

bool GcpManager::loadFromCsv(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    std::vector<GcpPoint> parsed;
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith("id,"))
            continue;
        const QStringList fields = line.split(',');
        if (fields.size() != 9)
            continue;
        GcpPoint pt;
        pt.id = fields[0].trimmed();
        bool okSx = false, okSy = false, okTx = false, okTy = false;
        bool okRx = false, okRy = false, okRt = false, okEnabled = false;
        pt.sourceX = fields[1].toDouble(&okSx);
        pt.sourceY = fields[2].toDouble(&okSy);
        pt.targetX = fields[3].toDouble(&okTx);
        pt.targetY = fields[4].toDouble(&okTy);
        pt.residualX = fields[5].toDouble(&okRx);
        pt.residualY = fields[6].toDouble(&okRy);
        pt.residualTotal = fields[7].toDouble(&okRt);
        pt.enabled = fields[8].trimmed().toInt(&okEnabled) != 0;
        const bool allParsed = okSx && okSy && okTx && okTy && okRx && okRy && okRt && okEnabled;
        if (pt.id.isEmpty() || !allParsed || !allFinite(pt))
            continue; // malformed row: skip per the header contract
        parsed.push_back(pt);
    }
    if (parsed.empty())
        return false;
    mPoints = std::move(parsed);
    return true;
}

QString GcpManager::toJson() const
{
    QJsonArray array;
    for (const auto& pt : mPoints) {
        QJsonObject obj;
        obj.insert("id", pt.id);
        obj.insert("source_x", pt.sourceX);
        obj.insert("source_y", pt.sourceY);
        obj.insert("target_x", pt.targetX);
        obj.insert("target_y", pt.targetY);
        obj.insert("residual_x", pt.residualX);
        obj.insert("residual_y", pt.residualY);
        obj.insert("residual_total", pt.residualTotal);
        obj.insert("enabled", pt.enabled);
        array.append(obj);
    }
    QJsonObject envelope;
    envelope.insert("gcps", array);
    return QString::fromUtf8(QJsonDocument(envelope).toJson(QJsonDocument::Compact));
}

bool GcpManager::fromJson(const QString& jsonStr)
{
    const QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
    if (!doc.isObject())
        return false;
    const QJsonValue value = doc.object().value("gcps");
    if (!value.isArray())
        return false;
    std::vector<GcpPoint> parsed;
    for (const auto& v : value.toArray()) {
        if (!v.isObject())
            return false;
        const QJsonObject obj = v.toObject();
        GcpPoint pt;
        pt.id = obj.value("id").toString();
        pt.sourceX = obj.value("source_x").toDouble();
        pt.sourceY = obj.value("source_y").toDouble();
        pt.targetX = obj.value("target_x").toDouble();
        pt.targetY = obj.value("target_y").toDouble();
        pt.residualX = obj.value("residual_x").toDouble();
        pt.residualY = obj.value("residual_y").toDouble();
        pt.residualTotal = obj.value("residual_total").toDouble();
        pt.enabled = obj.value("enabled").toBool(true);
        if (pt.id.isEmpty() || !allFinite(pt))
            return false;
        parsed.push_back(pt);
    }
    mPoints = std::move(parsed);
    return true;
}

} // namespace rs::core
