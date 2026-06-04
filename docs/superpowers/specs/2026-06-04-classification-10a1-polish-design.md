# Phase 10A.1 Classification Polish 设计

**日期:** 2026-06-04
**Phase:** 10A.1
**状态:** 设计完成，待写实现计划
**前置:** Phase 10A 完成（`docs/superpowers/specs/2026-06-04-classification-pixel-design.md`，commits `960ab12` → `0cb9a17`）

## 1. 目标

填三个 Phase 10A 留下的算法层缺口，让"功能存在但不能用"的三块完成闭环：

- **K-Means Hungarian assignment** — 让 K-Means 能算混淆矩阵
- **5-fold Cross Validation** — 替换当前的 QMessageBox stub
- **.yml 模型加载入口** — 训练一次后下次直接 predict，跳过样本采集

UI 改动尽量小（菜单 + 一两个对话框），核心是算法实现。

不在本 phase 范围（推迟到 10A.2 或 10B）：ROI 顶点编辑、PDF 导出、真实数据手工烟雾、设计稿 ui_diff_check、快速预览延迟基线。

## 2. 架构

### 2.1 子任务 3 个

| # | 子任务 | 新增文件 | 改动文件 |
|---|---|---|---|
| **10A.1.1** Hungarian | `rs_hungarian_assignment.{h,cpp}` + `test_hungarian_assignment.cpp` | `rs_classification_task.cpp` |
| **10A.1.2** 5-fold CV | `rs_cross_validation.{h,cpp}` + `test_cross_validation.cpp` | `qgsclassificationmainwindow.cpp` |
| **10A.1.3** 模型加载 | `rs_classifier_load_dialog.{h,cpp}` + `test_classifier_load_save.cpp` | `rs_classifier_backend.h`, `rs_classification_task.cpp`, `qgsclassificationmainwindow.{h,cpp}` |

### 2.2 依赖

无新外部依赖。复用：
- OpenCV ML 模块（Phase 11.5 引入）
- `RsAccuracyAssessment` (Phase 10A.9)
- `RsClassifierBackend` 抽象（Phase 10A.8）

## 3. 子任务设计

### 3.1 Task 10A.1.1: K-Means Hungarian Assignment

**问题：** K-Means 输出 cluster ID 1..K，与 ROI class ID 1..N 是任意置换。Task 10.9 直接跳过 K-Means 精度评价（commit `7dc93db`）。

**Hungarian assignment 算法：**

经典 O(n³) 实现（参考 Munkres 1957）。给定 N×N cost 矩阵，返回长度 N 的置换 vector，使得 Σ cost[i][assign[i]] 最小。

```cpp
// rs_hungarian_assignment.h
#pragma once
#include "qgis_analysis_export.h"
#include <opencv2/core.hpp>
#include <QVector>

class QGIS_ANALYSIS_EXPORT RsHungarianAssignment {
public:
    /// Solve min-cost assignment on an N×N cost matrix.
    /// Returns vector of length N: assign[row] = chosen column.
    /// Cost matrix must be CV_64F or CV_32F.
    static QVector<int> solve(const cv::Mat &costMatrix);
};
```

**Task 集成（K-Means 分支）：**

在 `rs_classification_task.cpp::run()` 当前的「跳过 K-Means 精度」块替换：

```cpp
if (mCfg.algoName == "KMeans" && mCfg.testX.rows > 0 && mCfg.testY.rows > 0) {
    cv::Mat predClusters = mCfg.backend->predict(mCfg.testX);
    // Collect unique true class IDs and cluster IDs
    QSet<int> trueIds, clusterIds;
    for (int i = 0; i < mCfg.testY.rows; ++i) trueIds.insert(mCfg.testY.at<int>(i, 0));
    for (int i = 0; i < predClusters.rows; ++i) clusterIds.insert(predClusters.at<int>(i, 0));
    if (trueIds.size() == clusterIds.size()) {
        // Build overlap matrix (rows = true classes, cols = clusters)
        QList<int> tList(trueIds.begin(), trueIds.end());
        QList<int> cList(clusterIds.begin(), clusterIds.end());
        std::sort(tList.begin(), tList.end());
        std::sort(cList.begin(), cList.end());
        const int N = tList.size();
        cv::Mat cost = cv::Mat::zeros(N, N, CV_64F);
        for (int i = 0; i < mCfg.testY.rows; ++i) {
            int ti = tList.indexOf(mCfg.testY.at<int>(i, 0));
            int ci = cList.indexOf(predClusters.at<int>(i, 0));
            if (ti >= 0 && ci >= 0) cost.at<double>(ti, ci) -= 1.0;
        }
        QVector<int> assign = RsHungarianAssignment::solve(cost);
        // Remap predicted clusters to class IDs
        QHash<int, int> remap;
        for (int i = 0; i < N; ++i) remap[cList[assign[i]]] = tList[i];
        QVector<int> yt, yp;
        for (int i = 0; i < mCfg.testY.rows; ++i) {
            yt.append(mCfg.testY.at<int>(i, 0));
            yp.append(remap.value(predClusters.at<int>(i, 0), -1));
        }
        try {
            mResult.accuracy = RsAccuracyAssessment::compute(yt, yp);
        } catch (const cv::Exception &) {}
    }
}
```

