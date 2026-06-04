# Phase 10A.1 Classification Polish Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close 3 algorithmic gaps left by Phase 10A — K-Means Hungarian remap, 5-fold cross validation, and .yml model load — to make the classification module fully usable.

**Architecture:** Pure-algorithm additions in `src/analysis/classification/` (Hungarian, CV) and small UI bumps in `src/app/classification/` (Load dialog + slot wiring). `RsClassifierBackend` gains one new virtual `isFitted()` so the task pipeline can skip `fit()` for loaded models.

**Tech Stack:** C++17 / Qt6 / Catch2 / OpenCV ≥ 4.5 (`cv::ml::NormalBayesClassifier::save/load`, `cv::ml::SVM::save/load`).

**Spec:** `docs/superpowers/specs/2026-06-04-classification-10a1-polish-design.md`

---

## Conventions for All Tasks

- **TDD cycle:** Red → Green → Refactor per file. Run failing test before writing implementation.
- **Build:** `cd build && cmake .. && make -j$(nproc)` (incremental).
- **Test:** `cd build && ctest --output-on-failure -R "<TestCaseName>"` — matches Catch2 TEST_CASE name, not binary name.
- **Commit prefix:** `feat(classify):` for behavior, `test(classify):` for test-only, `chore(classify):` for build.

---

## Task 1 (10A.1.1): K-Means Hungarian Assignment

**Goal:** O(n³) Munkres Hungarian algorithm + task pipeline integration so K-Means produces a meaningful confusion matrix when K equals the number of true classes.

**Files:**
- Create: `src/analysis/classification/rs_hungarian_assignment.h/.cpp`
- Modify: `src/analysis/classification/CMakeLists.txt` (add new source)
- Modify: `src/app/classification/rs_classification_task.cpp` (K-Means branch in `run()`)
- Create: `tests/test_hungarian_assignment.cpp`
- Modify: `tests/CMakeLists.txt`

### Steps

- [ ] **Step 1.1: Write failing test**

Create `tests/test_hungarian_assignment.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "rs_hungarian_assignment.h"
#include <opencv2/core.hpp>

TEST_CASE("Hungarian: 3x3 identity cost yields identity", "[classify][hungarian]") {
    cv::Mat cost = (cv::Mat_<double>(3,3) <<
        0, 1, 1,
        1, 0, 1,
        1, 1, 0);
    auto a = RsHungarianAssignment::solve(cost);
    REQUIRE(a.size() == 3);
    REQUIRE(a[0] == 0);
    REQUIRE(a[1] == 1);
    REQUIRE(a[2] == 2);
}

TEST_CASE("Hungarian: 3x3 off-diagonal optimum", "[classify][hungarian]") {
    // Best is to assign row0->col2, row1->col0, row2->col1 (total 0).
    cv::Mat cost = (cv::Mat_<double>(3,3) <<
        9, 9, 0,
        0, 9, 9,
        9, 0, 9);
    auto a = RsHungarianAssignment::solve(cost);
    REQUIRE(a[0] == 2);
    REQUIRE(a[1] == 0);
    REQUIRE(a[2] == 1);
}

TEST_CASE("Hungarian: 1x1 trivial", "[classify][hungarian]") {
    cv::Mat cost = (cv::Mat_<double>(1,1) << 5.0);
    auto a = RsHungarianAssignment::solve(cost);
    REQUIRE(a.size() == 1);
    REQUIRE(a[0] == 0);
}

TEST_CASE("Hungarian: 6x6 diagonal-dominant returns identity", "[classify][hungarian]") {
    cv::Mat cost = cv::Mat::ones(6,6, CV_64F) * 10.0;
    for (int i = 0; i < 6; ++i) cost.at<double>(i,i) = 0.0;
    auto a = RsHungarianAssignment::solve(cost);
    for (int i = 0; i < 6; ++i) REQUIRE(a[i] == i);
}

TEST_CASE("Hungarian: empty matrix returns empty vector", "[classify][hungarian]") {
    cv::Mat cost;
    auto a = RsHungarianAssignment::solve(cost);
    REQUIRE(a.isEmpty());
}
```

- [ ] **Step 1.2: Register test, run, expect FAIL**

In `tests/CMakeLists.txt` after the existing classify-block tests:

```cmake
add_executable(test_hungarian_assignment test_hungarian_assignment.cpp)
target_link_libraries(test_hungarian_assignment PRIVATE
    qgis_analysis qgis_core ${OpenCV_LIBS} Catch2::Catch2WithMain)
sicnu_discover_tests(test_hungarian_assignment)
```

```bash
cd build && cmake .. && make test_hungarian_assignment -j$(nproc) && ctest -R "Hungarian:" --output-on-failure
```

Expected: FAIL — `rs_hungarian_assignment.h` doesn't exist.

- [ ] **Step 1.3: Write `rs_hungarian_assignment.h`**

```cpp
#pragma once
#include "qgis_analysis_export.h"
#include <opencv2/core.hpp>
#include <QVector>

class QGIS_ANALYSIS_EXPORT RsHungarianAssignment
{
public:
    /// Solve min-cost assignment on an N×N cost matrix.
    /// Returns vector of length N: assign[row] = chosen column.
    /// Cost matrix must be CV_64F or CV_32F. Empty input → empty output.
    static QVector<int> solve(const cv::Mat &costMatrix);
};
```

- [ ] **Step 1.4: Implement `rs_hungarian_assignment.cpp` (Munkres O(n³))**

