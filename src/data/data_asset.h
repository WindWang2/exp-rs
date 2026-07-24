#pragma once

#include <optional>
#include <utility>

#include <QString>
#include <QVector>

#include "asset_types.h"
#include "data_result.h"
#include "source_descriptor.h"

namespace sicnu::data
{

class DataManager;

struct RegisterRequest
{
  SourceDescriptor source;
  PersistencePolicy persistence = PersistencePolicy::ProjectPersistent;
};

struct RegisterResult
{
  AssetId assetId;
  bool reusedExisting = false;
  QVector<Diagnostic> diagnostics;
};

} // namespace sicnu::data
