// src/operators/framework/model_catalog.h
#pragma once

#include <json/json.h>

#include <optional>
#include <string>
#include <vector>

namespace sicnu::operators {

/**
 * One registered model runtime entry (ADR 0122). Deserialized from
 * models/<name>/model.json manifests; weight files are referenced by path
 * and are not shipped with the repository.
 */
struct ModelInfo {
  std::string name;        ///< Unique id, e.g. "sam-building"
  std::string task;        ///< Task family: segmentation | classification | detection | ...
  std::string inputType;   ///< Input contract, e.g. "raster"
  std::string outputType;  ///< Output contract, e.g. "polygon" | "raster"
  std::string framework;   ///< Runtime, e.g. "onnx"
  std::string path;        ///< Local weight file path (optional until downloaded)
  bool gpu = false;        ///< Whether a GPU is expected/required
  double accuracy = -1.0;  ///< Optional benchmark accuracy in [0, 1] (<0 = unreported)
  std::string description;
  std::vector<std::string> tags;
  std::string sourceManifest; ///< Manifest file this entry was loaded from

  Json::Value toJson() const;
};

/**
 * ModelCatalog — scans a directory of model manifests (models/<name>/
 * model.json) so agents and the inference operator can discover available
 * model runtimes by task and contract (ADR 0122 step 7). Resolution order
 * for the default directory: $SICNU_MODELS_DIR, <cwd>/models,
 * <application dir>/../models.
 */
class ModelCatalog {
  public:
    static ModelCatalog &instance();

    /// Explicitly overrides the scanned directory and reloads.
    void setDirectory( const std::string &dir );

    /// Currently scanned directory (defaultModelsDirectory() until overridden).
    std::string directory() const;

    /// Default models directory resolution (env → cwd → app-relative).
    static std::string defaultModelsDirectory();

    /// (Re)reads every models/<name>/model.json under the directory.
    /// Missing directory yields an empty catalog, not an error.
    void reload();

    std::vector<ModelInfo> models() const;
    std::vector<ModelInfo> modelsByTask( const std::string &task ) const;
    std::optional<ModelInfo> find( const std::string &name ) const;

  private:
    ModelCatalog() = default;

    std::string mDirectory;
    std::vector<ModelInfo> mModels;
};

} // namespace sicnu::operators