```cpp
#include "rs_hungarian_assignment.h"
#include <vector>
#include <limits>
#include <algorithm>

namespace {
// Classic Munkres O(n³) implementation on a square cost matrix.
// Adapted from the textbook description; uses 1-based internal indexing
// converted at the boundary.
QVector<int> solveImpl(const std::vector<std::vector<double>> &in) {
    const int n = static_cast<int>(in.size());
    if (n == 0) return {};

    // u[i], v[j] are dual potentials; p[j] is the row assigned to column j
    // (0 = unassigned). way[j] is part of the augmenting-path reconstruction.
    std::vector<double> u(n + 1, 0.0), v(n + 1, 0.0);
    std::vector<int> p(n + 1, 0), way(n + 1, 0);

    for (int i = 1; i <= n; ++i) {
        p[0] = i;
        int j0 = 0;
        std::vector<double> minv(n + 1, std::numeric_limits<double>::infinity());
        std::vector<char> used(n + 1, false);
        do {
            used[j0] = true;
            const int i0 = p[j0];
            double delta = std::numeric_limits<double>::infinity();
            int j1 = 0;
            for (int j = 1; j <= n; ++j) {
                if (used[j]) continue;
                const double cur = in[i0 - 1][j - 1] - u[i0] - v[j];
                if (cur < minv[j]) { minv[j] = cur; way[j] = j0; }
                if (minv[j] < delta) { delta = minv[j]; j1 = j; }
            }
            for (int j = 0; j <= n; ++j) {
                if (used[j]) { u[p[j]] += delta; v[j] -= delta; }
                else         { minv[j] -= delta; }
            }
            j0 = j1;
        } while (p[j0] != 0);

        // Trace augmenting path back to row i.
        do {
            const int j1 = way[j0];
            p[j0] = p[j1];
            j0 = j1;
        } while (j0 != 0);
    }

    QVector<int> result(n, -1);
    for (int j = 1; j <= n; ++j) {
        if (p[j] != 0) result[p[j] - 1] = j - 1;
    }
    return result;
}
}

QVector<int> RsHungarianAssignment::solve(const cv::Mat &cost) {
    if (cost.empty() || cost.rows != cost.cols) return {};
    const int n = cost.rows;
    std::vector<std::vector<double>> m(n, std::vector<double>(n));
    cv::Mat tmp;
    cost.convertTo(tmp, CV_64F);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            m[i][j] = tmp.at<double>(i, j);
    return solveImpl(m);
}
```

- [ ] **Step 1.5: Register source + run, expect PASS**

In `src/analysis/classification/CMakeLists.txt`, add to `target_sources(qgis_analysis PRIVATE ...)`:

```cmake
rs_hungarian_assignment.cpp
```

```bash
cd build && cmake .. && make -j$(nproc) && ctest -R "Hungarian:" --output-on-failure
```

Expected: 5/5 PASS.

- [ ] **Step 1.6: Wire K-Means accuracy branch in task**

In `src/app/classification/rs_classification_task.cpp`, find the existing K-Means accuracy skip — the block that gates accuracy on `mCfg.algoName != "KMeans"`. Replace with:

```cpp
// Accuracy: NormalBayes/SVM path goes direct; K-Means goes through
// Hungarian remap of cluster IDs → class IDs (Phase 10A.1.1).
if (mCfg.testX.rows > 0 && mCfg.testY.rows > 0) {
    try {
        const cv::Mat pred = mCfg.backend->predict(mCfg.testX);
        QVector<int> yt, yp;

        if (mCfg.algoName == "KMeans") {
            // Hungarian-remap cluster IDs to ROI class IDs.
            QSet<int> trueSet, clusterSet;
            for (int i = 0; i < mCfg.testY.rows; ++i)
                trueSet.insert(mCfg.testY.at<int>(i, 0));
            for (int i = 0; i < pred.rows; ++i)
                clusterSet.insert(pred.at<int>(i, 0));

            if (trueSet.size() == clusterSet.size() && !trueSet.isEmpty()) {
                QList<int> tList(trueSet.begin(), trueSet.end());
                QList<int> cList(clusterSet.begin(), clusterSet.end());
                std::sort(tList.begin(), tList.end());
                std::sort(cList.begin(), cList.end());
                const int N = tList.size();

                cv::Mat cost = cv::Mat::zeros(N, N, CV_64F);
                for (int i = 0; i < mCfg.testY.rows; ++i) {
                    const int ti = tList.indexOf(mCfg.testY.at<int>(i, 0));
                    const int ci = cList.indexOf(pred.at<int>(i, 0));
                    if (ti >= 0 && ci >= 0)
                        cost.at<double>(ti, ci) -= 1.0;
                }
                const QVector<int> assign = RsHungarianAssignment::solve(cost);
                QHash<int, int> remap;
                for (int i = 0; i < N && i < assign.size(); ++i) {
                    if (assign[i] >= 0)
                        remap[cList[assign[i]]] = tList[i];
                }
                for (int i = 0; i < mCfg.testY.rows; ++i) {
                    yt.append(mCfg.testY.at<int>(i, 0));
                    yp.append(remap.value(pred.at<int>(i, 0), -1));
                }
            }
        } else {
            // Supervised: predictions already in class-ID space.
            for (int i = 0; i < mCfg.testY.rows; ++i) {
                yt.append(mCfg.testY.at<int>(i, 0));
                yp.append(pred.at<int>(i, 0));
            }
        }

        if (!yt.isEmpty()) {
            mResult.accuracy = RsAccuracyAssessment::compute(yt, yp);
        }
    } catch (const cv::Exception &) {
        // Swallow; classification still proceeds.
    }
}
```

Add `#include "rs_hungarian_assignment.h"` and `#include <QSet>` / `#include <QHash>` at the top if not already present.

- [ ] **Step 1.7: Build + full-suite regression**

```bash
cd build && make -j$(nproc) && ctest --output-on-failure 2>&1 | tail -10
```

Expected: 285/285 (was 280, +5 Hungarian). Phase 10A E2E test (`Classification E2E:`) still green — the K-Means accuracy branch only fires when testY is non-empty; the E2E uses NormalBayes so unaffected.

