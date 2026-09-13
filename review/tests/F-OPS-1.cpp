// F-OPS-1 — class_mapping product-class values are not bounds-checked against
// the Labels output encoding (Byte/255 NoData). NOT added to CMake: review
// draft only (track contract: review-only, no build wiring).
//
// Expected failure on current master: class 2 pixels (product id 300) come
// back as 255 == NoData instead of 300 (or a typed manifest refusal).

#include <catch2/catch.hpp>

#include "operators/framework/model_catalog.h"
#include "operators/runtime/tile_inference_engine.h"
#include "operators/framework/rs_operator_context.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <gdal_priv.h>

namespace {

constexpr const char *kManifest = R"JSON({
  "name": "f-ops-1-labels-remap",
  "task": "segmentation",
  "framework": "onnx",
  "input": { "dtype": "float32", "band_roles": ["Red"] },
  "output": {
    "type": "raster",
    "classes": ["a", "b", "c"],
    "format": "labels"
  },
  "postprocess": { "class_mapping": [0, 1, 300] }
})JSON";

} // namespace

TEST_CASE( "F-OPS-1: labels class_mapping value beyond Byte/NoData domain is refused or preserved", "[review][F-OPS-1][static-only-run]" )
{
  // 1) Manifest-level expectation (preferred contract): a remap target that
  //    cannot be represented in the chosen output encoding is an InvalidManifest.
  const std::vector<std::string> issues =
    sicnu::operators::ModelCatalog::instance().validateManifestJson( kManifest );
  INFO( "catalog issues: " << issues.size() );
  // FAILS on master: the remap passes validation (non-negative + injective).
  CHECK( !issues.empty() );

  // 2) Engine-level expectation (defense in depth): the written Byte raster
  //    preserves product class 300 — impossible in GDT_Byte — or the engine
  //    escalates the encoding to UInt16 with NoData 65535. Either way the
  //    class must never silently collapse into the NoData sentinel 255.
  //    Reproducing the write path requires a model artifact + onnx session;
  //    the encoding selection alone is observable through the stats/palette:
  //    writeType stays GDT_Byte (classCount 3 <= 255) while product class 300
  //    is written, which GDAL clamps to 255 (== writeNoData) — silent loss.
  const sicnu::operators::ModelInfo *info = nullptr;
  sicnu::operators::ModelInfo local;
  if ( const auto found = sicnu::operators::ModelCatalog::instance().find( "f-ops-1-labels-remap" ) )
    local = *found;
  else
    local = sicnu::operators::ModelInfo{};
  info = &local;
  CHECK( info->postprocess.classMapping.size() == 3 );
  // The product domain the tally is sized for (1 + max = 301) exceeds the
  // Byte encoding the writer will choose (classes.size() = 3 <= 255):
  const int productClasses = 1 + *std::max_element( info->postprocess.classMapping.begin(),
                                                    info->postprocess.classMapping.end() );
  const int classCount = static_cast<int>( info->output.classes.size() );
  CHECK( productClasses > classCount ); // encoding decision uses classCount only — mismatch
  CHECK( productClasses > 255 );        // cannot be represented in GDT_Byte
}
