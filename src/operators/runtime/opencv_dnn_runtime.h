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

#include <atomic>
#include <mutex>
#include <string>

namespace sicnu::operators::runtime {

class OpenCvDnnRuntime final : public IModelRuntime
{
  public:
    /**
     * @param artifactPath  ONNX weight file (absolute)
     * @param modelWantsGpu manifest runtime.gpu (already device-resolved by the registry)
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

    // --- Platform 4.0 unified contract -------------------------------------
    /// One throwaway 1x3x64x64 forward pass. Non-fatal: fixed-shape graphs
    /// may reject the probe; the outcome lands in health(), never in load().
    void warmup() override;

    /// Cooperative cancel: the NEXT checkpointed forward throws; a forward
    /// already handed to cv::dnn runs to completion (documented limitation).
    void requestCancel() override { m_cancelRequested.store( true, std::memory_order_relaxed ); }
    void clearCancel() override { m_cancelRequested.store( false, std::memory_order_relaxed ); }

    SessionHealth health() const override;
    SessionMemoryEstimate memoryEstimate() const override;

  private:
    std::string m_artifactPath;
    bool m_modelWantsGpu;
    ModelHardwareCapabilities m_hw;
    cv::dnn::Net m_net;
    std::string m_deviceName = "cpu";
    std::mutex m_inferMutex;
    bool m_loaded = false;

    // Platform 4.0 health/cancel state (atomics: health() never takes the
    // inference lock, so a stuck forward cannot block the probe).
    std::atomic<bool> m_cancelRequested{ false };
    std::atomic<std::uint64_t> m_forwards{ 0 };
    std::atomic<std::uint64_t> m_failures{ 0 };
    mutable std::mutex m_healthMutex; // guards lastError/lastForwardMs only
    double m_lastForwardMs = 0.0;
    std::string m_lastError;
};

} // namespace sicnu::operators::runtime