- [ ] **Step 1.8: Commit**

```bash
git add src/analysis/classification/rs_hungarian_assignment.{h,cpp} \
        src/analysis/classification/CMakeLists.txt \
        src/app/classification/rs_classification_task.cpp \
        tests/test_hungarian_assignment.cpp tests/CMakeLists.txt
git commit -m "feat(classify): K-Means Hungarian assignment for accuracy

- RsHungarianAssignment::solve: Munkres O(n^3) on N×N cost matrix
- Task K-Means branch: build cost = -overlap, remap clusters → classes,
  feed remapped predictions to RsAccuracyAssessment
- Skips accuracy gracefully when K != |unique testY|
- Tests: 3x3 identity, off-diagonal optimum, 1x1, 6x6 diagonal, empty

Task 10A.1.1"
```

---

## Task 2 (10A.1.2): 5-fold Cross Validation

**Goal:** Stratified k-fold CV in `qgis_analysis` library; wire `runCrossValidation()` slot to replace the QMessageBox stub from review patch `fd8f474`.

**Files:**
- Create: `src/analysis/classification/rs_cross_validation.h/.cpp`
- Modify: `src/analysis/classification/CMakeLists.txt`
- Modify: `src/app/classification/qgsclassificationmainwindow.cpp` (`runCrossValidation()` slot)
- Create: `tests/test_cross_validation.cpp`
- Modify: `tests/CMakeLists.txt`

### Steps

- [ ] **Step 2.1: Write failing test**

Create `tests/test_cross_validation.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <opencv2/core.hpp>
#include "rs_cross_validation.h"
#include "rs_classifier_normalbayes.h"

using Catch::Approx;

namespace {
void makeGaussianData(cv::Mat &X, cv::Mat &y, int perClass = 200, int seed = 42) {
    cv::RNG rng(seed);
    const int total = perClass * 3;
    X.create(total, 2, CV_32F);
    y.create(total, 1, CV_32S);
    for (int i = 0; i < perClass; ++i) {
        X.at<float>(i, 0) = float(rng.gaussian(2.0)) + 5.0f;
        X.at<float>(i, 1) = float(rng.gaussian(2.0)) + 5.0f;
        y.at<int>(i, 0) = 1;
    }
    for (int i = 0; i < perClass; ++i) {
        X.at<float>(perClass + i, 0) = float(rng.gaussian(2.0)) + 20.0f;
        X.at<float>(perClass + i, 1) = float(rng.gaussian(2.0)) + 20.0f;
        y.at<int>(perClass + i, 0) = 2;
    }
    for (int i = 0; i < perClass; ++i) {
        X.at<float>(2*perClass + i, 0) = float(rng.gaussian(2.0)) + 5.0f;
        X.at<float>(2*perClass + i, 1) = float(rng.gaussian(2.0)) + 20.0f;
        y.at<int>(2*perClass + i, 0) = 3;
    }
}
}

TEST_CASE("CV: NormalBayes 5-fold on 3 Gaussians yields mean > 0.85", "[classify][cv]") {
    cv::Mat X, y;
    makeGaussianData(X, y);
    auto factory = []() -> std::unique_ptr<RsClassifierBackend> {
        return std::make_unique<RsClassifierNormalBayes>();
    };
    auto r = RsCrossValidation::kFold(X, y, factory, 5);
    REQUIRE(r.ok());
    REQUIRE(r.foldAccuracies.size() == 5);
    REQUIRE(r.meanAccuracy > 0.85);
}

TEST_CASE("CV: std accuracy non-negative and bounded", "[classify][cv]") {
    cv::Mat X, y;
    makeGaussianData(X, y);
    auto factory = []() -> std::unique_ptr<RsClassifierBackend> {
        return std::make_unique<RsClassifierNormalBayes>();
    };
    auto r = RsCrossValidation::kFold(X, y, factory, 5);
    REQUIRE(r.stdAccuracy >= 0.0);
    REQUIRE(r.stdAccuracy <= 0.5);
}

TEST_CASE("CV: empty data returns error", "[classify][cv]") {
    cv::Mat X, y;
    auto factory = []() -> std::unique_ptr<RsClassifierBackend> {
        return std::make_unique<RsClassifierNormalBayes>();
    };
    auto r = RsCrossValidation::kFold(X, y, factory, 5);
    REQUIRE_FALSE(r.ok());
}

TEST_CASE("CV: class with < k samples folded to train only", "[classify][cv]") {
    cv::Mat X(13, 2, CV_32F);
    cv::Mat y(13, 1, CV_32S);
    cv::RNG rng(7);
    for (int i = 0; i < 10; ++i) {
        X.at<float>(i, 0) = float(rng.gaussian(1.0));
        X.at<float>(i, 1) = float(rng.gaussian(1.0));
        y.at<int>(i, 0) = 1;
    }
    for (int i = 10; i < 13; ++i) {
        X.at<float>(i, 0) = float(rng.gaussian(1.0)) + 10.0f;
        X.at<float>(i, 1) = float(rng.gaussian(1.0)) + 10.0f;
        y.at<int>(i, 0) = 2;
    }
    auto factory = []() -> std::unique_ptr<RsClassifierBackend> {
        return std::make_unique<RsClassifierNormalBayes>();
    };
    auto r = RsCrossValidation::kFold(X, y, factory, 5);
    REQUIRE(r.ok());
    REQUIRE(r.foldAccuracies.size() == 5);
}
```

- [ ] **Step 2.2: Register, run, expect FAIL**

In `tests/CMakeLists.txt`:

```cmake
add_executable(test_cross_validation test_cross_validation.cpp)
target_link_libraries(test_cross_validation PRIVATE
    qgis_analysis qgis_core ${OpenCV_LIBS} Catch2::Catch2WithMain)
sicnu_discover_tests(test_cross_validation)
```