**边界：** 
- K != |unique testY| → 跳过（与之前行为一致，但加状态栏提示）
- N=0 → 跳过
- 不可分（cost 矩阵全 0）→ Hungarian 返回任意有效置换，accuracy 自然低

**测试 (`test_hungarian_assignment.cpp`)：**

```cpp
TEST_CASE("Hungarian: 3x3 identity cost yields identity assignment", "[classify][hungarian]")
TEST_CASE("Hungarian: 3x3 with off-diagonal optimum", "[classify][hungarian]")
TEST_CASE("Hungarian: 1x1 trivial", "[classify][hungarian]")
TEST_CASE("Hungarian: 6x6 diagonal-dominant returns identity-like", "[classify][hungarian]")
```

### 3.2 Task 10A.1.2: 5-fold Cross Validation

**当前：** `RsClassifierSetupBar::crossValidateRequested` 信号已 connect 到 `runCrossValidation()` slot，但 slot 只弹 `QMessageBox::information("Cross-validation coming soon — Phase 10A.1")`（Review patch `fd8f474`）。

**算法 — 分层 k-fold：**

```cpp
// rs_cross_validation.h
#pragma once
#include "qgis_analysis_export.h"
#include "rs_classifier_backend.h"
#include <opencv2/core.hpp>
#include <QVector>
#include <functional>
#include <memory>

class QGIS_ANALYSIS_EXPORT RsCrossValidation {
public:
    struct Result {
        double meanAccuracy = 0;
        double stdAccuracy = 0;
        QVector<double> foldAccuracies;
        QString errorMessage;
        bool ok() const { return errorMessage.isEmpty(); }
    };

    /// Stratified k-fold CV. factory() instantiates a fresh backend per fold.
    /// Each fold preserves class proportions; classes with < k samples
    /// are placed in train and excluded from test for the fold they would
    /// belong to (so test is smaller but non-empty).
    static Result kFold(const cv::Mat &X, const cv::Mat &y,
                        std::function<std::unique_ptr<RsClassifierBackend>()> factory,
                        int k = 5);
};
```

实现要点：
1. 按 class label 分桶 sample indices
2. 每桶 `std::shuffle(std::mt19937(42))` 确定性洗
3. 每桶 round-robin 分到 k 个 fold（保证每 fold 每类近似等量）
4. fold i 作为 test，其余作为 train
5. 每 fold：fresh `backend = factory()`, `fit(trainX, trainY)`, `predict(testX)`, 算 overall accuracy
6. 返回 mean ± std + per-fold list

**Wire（主窗口）：**

替换 `runCrossValidation()` 的 stub：

```cpp
void QgsClassificationMainWindow::runCrossValidation() {
    if (mSourceRasterPath.isEmpty()) {
        statusBar()->showMessage(tr("请先 Open source raster…"), 5000);
        return;
    }
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

    auto kind = mClassifierBar->currentKind();
    if (kind == RsClassifierKind::KMeans) {
        QMessageBox::information(this, tr("K-Means CV"),
            tr("K-Means 交叉验证不适用 (cluster ↔ class 标签不齐)。请用 NormalBayes 或 SVM。"));
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
    auto res = RsCrossValidation::kFold(X, y, factory, 5);
    if (!res.ok()) {
        QMessageBox::warning(this, tr("CV failed"), res.errorMessage);
        return;
    }
    QString perFold;
    for (int i = 0; i < res.foldAccuracies.size(); ++i)
        perFold += QString("fold%1=%2%\n").arg(i+1).arg(res.foldAccuracies[i]*100, 0, 'f', 1);
    QMessageBox::information(this, tr("5-fold CV"),
        tr("Mean accuracy: %1% ± %2%\n\n%3")
            .arg(res.meanAccuracy*100, 0, 'f', 1)
            .arg(res.stdAccuracy*100, 0, 'f', 1)
            .arg(perFold));
}
```

**测试 (`test_cross_validation.cpp`)：**

