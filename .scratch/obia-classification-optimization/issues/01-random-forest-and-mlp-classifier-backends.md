# 01 — Random Forest & MLP Classifier Backend Expansion

Type: prototype
Status: resolved
Blocked by:

## Question

OpenCV ML supports `cv::ml::RTrees` (Random Forest) and `cv::ml::ANN_MLP` (Multi-Layer Perceptron), but `RsClassifierBackendFactory`, `RsObiaMainWindow`, and `RsObiaClassifyOperator` schema currently only expose SVM, NormalBayes, and KMeans.

How should we expand `RsClassifierBackendFactory` to support `RsRandomForestBackend` (`cv::ml::RTrees`) and `RsMlpBackend` (`cv::ml::ANN_MLP`), and expose key hyperparameters (`numTrees`, `maxDepth`, `minSampleCount`) across `RsObiaMainWindow` UI and `RsObiaClassifyOperator` parameters?