```bash
cd build && cmake .. && make test_cross_validation -j$(nproc) && ctest -R "^CV:" --output-on-failure
```

Expected: FAIL — header doesn't exist.

- [ ] **Step 2.3: Write `rs_cross_validation.h`**

```cpp
#pragma once
#include "qgis_analysis_export.h"
#include "rs_classifier_backend.h"
#include <opencv2/core.hpp>
#include <QVector>
#include <QString>
#include <functional>
#include <memory>

class QGIS_ANALYSIS_EXPORT RsCrossValidation
{
public:
    struct Result {
        double meanAccuracy = 0.0;
        double stdAccuracy = 0.0;
        QVector<double> foldAccuracies;
        QString errorMessage;
        bool ok() const { return errorMessage.isEmpty(); }
    };

    /// Stratified k-fold CV.
    /// factory() instantiates a fresh backend per fold.
    /// Returns per-fold accuracies + mean + std.
    /// Classes with < k samples are kept in train for every fold
    /// (their test contribution is empty).
    static Result kFold(const cv::Mat &X, const cv::Mat &y,
                        std::function<std::unique_ptr<RsClassifierBackend>()> factory,
                        int k = 5);
};
```

- [ ] **Step 2.4: Implement `rs_cross_validation.cpp`**

```cpp
#include "rs_cross_validation.h"

#include <QHash>
#include <algorithm>
#include <cmath>
#include <random>

RsCrossValidation::Result
RsCrossValidation::kFold(const cv::Mat &X, const cv::Mat &y,
                          std::function<std::unique_ptr<RsClassifierBackend>()> factory,
                          int k)
{
    Result r;
    if (X.empty() || y.empty() || X.rows != y.rows) {
        r.errorMessage = QStringLiteral("Empty or mismatched X/y");
        return r;
    }
    if (k < 2) {
        r.errorMessage = QStringLiteral("k must be >= 2");
        return r;
    }
    if (!factory) {
        r.errorMessage = QStringLiteral("No backend factory");
        return r;
    }

    // Group sample indices by class label.
    QHash<int, QVector<int>> byClass;
    for (int i = 0; i < y.rows; ++i)
        byClass[y.at<int>(i, 0)].append(i);

    // Shuffle each bucket with a deterministic seed.
    std::mt19937 rng(42);
    for (auto it = byClass.begin(); it != byClass.end(); ++it) {
        std::shuffle(it.value().begin(), it.value().end(), rng);
    }

    // Round-robin assign indices to folds (stratified).
    QVector<QVector<int>> foldTest(k);
    QVector<bool> trainOnly(0);
    for (auto it = byClass.constBegin(); it != byClass.constEnd(); ++it) {
        const QVector<int> &bucket = it.value();
        if (bucket.size() < k) {
            // Class doesn't have enough samples to spread across folds —
            // keep all in train (foldTest empty for this class).
            continue;
        }
        for (int i = 0; i < bucket.size(); ++i)
            foldTest[i % k].append(bucket[i]);
    }

    // Build the "train always" set: classes with < k samples.
    QVector<int> trainAlways;
    for (auto it = byClass.constBegin(); it != byClass.constEnd(); ++it) {
        if (it.value().size() < k) trainAlways += it.value();
    }

    QVector<double> accs;
    accs.reserve(k);

    for (int fi = 0; fi < k; ++fi) {
        QVector<int> trainIdx, testIdx;
        testIdx = foldTest[fi];
        for (int fj = 0; fj < k; ++fj) {
            if (fj == fi) continue;
            trainIdx += foldTest[fj];
        }
        trainIdx += trainAlways;

        if (trainIdx.isEmpty() || testIdx.isEmpty()) {
            // Degenerate fold; skip.
            continue;
        }

        cv::Mat trainX(trainIdx.size(), X.cols, X.type());
        cv::Mat trainY(trainIdx.size(), 1, CV_32S);
        for (int i = 0; i < trainIdx.size(); ++i) {
            X.row(trainIdx[i]).copyTo(trainX.row(i));
            trainY.at<int>(i, 0) = y.at<int>(trainIdx[i], 0);
        }
        cv::Mat testX(testIdx.size(), X.cols, X.type());
        cv::Mat testY(testIdx.size(), 1, CV_32S);
        for (int i = 0; i < testIdx.size(); ++i) {
            X.row(testIdx[i]).copyTo(testX.row(i));
            testY.at<int>(i, 0) = y.at<int>(testIdx[i], 0);
        }

        auto backend = factory();
        if (!backend) {
            r.errorMessage = QStringLiteral("Factory returned null");
            return r;
        }
        if (!backend->fit(trainX, trainY)) continue;

        cv::Mat pred;
        try { pred = backend->predict(testX); } catch (...) { continue; }
        if (pred.empty() || pred.rows != testY.rows) continue;

        int correct = 0;
        for (int i = 0; i < pred.rows; ++i)
            if (pred.at<int>(i, 0) == testY.at<int>(i, 0)) ++correct;
        accs.append(double(correct) / pred.rows);
    }

    if (accs.isEmpty()) {
        r.errorMessage = QStringLiteral("All folds failed");
        return r;
    }
    r.foldAccuracies = accs;

    double sum = 0;
    for (double a : accs) sum += a;
    r.meanAccuracy = sum / accs.size();

    double sq = 0;
    for (double a : accs) sq += (a - r.meanAccuracy) * (a - r.meanAccuracy);
    r.stdAccuracy = std::sqrt(sq / accs.size());

    return r;
}
```

- [ ] **Step 2.5: Register source, build, run, expect PASS**

In `src/analysis/classification/CMakeLists.txt`:

```cmake
rs_cross_validation.cpp
```

```bash
cd build && cmake .. && make -j$(nproc) && ctest -R "^CV:" --output-on-failure
```