```cpp
TEST_CASE("CV: NormalBayes 5-fold on 3 Gaussians yields mean > 0.85", "[classify][cv]")
TEST_CASE("CV: per-fold accuracies populated", "[classify][cv]")
TEST_CASE("CV: empty data returns error", "[classify][cv]")
TEST_CASE("CV: < k samples uses smaller test bucket gracefully", "[classify][cv]")
```

### 3.3 Task 10A.1.3: .yml 模型加载入口

**目的：** 训练一次后存模型，下次只 predict 跳过样本采集 + 训练。常见场景：用 A 区域训练好模型，对 B 区域批量分类。

**API 改动：**

`RsClassifierBackend` 加 `isFitted()` 接口：

```cpp
class QGIS_ANALYSIS_EXPORT RsClassifierBackend {
public:
    // ... 现有接口 ...
    /// True if the backend can run predict() without fit() first.
    virtual bool isFitted() const { return false; }
};
```

具体实现：
- `RsClassifierNormalBayes::isFitted()` → `mClf && mClf->isTrained()`
- `RsClassifierSvm::isFitted()` → `mClf && mClf->isTrained()`
- `RsClassifierKMeans::isFitted()` → `!mCenters.empty()`

**File 菜单新增 `Load classifier model…`：**

```cpp
fileMenu->addAction(tr("Load classifier model..."), this,
                    &QgsClassificationMainWindow::loadClassifierModel);
```

**Load Dialog (`RsClassifierLoadDialog`)：**

加载流程需要用户告诉我们 YAML 是哪种算法（OpenCV YAML 根节点名字可以读但 v1 简化为用户选）：

```cpp
class RsClassifierLoadDialog : public QDialog {
    Q_OBJECT
public:
    enum class BackendKind { NormalBayes, SvmRbf };  // K-Means 不支持
    RsClassifierLoadDialog(QWidget *parent = nullptr);
    BackendKind selectedKind() const { return mKind; }
    QString modelPath() const { return mPath; }

private:
    BackendKind mKind = BackendKind::NormalBayes;
    QString mPath;
    // ... QRadioButton 三选 + 路径 QLineEdit + Browse + Ok/Cancel ...
};
```

**Main window load slot：**

```cpp
void QgsClassificationMainWindow::loadClassifierModel() {
    RsClassifierLoadDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;
    std::unique_ptr<RsClassifierBackend> backend;
    switch (dlg.selectedKind()) {
        case RsClassifierLoadDialog::BackendKind::NormalBayes:
            backend = std::make_unique<RsClassifierNormalBayes>(); break;
        case RsClassifierLoadDialog::BackendKind::SvmRbf:
            backend = std::make_unique<RsClassifierSvm>(); break;
    }
    if (!backend->load(dlg.modelPath())) {
        QMessageBox::warning(this, tr("Load failed"),
            tr("无法加载模型：%1").arg(dlg.modelPath()));
        return;
    }
    mLoadedBackend = std::move(backend);
    statusBar()->showMessage(
        tr("已加载模型 — 下次 Apply 将跳过训练"), 0);
}
```

**Apply slot 分支：**

```cpp
void QgsClassificationMainWindow::applyClassification() {
    // ... 既有校验 ...
    RsClassificationTask::Config cfg;
    cfg.sourceRaster = mSourceRasterPath;
    cfg.outputRaster = outPath;
    cfg.bandIndices = bands;

    if (mLoadedBackend) {
        // Predict-only mode
        cfg.backend = std::move(mLoadedBackend);
        cfg.algoName = "Loaded (" + cfg.backend->name() + ")";
        // No training data, no testX/testY
        statusBar()->showMessage(tr("使用已加载模型 (跳过训练)"), 3000);
    } else {
        // 既有训练流程：buildTrainingData → stratifiedSplit
        // ...
    }

    cfg.classColors = /* 既有 */;
    auto *task = new RsClassificationTask(std::move(cfg));
    // ... 既有 taskCompleted lambda ...
}
```

**Task::run() 跳过 fit 分支：**

```cpp
bool RsClassificationTask::run() {
    // ...
    if (!mCfg.backend) { /* error */ return false; }

    // Skip fit if backend is already trained (loaded mode)
    if (!mCfg.backend->isFitted()) {
        if (mCfg.trainX.empty() || mCfg.trainY.empty()) {
            mResult.errorMessage = "Backend not fitted and no training data";
            return false;
        }
        if (!mCfg.backend->fit(mCfg.trainX, mCfg.trainY)) {
            mResult.errorMessage = "Training failed";
            return false;
        }
    }
    mFb.setProgress(30.0);
    // ... 继续 tile-streamed predict ...
}
```

