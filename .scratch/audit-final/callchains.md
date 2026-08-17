# Call-chain Maps

## A. Georeferencing

### Entry points
```
Menu: src/app/main_window_menus.cpp:380-385  栅格 → 预处理 → 影像配准 → "影像对影像 (I2I)" / "影像对地图 (I2M)"
Ribbon: src/app/shell/ribbon_controller.cpp:827-829  (same slots)
  → QgisDesktopWindow::openGeorefImageToImage / openGeorefImageToMap
  → src/app/main_window_view.cpp:71 / :104
```

### Constructor chain
```
QgsGeoreferencerMainWindow (qgsgeoreferencermainwindow.cpp:38)
  → setupCentralWidget (qgsgeoreferencermainwindow.cpp:58)
  → QgisDesktopWindow secondary view registration (:93-96)
  → super: QgsGeorefShellWindow ctor (qgsgeoref_shell_window.cpp:94)
  → finishCommonSetup (qgsgeoref_shell_window.cpp:124)  — wires docks, tools, table, params panel, task list
```

### GCP acquisition (I2I dual-canvas)
```
QgsGeorefToolAddPoint::canvasPressEvent (qgsgeoreftooladdpoint.cpp:44)
  emit pointPicked
  → onSourcePointPicked (qgsgeoref_shell_window.cpp:1724) → beginPendingSourcePick (:1654)
  → onDestPointPicked (:1738)
  → commitGcpPair (:1672) — rejects (0,0) source when dest non-zero (:1680-1689)
  → RsGeoreferencingSession::addGcp (rs_georeferencing_session.cpp:193)
  → emit gcpsChanged → shell onPointsChanged (qgsgeoref_shell_window.cpp:1095)
  → refit() (rs_georeferencing_session.cpp:271)
  → emit fitChanged → shell onSessionFitChanged (:1270)
```

### Fit engine
```
QgsGeorefTransform::fit (qgsgeoreftransform.cpp:315)
  → minimumGcpCountFor (qgsgcptransformer.cpp:73-96)
  → collectEnabledGcps (qgsgeoreftransform.cpp:295)
  → makeConfiguredTransform (:30) — new + loadRaster + setRpcOptions
  → updateParametersFromGcps (:162)
      ├─ Linear     → qgsleastsquares.cpp:32 (closed-form)
      ├─ Helmert    → qgsleastsquares.cpp:79 (GSL LU 4x4)
      ├─ Poly 1/2/3 → qgsgcptransformer.cpp:387 (GDALCreateGCPTransformer)
      ├─ TPS        → qgsgcptransformer.cpp:384 (GDALCreateTPSTransformer)
      ├─ Projective → qgsleastsquares.cpp:296 (GSL SVD 2nx9, with normalization)
      └─ RPC        → qgsrpcgcptransformer.cpp:65 (GDALCreateRPCTransformerV2 + optional refinement)
  → sourcePixelResiduals + pixelRms (qgsgeoreftransform.cpp:48, 80)
```

### Warp (Apply)
```
QgsGeorefShellWindow::applyTransform (qgsgeoref_shell_window.cpp:1391)
  → RsGeoreferencingSession::createWarpSnapshot (rs_georeferencing_session.cpp:281)
  → startWarpTask (:325)
  → transformFromSnapshot (:308) — re-fits (no try/catch here!)
  → cloneTransform (qgsgeoreftransform.cpp:148) — re-fits (no try/catch here!)
  → new RsWarpTask (rs_warp_task.cpp:8)
  → TaskCenter::submitJob
  → RsWarpTask::run (rs_warp_task.cpp:34)
  → QgsImageWarper::warpFile (qgsimagewarper.cpp:163 or 263)
  → GDALSuggestedWarpOutput → GDALChunkAndWarpImage
  → session.onTaskUpdated → emit warpFinished
  → shell warpFinished lambda (qgsgeoref_shell_window.cpp:248)
```

## B. Classification