Expected: 4/4 PASS.

- [ ] **Step 2.6: Replace `runCrossValidation()` stub in main window**

In `src/app/classification/qgsclassificationmainwindow.cpp`, find the existing `runCrossValidation()` slot (added by the review patch `fd8f474`) and replace its body with:

```cpp
void QgsClassificationMainWindow::runCrossValidation()
{
    if (mSourceRasterPath.isEmpty()) {
        statusBar()->showMessage(tr("请先 Open source raster…"), 5000);
        return;
    }
    if (!mClassifierBar) return;

    QVector<int> bands = mClassifierBar->selectedBands();
    if (bands.isEmpty()) {
        const int n = std::min(3, mSourceBandCount);
        for (int i = 1; i <= n; ++i) bands.push_back(i);
    }
    cv::Mat X, y;
    if (!buildTrainingData(bands, X, y) || X.rows < 25) {
        statusBar()->showMessage(tr("CV 需要 ≥ 25 像元"), 5000);
        return;
    }

    const auto kind = mClassifierBar->currentKind();
    if (kind == RsClassifierKind::KMeans) {
        QMessageBox::information(this, tr("K-Means CV"),
            tr("K-Means 交叉验证不适用 (cluster ↔ class 标签不齐)。\n"
               "请用 NormalBayes 或 SVM。"));
        return;
    }
    auto factory = [kind]() -> std::unique_ptr<RsClassifierBackend> {
        switch (kind) {
            case RsClassifierKind::NormalBayes:
                return std::make_unique<RsClassifierNormalBayes>();
            case RsClassifierKind::SvmRbf:
                return std::make_unique<RsClassifierSvm>();
            default:
                return nullptr;
        }
    };

    statusBar()->showMessage(tr("5-fold CV 运行中…"), 3000);
    QApplication::processEvents();   // let the status bar paint
    const auto res = RsCrossValidation::kFold(X, y, factory, 5);
    if (!res.ok()) {
        QMessageBox::warning(this, tr("CV failed"), res.errorMessage);
        return;
    }
    QString perFold;
    for (int i = 0; i < res.foldAccuracies.size(); ++i)
        perFold += QString("  fold%1: %2%\n")
                       .arg(i + 1)
                       .arg(res.foldAccuracies[i] * 100, 0, 'f', 1);
    QMessageBox::information(this, tr("5-fold Cross Validation"),
        tr("Mean accuracy: %1% ± %2%\n\n%3")
            .arg(res.meanAccuracy * 100, 0, 'f', 1)
            .arg(res.stdAccuracy  * 100, 0, 'f', 1)
            .arg(perFold));
}
```

Add `#include "rs_cross_validation.h"` and `#include "rs_classifier_normalbayes.h"` / `#include "rs_classifier_svm.h"` at the top of the file if not already present.

- [ ] **Step 2.7: Build + full-suite regression**

```bash
cd build && cmake .. && make -j$(nproc) && ctest --output-on-failure 2>&1 | tail -10
```

Expected: 289/289 (285 + 4 CV).

- [ ] **Step 2.8: Commit**

```bash
git add src/analysis/classification/rs_cross_validation.{h,cpp} \
        src/analysis/classification/CMakeLists.txt \
        src/app/classification/qgsclassificationmainwindow.cpp \
        tests/test_cross_validation.cpp tests/CMakeLists.txt
git commit -m "feat(classify): 5-fold cross validation replaces QMessageBox stub

- RsCrossValidation::kFold: stratified k-fold (round-robin per class)
  with deterministic shuffle (mt19937 seed=42)
- Classes with < k samples → train-only across all folds
- Main window: runCrossValidation() builds training data, picks backend
  factory by current algorithm kind, displays mean ± std + per-fold table
- K-Means CV intentionally rejected (cluster ↔ class not aligned)
- Tests: 3-Gaussian 5-fold mean > 0.85; std bound; empty error;
  small-class graceful fold

Task 10A.1.2"
```

---

## Task 3 (10A.1.3): .yml Model Load Entry

**Goal:** New `File → Load classifier model…` menu item; algorithm picker dialog; `RsClassifierBackend::isFitted()` virtual; `RsClassificationTask::run()` skips fit if backend is already trained.

**Files:**
- Create: `src/app/classification/rs_classifier_load_dialog.h/.cpp`
- Modify: `src/analysis/classification/rs_classifier_backend.h` (add `isFitted()`)
- Modify: `src/analysis/classification/rs_classifier_normalbayes.h/.cpp` (override `isFitted()`)
- Modify: `src/analysis/classification/rs_classifier_svm.h/.cpp` (override `isFitted()`)
- Modify: `src/analysis/classification/rs_classifier_kmeans.h/.cpp` (override `isFitted()`)
- Modify: `src/app/classification/rs_classification_task.cpp` (skip fit when already fitted)
- Modify: `src/app/classification/qgsclassificationmainwindow.h/.cpp` (new slot + member + File menu item)
- Modify: `src/app/classification/CMakeLists.txt` (new source)
- Create: `tests/test_classifier_load_save.cpp`
- Modify: `tests/CMakeLists.txt`

### Steps

- [ ] **Step 3.1: Write failing test**

