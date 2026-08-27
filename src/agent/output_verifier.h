#pragma once

#include "sicnu_agent_export.h"

#include <json/json.h>

#include <QString>
#include <QStringList>

namespace sicnu::data
{
enum class AssetKind;
}

namespace sicnu::agent
{

/// Lightweight structured report for a committed output.
struct OutputVerification
{
  bool ok = false;
  QString kind;
  Json::Value summary;
  QStringList issues;
  QStringList warnings;
};

/// Verifies that a committed GIS output is structurally healthy without
/// scanning the whole dataset.  Used by the agent run coordinator and wired
/// into the ToolCallDispatcher result payload builder.
class SICNU_AGENT_EXPORT OutputVerifier
{
  public:
    OutputVerifier() = default;

    /// Verify @a path using the hint @a kindHint ("raster" or "vector").
    /// When the hint is empty the verifier tries raster first, then vector.
    OutputVerification verify( const QString &path, const QString &kindHint = QString() ) const;

    /// Verify a raster dataset.
    static OutputVerification verifyRaster( const QString &path );

    /// Verify a vector dataset.
    static OutputVerification verifyVector( const QString &path );

  private:
    static QString kindHintFromPath( const QString &path );
};

} // namespace sicnu::agent
