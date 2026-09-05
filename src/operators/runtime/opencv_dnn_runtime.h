// src/operators/runtime/opencv_dnn_runtime.h — ONNX provider on cv::dnn.
//
// Baseline InferenceProvider for the model runtime layer (ADR 0122 follow-up
// "GPU-batch execution queue"): loads ONNX weights once per session, selects
// the best backend/target the host actually offers (CUDA when the model asks
// for GPU and the OpenCV build has it, CPU otherwise per the manifest's
// cpu_fallback), and serializes forward passes on one session — cv::dnn::Net
// is not safe for concurrent infer() on the same object.
#pragma once

#include "operators/runtime/model_runtime.h"

#include <opencv2/dnn.hpp>

#include <mutex>
#include <string>

namespace sicnu::operators::runtime {

class OpenCvDnnRuntime final : public IModelRuntime
{
  public:
    /**
     * @param artifactPath  ONNX weight file (absolute)
     * @param modelWantsGpu manifest runtime.gpu
     * @param hw            detected host capabilities
     */
    OpenCvDnnRuntime( std::string artifactPath, bool modelWantsGpu,
                      const ModelHardwareCapabilities &hw );

    /// Parse the weights and bind the backend. Returns false with *errorMessage on failure.
    bool load( std::string *errorMessage = nullptr );

    std::string framework() const override { return "onnx"; }
    std::string backendName() const override { return "opencv_dnn"; }
    std::string deviceName() const override { return m_deviceName; }
    std::string artifactPath() const override { return m_artifactPath; }

    cv::Mat infer( const cv::Mat &nchwBlob ) override;
    cv::Mat infer( const cv::Mat &nchwBlob, const std::string &outputName ) override;

    // Platform 3.0: named multi-input forward passes (cv::dnn setInput by
    // blob name, one forward over all inputs).
    bool supportsMultiInput() const override { return true; }
    std::vector<cv::Mat> inferMulti( const std::vector<NamedBlob> &namedBlobs ) override;

    /// The ONNX graph's unconnected output layer names (empty before load or
    /// when enumeration fails — consumers treat that as "unknown", #705).
    std::vector<std::string> outputTensorNames() const override;

  private:
    std::string m_artifactPath;
    bool m_modelWantsGpu;
    ModelHardwareCapabilities m_hw;
    cv::dnn::Net m_net;
    std::string m_deviceName = "cpu";
    std::mutex m_inferMutex;
    bool m_loaded = false;
};

} // namespace sicnu::operators::runtime