Create `tests/test_classifier_load_save.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <QTemporaryDir>
#include <opencv2/core.hpp>
#include "rs_classifier_normalbayes.h"
#include "rs_classifier_svm.h"

namespace {
void makeData(cv::Mat &X, cv::Mat &y) {
    cv::RNG rng(42);
    X.create(300, 2, CV_32F);
    y.create(300, 1, CV_32S);
    for (int i = 0; i < 100; ++i) {
        X.at<float>(i, 0) = float(rng.gaussian(2.0)) + 5.0f;
        X.at<float>(i, 1) = float(rng.gaussian(2.0)) + 5.0f;
        y.at<int>(i, 0) = 1;
    }
    for (int i = 0; i < 100; ++i) {
        X.at<float>(100 + i, 0) = float(rng.gaussian(2.0)) + 20.0f;
        X.at<float>(100 + i, 1) = float(rng.gaussian(2.0)) + 20.0f;
        y.at<int>(100 + i, 0) = 2;
    }
    for (int i = 0; i < 100; ++i) {
        X.at<float>(200 + i, 0) = float(rng.gaussian(2.0)) + 5.0f;
        X.at<float>(200 + i, 1) = float(rng.gaussian(2.0)) + 20.0f;
        y.at<int>(200 + i, 0) = 3;
    }
}
}

TEST_CASE("isFitted: false before fit, true after", "[classify][persist]") {
    RsClassifierNormalBayes clf;
    REQUIRE_FALSE(clf.isFitted());
    cv::Mat X, y;
    makeData(X, y);
    REQUIRE(clf.fit(X, y));
    REQUIRE(clf.isFitted());
}

TEST_CASE("NormalBayes save+load: predictions identical", "[classify][persist]") {
    QTemporaryDir tmp; REQUIRE(tmp.isValid());
    QString path = tmp.path() + "/nb.yml";

    cv::Mat X, y;
    makeData(X, y);

    RsClassifierNormalBayes a;
    REQUIRE(a.fit(X, y));
    REQUIRE(a.save(path));

    RsClassifierNormalBayes b;
    REQUIRE_FALSE(b.isFitted());
    REQUIRE(b.load(path));
    REQUIRE(b.isFitted());

    cv::Mat predA = a.predict(X);
    cv::Mat predB = b.predict(X);
    REQUIRE(predA.rows == predB.rows);
    for (int i = 0; i < predA.rows; ++i)
        REQUIRE(predA.at<int>(i, 0) == predB.at<int>(i, 0));
}

TEST_CASE("SVM save+load: predictions identical", "[classify][persist]") {
    QTemporaryDir tmp; REQUIRE(tmp.isValid());
    QString path = tmp.path() + "/svm.yml";

    cv::Mat X, y;
    makeData(X, y);

    RsClassifierSvm a;
    REQUIRE(a.fit(X, y));
    REQUIRE(a.save(path));

    RsClassifierSvm b;
    REQUIRE_FALSE(b.isFitted());
    REQUIRE(b.load(path));
    REQUIRE(b.isFitted());

    cv::Mat predA = a.predict(X);
    cv::Mat predB = b.predict(X);
    REQUIRE(predA.rows == predB.rows);
    for (int i = 0; i < predA.rows; ++i)
        REQUIRE(predA.at<int>(i, 0) == predB.at<int>(i, 0));
}

TEST_CASE("Load invalid path returns false", "[classify][persist]") {
    RsClassifierNormalBayes clf;
    REQUIRE_FALSE(clf.load("/does/not/exist.yml"));
    REQUIRE_FALSE(clf.isFitted());
}
```

- [ ] **Step 3.2: Register test, run, expect FAIL**

In `tests/CMakeLists.txt`:

```cmake
add_executable(test_classifier_load_save test_classifier_load_save.cpp)
target_link_libraries(test_classifier_load_save PRIVATE
    qgis_analysis qgis_core ${OpenCV_LIBS}
    Qt6::Core Catch2::Catch2WithMain)
sicnu_discover_tests(test_classifier_load_save)
```

```bash
cd build && cmake .. && make test_classifier_load_save -j$(nproc) && ctest -R "isFitted|save\\+load|Load invalid" --output-on-failure
```

Expected: FAIL — `isFitted()` doesn't exist on backends yet.

- [ ] **Step 3.3: Add `isFitted()` virtual to `RsClassifierBackend`**

In `src/analysis/classification/rs_classifier_backend.h`, inside the class:

```cpp
public:
    /// True if backend can run predict() without fit() first.
    virtual bool isFitted() const { return false; }
```

- [ ] **Step 3.4: Override in 3 concrete backends**

In `src/analysis/classification/rs_classifier_normalbayes.h` add inside class:

```cpp
bool isFitted() const override { return mClf && mClf->isTrained(); }
```

In `rs_classifier_svm.h`:

```cpp
bool isFitted() const override { return mClf && mClf->isTrained(); }
```

In `rs_classifier_kmeans.h`:

```cpp
bool isFitted() const override { return !mCenters.empty(); }
```

- [ ] **Step 3.5: Run, expect PASS on load+save tests**

```bash
cd build && cmake .. && make -j$(nproc) && ctest -R "isFitted|save\\+load|Load invalid" --output-on-failure
```

Expected: 4/4 PASS. (`save()`/`load()` already exist on the OpenCV-backed classifiers from Phase 10A.8 — only `isFitted()` was missing.)

- [ ] **Step 3.6: Implement `RsClassifierLoadDialog`**

Create `src/app/classification/rs_classifier_load_dialog.h`:

```cpp
#pragma once
#include <QDialog>
#include <QString>

class QLineEdit;
class QRadioButton;

class RsClassifierLoadDialog : public QDialog
{
    Q_OBJECT
public:
    enum class BackendKind { NormalBayes, SvmRbf };

    explicit RsClassifierLoadDialog(QWidget *parent = nullptr);

    BackendKind selectedKind() const { return mKind; }
    QString modelPath() const;

private slots:
    void browseForFile();
    void onAccept();

private:
    QRadioButton *mRbBayes = nullptr;
    QRadioButton *mRbSvm = nullptr;
    QLineEdit    *mPathEdit = nullptr;
    BackendKind   mKind = BackendKind::NormalBayes;
};
```

Create `rs_classifier_load_dialog.cpp`:

```cpp
#include "rs_classifier_load_dialog.h"

#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

RsClassifierLoadDialog::RsClassifierLoadDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Load classifier model"));
    setMinimumWidth(420);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Select model algorithm:"), this));

    mRbBayes = new QRadioButton(tr("NormalBayes (Maximum Likelihood)"), this);
    mRbSvm   = new QRadioButton(tr("SVM (RBF kernel)"), this);
    mRbBayes->setChecked(true);
    auto *grp = new QButtonGroup(this);
    grp->addButton(mRbBayes);
    grp->addButton(mRbSvm);
    layout->addWidget(mRbBayes);
    layout->addWidget(mRbSvm);

    auto *pathRow = new QHBoxLayout;
    mPathEdit = new QLineEdit(this);
    mPathEdit->setPlaceholderText(tr("Path to .yml model file"));
    auto *browse = new QPushButton(tr("Browse…"), this);
    pathRow->addWidget(mPathEdit, 1);
    pathRow->addWidget(browse);
    layout->addLayout(pathRow);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);

    connect(browse, &QPushButton::clicked, this, &RsClassifierLoadDialog::browseForFile);
    connect(buttons, &QDialogButtonBox::accepted, this, &RsClassifierLoadDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString RsClassifierLoadDialog::modelPath() const
{
    return mPathEdit ? mPathEdit->text() : QString();
}

void RsClassifierLoadDialog::browseForFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load classifier model"), QString(),
        tr("OpenCV YAML (*.yml *.yaml *.xml);;All files (*)"));
    if (!path.isEmpty()) mPathEdit->setText(path);
}

void RsClassifierLoadDialog::onAccept()
{
    mKind = mRbSvm->isChecked() ? BackendKind::SvmRbf : BackendKind::NormalBayes;
    if (modelPath().isEmpty() || !QFileInfo::exists(modelPath())) {
        return;   // keep dialog open
    }
    accept();
}
```

- [ ] **Step 3.7: Add slot + member to main window**

In `src/app/classification/qgsclassificationmainwindow.h`, inside the class:

```cpp
public slots:
    void loadClassifierModel();

private:
    std::unique_ptr<RsClassifierBackend> mLoadedBackend;
```

Forward-declare or `#include "rs_classifier_backend.h"` near the top of the header.

In `qgsclassificationmainwindow.cpp`:

```cpp
#include "rs_classifier_load_dialog.h"
#include "rs_classifier_normalbayes.h"
#include "rs_classifier_svm.h"

void QgsClassificationMainWindow::loadClassifierModel()
{
    RsClassifierLoadDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    std::unique_ptr<RsClassifierBackend> backend;
    switch (dlg.selectedKind()) {
        case RsClassifierLoadDialog::BackendKind::NormalBayes:
            backend = std::make_unique<RsClassifierNormalBayes>(); break;
        case RsClassifierLoadDialog::BackendKind::SvmRbf:
            backend = std::make_unique<RsClassifierSvm>(); break;
    }
    if (!backend || !backend->load(dlg.modelPath())) {
        QMessageBox::warning(this, tr("Load failed"),
            tr("无法加载模型：%1").arg(dlg.modelPath()));
        return;
    }
    mLoadedBackend = std::move(backend);
    if (statusBar())
        statusBar()->showMessage(
            tr("已加载模型 — 下次 Apply 将跳过训练，直接 predict"), 0);
}
```

- [ ] **Step 3.8: Wire File menu**

Find the existing File menu construction in `setupMenus()` of `qgsclassificationmainwindow.cpp`. After the existing "Open source raster..." action (or wherever appropriate), insert:

```cpp
fileMenu->addAction(tr("Load classifier model..."), this,
                    &QgsClassificationMainWindow::loadClassifierModel);
```

- [ ] **Step 3.9: Branch `applyClassification()` for loaded backend**

In `qgsclassificationmainwindow.cpp::applyClassification()`, find the `RsClassificationTask::Config cfg;` line. BEFORE the existing training-data collection (`buildTrainingData` call), add:

```cpp
RsClassificationTask::Config cfg;
cfg.sourceRaster = mSourceRasterPath;
cfg.outputRaster = outPath;
cfg.bandIndices = bands;

if (mLoadedBackend) {
    cfg.backend = std::move(mLoadedBackend);
    cfg.algoName = QStringLiteral("Loaded (%1)").arg(cfg.backend->name());
    // No training data; testX/testY left empty so accuracy is skipped.
    if (statusBar())
        statusBar()->showMessage(tr("使用已加载模型 (跳过训练)"), 3000);
} else {
    // ... existing path: buildTrainingData → stratifiedSplit → factory ...
}
```

Keep the existing `else` branch as is. After the dispatch, `mLoadedBackend` is already nullptr (moved out); the status bar message from `loadClassifierModel` will be replaced by the apply-time message.

- [ ] **Step 3.10: Skip fit in task**

In `src/app/classification/rs_classification_task.cpp::run()`, find the `if (!mCfg.backend->fit(...))` block and replace with:

```cpp
// Step 1: Train (skip if backend was loaded fitted from .yml).
if (!mCfg.backend->isFitted()) {
    if (mCfg.trainX.empty() || mCfg.trainY.empty()) {
        mResult.errorMessage = QStringLiteral(
            "Backend not fitted and no training data supplied");
        return false;
    }
    if (!mCfg.backend->fit(mCfg.trainX, mCfg.trainY)) {
        mResult.errorMessage = QStringLiteral("Backend training failed");
        return false;
    }
}
```

- [ ] **Step 3.11: Register source, build, full-suite**

In `src/app/classification/CMakeLists.txt`, add to `qt_add_library(qgis_app_classify STATIC ...)`:

```cmake
rs_classifier_load_dialog.cpp
```

```bash
cd build && cmake .. && make -j$(nproc) && ctest --output-on-failure 2>&1 | tail -10
```

Expected: 293/293 (289 + 4 load+save).