### Entry point
```
Menu: src/app/main_window_menus.cpp:484  (gated by SICNU_HAS_CLASSIFY)
  → QgisDesktopWindow::m_classifyWindow (main_window_view.cpp:141)
  → new QgsClassificationMainWindow
```

### Train → Predict pipeline
```
QgsClassificationMainWindow::applyClassification (qgsclassificationmainwindow.cpp:2222)
  → buildTrainingData (qgsclassificationmainwindow.cpp:2176)
  → RsTrainingDataExtraction::extract (rs_training_data_extraction.cpp:255)
  → RsClassificationSplit::stratifiedSplit (rs_classification_split.cpp:15)
  → fitScalerOntoConfig (qgsclassificationmainwindow.cpp:198)
  → RsFeatureScaler::fit(trainX) + transform  (rs_feature_scaler.cpp:18, 50)
  → backend->fit (per type)
  → RsClassificationPipeline::run (rs_classification_pipeline.cpp:178)
      ├─ loadModelSidecar (predict-only) (:201)
      ├─ RsClassifierBackendFactory::create (:244)
      ├─ backend->load(modelLoadPath) (:247)
      ├─ RsTrainingDataExtraction::extractFromVector (:268)
      ├─ RsClassificationSplit::stratifiedSplit (:331)
      ├─ RsFeatureScaler::fit(trainX) + transform (:340-349)
      ├─ backend->fit(trainX, trainY) (:367)
      ├─ backend->save + saveModelSidecar (:383-395)
      ├─ backend->predict(trainX) → Hungarian cost → RsHungarianAssignment::solve (:428-454)
      ├─ backend->predict(testX) → RsAccuracyAssessment::compute (:476-504)
      └─ tile loop 256x256 (:731-873)
          ├─ band RasterIO → cv::Mat X(npx,B) (:745-758)
          ├─ isIgnorePixel per pixel (:766)
          ├─ RsFeatureScaler::transform(X) (:772)
          ├─ backend->predict(X) (:784)
          ├─ backend->predictProbabilities(X) (:813) when enabled
          └─ outBuf / probBuf → RasterIO (:853-861)
```

### Per-backend ownership
| Backend | File | Notes |
|---|---|---|
| SVM | `rs_classifier_svm.cpp` (C_SVC + RBF) | `cv::ml::SVM` template |
| NormalBayes | `rs_classifier_normalbayes.cpp` | `cv::ml::NormalBayesClassifier` template + custom predictProb row-normalize |
| MLP | `rs_classifier_mlp.cpp` | `cv::ml::ANN_MLP` + RPROP + SIGMOID_SYM; argmax over `mClassLabels`; softmax probs |
| RandomForest | `rs_classifier_random_forest.cpp` | `cv::ml::RTrees`; `predictProbabilities` from `getVotes` |
| KMeans | `rs_classifier_kmeans.cpp` | `cv::kmeans` (KMEANS_PP_CENTERS, 3 attempts); per-pixel predict loop |

### Post-process
```
QgsClassificationMainWindow::runPostProcess (qgsclassificationmainwindow.cpp:2694)
  → startPostProcessTask (:2702)
  → RsPostProcessTask::run (rs_post_process_task.cpp:98)
      → RsPostProcess::loadLabelRaster (:133)
      → sieve (:167) → majorityFilter (:185) → clump (:204) → recode (:223)
      → saveLabelRaster (:240) → polygonize (:256)
```

### Cross-validation
```
runCrossValidation (qgsclassificationmainwindow.cpp:2851)
  → RsCrossValidation::kFold (rs_cross_validation.cpp:13)
      per fold: scaler fit → backend->fit → backend->predict(testX) (:121-158)
```

### Headless consumers (operators)
- `src/operators/rs/rs_supervised_classification_operator.cpp` (rs:supervised_classification)
- `src/operators/rs/rs_kmeans_operator.cpp` (rs:kmeans_classification) — uses all-zero dummy y