**Apply 后清空 `mLoadedBackend`** — 已经被 `std::move` 进 task 的 Config，但置 `nullptr` 表达"用过了"。下次 Apply 回到训练模式。状态栏从"已加载模型"复位到空。

**测试 (`test_classifier_load_save.cpp`)：**

```cpp
TEST_CASE("NormalBayes save+load: predictions identical", "[classify][persist]")
TEST_CASE("SVM save+load: predictions identical", "[classify][persist]")
TEST_CASE("isFitted: false before fit, true after", "[classify][persist]")
TEST_CASE("isFitted: true after load", "[classify][persist]")
```

## 4. 数据流（Apply 增量分支）

```
applyClassification 入口
  ↓
mLoadedBackend 非空?
  ├── 是 → cfg.backend = std::move(mLoadedBackend)
  │       cfg.trainX/trainY 空
  │       清空 mLoadedBackend
  │       不走 buildTrainingData / stratifiedSplit
  │
  └── 否 → 既有路径:
          buildTrainingData → stratifiedSplit → 构造 fresh backend → cfg
  ↓
new RsClassificationTask(std::move(cfg))
  ↓
run():
  backend->isFitted()?
    ├── 是 → 跳过 fit
    └── 否 → fit(trainX, trainY)
  ↓
  testX.rows > 0?
    ├── 是 → predict → 算 accuracy（K-Means 走 Hungarian 分支）
    └── 否 → 跳过
  ↓
  tile-streamed predict 全图 → 输出 GeoTIFF
  ↓
taskCompleted → 弹 RsAccuracyDialog (若有 accuracy) + 结构化 log
```

## 5. 测试矩阵

| 子任务 | 测试 | 关键断言 |
|---|---|---|
| 10A.1.1 | `test_hungarian_assignment.cpp` | 3×3 identity / 3×3 off-diag / 1×1 / 6×6 diag-dominant |
| 10A.1.2 | `test_cross_validation.cpp` | NormalBayes 3 高斯 mean > 0.85；per-fold 列表正确；空数据 error；< k 像元 graceful |
| 10A.1.3 | `test_classifier_load_save.cpp` | NormalBayes save+load 预测一致；SVM 同；isFitted true/false 转换 |

新增 3 测试文件，~10 TEST_CASEs。总测试 280 → 290+。

## 6. 风险

| # | 风险 | 概率 | 影响 | 缓解 |
|---|---|---|---|---|
| 1 | Hungarian O(n³) 在大 N 慢 | 低 | 低 | v1 限定 N ≤ 256 (uint8 class IDs)；典型 6 |
| 2 | K-Means K != unique testY | 中 | 低 | 跳过 accuracy + 状态栏提示，行为不变 |
| 3 | 5-fold 某 fold 缺类别 | 中 | 中 | 分层切分保证每 fold 至少 1 个该类样本；类样本 < k 时全 train |
| 4 | OpenCV YAML 文件版本不兼容 | 低 | 中 | load 返 false → UI 弹错误 + 不崩 |
| 5 | 用户加载的模型波段数与当前栅格不匹配 | 中 | 高 | predict 时 OpenCV 会抛 cv::Exception → catch → 状态栏报错 |
| 6 | `mLoadedBackend` 状态被遗忘（用户期望持续使用）| 中 | 低 | UX 决定：v1 一次性消耗；状态栏明显提示 |

## 7. Done When

- 290+ Catch2 测试全绿（280 + 10+ 新增）
- K-Means 现在在 ROI 模式 + K==N 时输出 accuracy + Kappa
- 工具栏「交叉验证」按钮弹出 5-fold mean ± std 真实数字
- File → Load classifier model… → 选 .yml → 状态栏提示 → Apply 跳过训练直接 predict
- 结构化日志保留：`algo` 字段标 "Loaded (NormalBayes)" 或类似时仍正确
- 完工 commit chain: 3 子任务 + 1 planning files = 4 commits

## 8. 已知未决

- **K-Means K > N 处理**：v1 跳过 accuracy；未来可"合并 cluster"或"丢弃多余 cluster"
- **K-Means K < N 处理**：同上，v1 跳过
- **加载的模型元数据**：v1 不存波段索引/CRS；用户自己负责对得上
- **批量分类（不同栅格用同模型）**：v1 一次性消耗 `mLoadedBackend`；未来加 toggle "保持已加载模型"
- **CV 取消语义**：当前 5-fold 同步阻塞主线程；典型样本数 < 5s 完成，可接受；大数据未来上 QgsTask 包装