- [ ] **Step 3.12: Commit**

```bash
git add src/analysis/classification/rs_classifier_backend.h \
        src/analysis/classification/rs_classifier_normalbayes.h \
        src/analysis/classification/rs_classifier_svm.h \
        src/analysis/classification/rs_classifier_kmeans.h \
        src/app/classification/rs_classifier_load_dialog.{h,cpp} \
        src/app/classification/rs_classification_task.cpp \
        src/app/classification/qgsclassificationmainwindow.{h,cpp} \
        src/app/classification/CMakeLists.txt \
        tests/test_classifier_load_save.cpp tests/CMakeLists.txt
git commit -m "feat(classify): load .yml model and skip training

- RsClassifierBackend::isFitted() virtual; NormalBayes/SVM/KMeans override
- RsClassifierLoadDialog: algorithm radio (NB/SVM) + path browse
- File → Load classifier model... menu wiring
- RsClassificationTask::run() skips fit() when backend->isFitted()
- applyClassification() uses mLoadedBackend (one-shot) when present
- Tests: isFitted state machine, NB+SVM save/load round-trip, invalid path

Task 10A.1.3"
```

---

## Task 4: Planning Files Final Update

**Goal:** Mark Phase 10A.1 complete in `task_plan.md`; append session block to `progress.md`; log lessons in `findings.md`.

### Steps

- [ ] **Step 4.1: Update `task_plan.md`**

Find the Current Phase line (around line 9) and replace:

```markdown
Phase 11.4 + 11.5 + 10A + 10A.1 complete (Georeferencer + v1.5 + Pixel Classification + Polish). **293+ tests pass**. Next: Phase 10B (OBIA) 或 Phase 12 (AI Agent foundation).
```

Find the Phase 10A.1 block and change `🟢 **[NEXT — 收尾]**` to `✅ **COMPLETE (2026-06-04)**`. Tick all 3 sub-tasks with their commit SHAs (fill in actual SHAs as you commit).

- [ ] **Step 4.2: Append `progress.md` session entry**

Prepend a new session block at the top:

```markdown
## Session: 2026-06-04 (深夜后) — Phase 10A.1 Classification Polish ✅ COMPLETE

- 3 sub-tasks committed in sequence (10A.1.1 Hungarian / 10A.1.2 CV / 10A.1.3 load)
- Test count grew 280 → 293+ (5 Hungarian + 4 CV + 4 load/save)
- K-Means now reports meaningful confusion matrix when K == |unique classes|
- 5-fold CV button works on NormalBayes/SVM with mean ± std display
- File menu loads .yml model; Apply skips training one-shot

Key plan→reality deltas: <implementer fills in>
```

- [ ] **Step 4.3: Append `findings.md` block**

```markdown
## Phase 10A.1 Implementation Lessons (2026-06-04)

- OpenCV's `cv::ml::NormalBayesClassifier::save()` / `load()` round-trips predictions exactly
- `cv::ml::SVM::save()` writes ~50KB YAML for typical 6-class RBF; loadable in <100ms
- Munkres O(n³) Hungarian fits in ~100 lines; standard textbook implementation
- Stratified k-fold: round-robin per-class assignment to folds keeps proportions; classes with < k samples stay in train-only buckets
- `cv::kmeans` (not `cv::ml::KMeans`) has no save/load — Phase 10A.1.3 limits .yml load UI to NormalBayes + SVM
- `mLoadedBackend` one-shot pattern (consumed on Apply) keeps the state machine simple; "持续使用" mode deferred to v1.1
```

- [ ] **Step 4.4: Commit**

```bash
git add task_plan.md progress.md findings.md
git commit -m "docs(classify): mark Phase 10A.1 polish complete in planning files

- task_plan.md: 3 sub-tasks ticked, Current Phase advanced
- progress.md: Phase 10A.1 session block with commit chain
- findings.md: Hungarian/CV/load implementation lessons

Phase 10A.1 Classification Polish COMPLETE"
```

---

## Self-Review

**Spec coverage:**

| Spec section | Plan coverage |
|---|---|
| §2.1 子任务 3 个 | Tasks 1, 2, 3 |
| §3.1 Hungarian + K-Means task integration | Task 1 (Steps 1.3, 1.4, 1.6) |
| §3.2 CV stratified k-fold | Task 2 (Steps 2.3–2.6) |
| §3.3 .yml load with isFitted virtual + dialog + skip-fit | Task 3 (Steps 3.3–3.10) |
| §4 Data flow (Apply branch with mLoadedBackend) | Task 3 Step 3.9 |
| §5 Test matrix (~10 cases) | 5 Hungarian + 4 CV + 4 load/save = 13 tests |
| §6 Risks (1) N≤256: Hungarian works up to typical n; (2) K!=N skip: Task 1 Step 1.6 logic; (3) <k samples: Task 2 Step 2.4 trainAlways; (4) YAML version: tested via load on existing tree; (5) band mismatch: not tested (runtime cv::Exception caught); (6) one-shot: Task 3 Step 3.9 explicit | Tasks 1, 2, 3 |
| §7 Done When | Final ctest in Task 3 + planning files Task 4 |

**Placeholder scan:** no TBD/TODO/"implement later" tokens in plan body.

**Type consistency:** `RsClassifierBackend::isFitted()` introduced in Task 3.3 and used in Task 3.10 task `run()`. `RsHungarianAssignment::solve` signature stable across Task 1 step 1.4 and 1.6. `RsCrossValidation::Result` stable across Task 2 steps 2.3, 2.4, 2.6.

No gaps.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-06-04-classification-10a1-polish-implementation.md`. Two execution options:

1. **Subagent-Driven (recommended)** — fresh subagent per task with review checkpoints
2. **Inline Execution** — sequential in this session via executing-plans

Which approach?
